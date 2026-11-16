// STUTTER engine (offline, runs on the render worker). Works on the FULL selection:
// every repeat plays up to one grid step of the selection; when the selection is longer than a
// step and 'scan' is on, successive repeats walk through the whole selection so no part of it
// is silently dropped. Pitch is never altered (no time-stretching).
#pragma once
#include "Biquad.h"
#include "Common.h"
#include "Divisions.h"
#include "SpectralShifter.h"

namespace cray
{
enum class Direction { Forward = 0, Reverse, PingPong, Random };

struct StutterParams
{
    int repeats = 4;                 // 2..32
    int division = 2;                // index into kDivisionBeats
    Direction direction = Direction::Forward;
    bool scan = true;                // walk through a selection longer than one step
    float swing = 0.0f;              // 0..1
    float gate = 1.0f;               // 0.05..1, portion of the step that sounds
    float humanize = 0.0f;           // 0..1 (timing +-10% of a step, level +-1.5 dB max)
    float fadeMs = 3.0f;             // micro fade on every slice edge
    uint32_t seed = 0xC0FFEEu;
};

struct RenderSettings
{
    StutterParams stutter;
    float saturation = 0.0f;         // 0..1 drive
    bool phone = false;              // HP 320 Hz + 4th-order LP 3.4 kHz
    float pitchSemitones = 0.0f;      // -12..+12, duration-preserving phase vocoder
    float formantSemitones = 0.0f;    // -6..+6, independent spectral-envelope shift
    float delayMix = 0.0f;            // 0..1
    int delayDivision = 2;              // tempo-synchronised index into kDivisionBeats
    float delayFeedback = 0.28f;       // 0..0.85
    float reverbMix = 0.0f;            // 0..1
    float reverbSize = 0.5f;           // 0..1
    double bpm = 120.0;
};

struct RenderStats { double seconds = 0, peak = 0; size_t slices = 0; };

namespace detail
{
inline void applyFades (std::vector<float>& l, std::vector<float>& r, size_t n, size_t fade, bool fi, bool fo)
{
    fade = std::min (fade, n / 2);
    for (size_t i = 0; i < fade; ++i)
    {
        const float g = std::sin (0.5f * (float) kPi * ((float) i + 0.5f) / (float) fade);   // equal-power
        if (fi) { l[i] *= g; r[i] *= g; }
        if (fo) { l[n - 1 - i] *= g; r[n - 1 - i] *= g; }
    }
}
} // namespace detail

inline StereoBuffer renderStutter (const StereoBuffer& sel, const RenderSettings& rs, RenderStats* stats = nullptr,
                                   const std::atomic<bool>* cancel = nullptr, std::atomic<float>* progress = nullptr)
{
    StereoBuffer out;
    out.sampleRate = sel.sampleRate;
    if (progress) progress->store (0.0f, std::memory_order_release);
    const StutterParams& p = rs.stutter;
    const size_t selLen = sel.size();
    if (selLen < 16 || sel.sampleRate <= 0.0) return out;

    const int reps = std::max (2, std::min (32, p.repeats));
    const double step = std::max (16.0, divisionSamples (p.division, rs.bpm, sel.sampleRate));
    const size_t contentLen = (size_t) std::max (16.0, std::min ((double) selLen, std::floor (step * std::max (0.05f, std::min (1.0f, p.gate)))));
    const size_t fade = (size_t) std::max (1.0, p.fadeMs * 0.001 * sel.sampleRate);
    Rng rng (p.seed);

    struct Slice { double start; size_t src; bool rev; float gain; };
    std::vector<Slice> slices;
    double endMax = 0.0;
    for (int r = 0; r < reps; ++r)
    {
        double t = (double) r * step;
        if ((r & 1) && p.swing > 0.0f) t += (double) p.swing * step / 3.0;
        float gain = 1.0f;
        if (p.humanize > 0.0f)
        {
            t += (double) rng.bipolar() * p.humanize * 0.10 * step;
            gain = std::pow (10.0f, rng.bipolar() * p.humanize * 1.5f / 20.0f);
        }
        t = std::max (0.0, t);
        size_t src = 0;
        if (p.scan && selLen > contentLen && reps > 1)
            src = (size_t) std::llround ((double) (selLen - contentLen) * (double) r / (double) (reps - 1));
        bool rev = false;
        switch (p.direction)
        {
            case Direction::Forward:  rev = false; break;
            case Direction::Reverse:  rev = true; break;
            case Direction::PingPong: rev = (r & 1) != 0; break;
            case Direction::Random:   rev = (rng.next() & 1u) != 0; break;
        }
        slices.push_back ({ t, src, rev, gain });
        endMax = std::max (endMax, t + (double) contentLen);
    }
    const size_t total = (size_t) std::ceil (std::max (endMax, (double) reps * step));
    out.resize (total);

    std::vector<float> bl (contentLen), br (contentLen);
    size_t done = 0;
    for (const Slice& s : slices)
    {
        if (cancel && cancel->load (std::memory_order_relaxed)) return StereoBuffer{};
        for (size_t i = 0; i < contentLen; ++i)
        {
            const size_t k = s.src + (s.rev ? contentLen - 1 - i : i);
            bl[i] = sel.l[k]; br[i] = sel.r[k];
        }
        // PingPong joins reversed/forward slices at identical sample values: skip the fade there.
        const bool pp = p.direction == Direction::PingPong && ! p.scan; // only fixed-source joins share identical edge samples
        const size_t idx = done;
        detail::applyFades (bl, br, contentLen, fade, ! (pp && idx > 0), ! (pp && idx + 1 < slices.size()));
        const size_t at = (size_t) std::llround (s.start);
        for (size_t i = 0; i < contentLen && at + i < total; ++i) { out.l[at + i] += bl[i] * s.gain; out.r[at + i] += br[i] * s.gain; }
        ++done;
        if (progress) progress->store (0.65f * (float) done / (float) slices.size(), std::memory_order_release);
    }

    // Independent pitch/formant processing is performed offline on the completed phrase.
    if (std::abs (rs.pitchSemitones) > 0.01f || std::abs (rs.formantSemitones) > 0.01f)
    {
        spectral::shift (out, std::max (-12.0f, std::min (12.0f, rs.pitchSemitones)),
                         std::max (-6.0f, std::min (6.0f, rs.formantSemitones)), cancel, progress);
        if (cancel && cancel->load (std::memory_order_relaxed)) return StereoBuffer{};
    }
    if (progress) progress->store (0.81f, std::memory_order_release);

    // Tone stage (offline, applies to the render only; live pass-through is untouched).
    if (rs.phone || rs.saturation > 0.001f)
    {
        const double sr = sel.sampleRate;
        const float drive = 1.0f + rs.saturation * 7.0f;
        const float norm = 1.0f / std::tanh (drive);
        for (auto* ch : { &out.l, &out.r })
        {
            Biquad hp, lp1, lp2;
            hp.highpass (sr, rs.phone ? 320.0 : 30.0);
            lp1.lowpass (sr, rs.phone ? 3400.0 : 18000.0, 0.5412);
            lp2.lowpass (sr, rs.phone ? 3400.0 : 18000.0, 1.3066);
            for (float& v : *ch)
            {
                float x = lp2.process (lp1.process (hp.process (v)));
                v = rs.saturation > 0.001f ? std::tanh (x * drive) * norm : x;
            }
        }
    }
    if (progress) progress->store (0.86f, std::memory_order_release);

    // Extend the render before time-based effects so combined delay/reverb decays are not cut.
    size_t delayTailSamples = 0, reverbTailSamples = 0;
    if (rs.delayMix > 0.001f)
    {
        const double delaySamples = std::max (1.0, std::min (1.2 * sel.sampleRate,
            divisionSamples (rs.delayDivision, rs.bpm, sel.sampleRate)));
        const double fb = std::max (0.0, std::min (0.85, (double) rs.delayFeedback));
        const double repeatsToSilence = fb > 0.001 ? std::ceil (std::log (0.001) / std::log (fb)) : 1.0;
        delayTailSamples = (size_t) std::min (6.0 * sel.sampleRate,
            delaySamples * std::max (1.0, repeatsToSilence));
    }
    if (rs.reverbMix > 0.001f)
        reverbTailSamples = (size_t) (sel.sampleRate * (2.0 + 3.0 * std::max (0.0f, std::min (1.0f, rs.reverbSize))));
    const size_t effectTailSamples = (size_t) std::min (6.0 * sel.sampleRate,
        (double) delayTailSamples + (double) reverbTailSamples);
    if (effectTailSamples > 0)
        const size_t extendedSize = out.size() + effectTailSamples;
        out.l.resize (extendedSize, 0.0f);
        out.r.resize (extendedSize, 0.0f);

    // Stereo feedback delay with cross-channel regeneration (ping-pong topology).
    if (rs.delayMix > 0.001f)
    {
        const size_t d = (size_t) std::max (1.0, std::min (1.2 * sel.sampleRate, divisionSamples (rs.delayDivision, rs.bpm, sel.sampleRate)));
        std::vector<float> dl (d, 0.0f), dr (d, 0.0f);
        size_t cursor = 0;
        const float mix = std::max (0.0f, std::min (1.0f, rs.delayMix));
        const float fb = std::max (0.0f, std::min (0.85f, rs.delayFeedback));
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (cancel && (i & 4095u) == 0 && cancel->load (std::memory_order_relaxed)) return StereoBuffer{};
            if (progress && (i & 16383u) == 0) progress->store (0.86f + 0.04f * (float) i / (float) std::max ((size_t) 1, out.size()), std::memory_order_release);
            const float dryL = out.l[i], dryR = out.r[i];
            const float echoL = dl[cursor], echoR = dr[cursor];
            dl[cursor] = dryL + echoR * fb;
            dr[cursor] = dryR + echoL * fb;
            out.l[i] = dryL * (1.0f - mix) + echoL * mix;
            out.r[i] = dryR * (1.0f - mix) + echoR * mix;
            if (++cursor == d) cursor = 0;
        }
    }

    // Compact four-comb stereo reverb. The render is bounded by the 120 s safety limit.
    if (rs.reverbMix > 0.001f)
    {
        struct Comb { std::vector<float> data; size_t pos = 0; float damp = 0.0f; };
        const double scale = std::max (0.5, std::min (1.5, 0.7 + rs.reverbSize * 0.8));
        const double ms[4] = { 29.7, 37.1, 41.1, 43.7 };
        Comb left[4], right[4];
        for (int c = 0; c < 4; ++c)
        {
            const size_t nL = (size_t) std::max (32.0, ms[c] * scale * 0.001 * sel.sampleRate);
            const size_t nR = nL + (size_t) (0.73 * c + 11.0);
            left[c].data.assign (nL, 0.0f); right[c].data.assign (nR, 0.0f);
        }
        const float mix = std::max (0.0f, std::min (1.0f, rs.reverbMix));
        const float feedback = 0.72f + 0.20f * std::max (0.0f, std::min (1.0f, rs.reverbSize));
        for (size_t i = 0; i < out.size(); ++i)
        {
            if (cancel && (i & 4095u) == 0 && cancel->load (std::memory_order_relaxed)) return StereoBuffer{};
            if (progress && (i & 16383u) == 0) progress->store (0.90f + 0.05f * (float) i / (float) std::max ((size_t) 1, out.size()), std::memory_order_release);
            const float dryL = out.l[i], dryR = out.r[i];
            float wetL = 0.0f, wetR = 0.0f;
            for (int c = 0; c < 4; ++c)
            {
                auto& a = left[c]; auto& b = right[c];
                const float yL = a.data[a.pos], yR = b.data[b.pos];
                a.damp += 0.22f * (yL - a.damp); b.damp += 0.22f * (yR - b.damp);
                a.data[a.pos] = dryL + a.damp * feedback; b.data[b.pos] = dryR + b.damp * feedback;
                if (++a.pos == a.data.size()) a.pos = 0;
                if (++b.pos == b.data.size()) b.pos = 0;
                wetL += yL * 0.25f; wetR += yR * 0.25f;
            }
            out.l[i] = dryL * (1.0f - mix) + wetL * mix;
            out.r[i] = dryR * (1.0f - mix) + wetR * mix;
        }
    }

    // Preserve headroom after cumulative delay/reverb summing; avoid hard PCM clipping.
    const double renderPeak = peakOf (out);
    if (renderPeak > 1.0 && (rs.delayMix > 0.001f || rs.reverbMix > 0.001f || rs.saturation > 0.001f || rs.phone
                            || std::abs (rs.pitchSemitones) > 0.01f || std::abs (rs.formantSemitones) > 0.01f))
    {
        const float safetyGain = (float) (0.98 / renderPeak);
        for (float& v : out.l) v *= safetyGain;
        for (float& v : out.r) v *= safetyGain;
    }

    // A short final fade prevents a hard cut if the safety tail reaches its bounded limit.
    const size_t tailFade = std::min (out.size(), (size_t) std::max (1.0, 0.010 * sel.sampleRate));
    for (size_t i = 0; i < tailFade; ++i)
    {
        const float g = (float) std::cos (0.5 * kPi * ((double) i + 0.5) / (double) tailFade);
        const size_t at = out.size() - tailFade + i;
        out.l[at] *= g;
        out.r[at] *= g;
    }

    if (progress) progress->store (0.95f, std::memory_order_release);
    if (stats) { stats->seconds = out.seconds(); stats->peak = peakOf (out); stats->slices = slices.size(); }
    if (progress) progress->store (1.0f, std::memory_order_release);
    return out;
}
} // namespace cray
