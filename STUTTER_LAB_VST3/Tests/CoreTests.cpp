// STUTTER LAB core tests (std only). Build: see Tests/CMakeLists.txt. Exit code 0 = all pass.
#include <cstdio>
#include <fstream>
#include <thread>
#include "../Source/Core/CaptureEngine.h"
#include "../Source/Core/PreviewEngine.h"
#include "../Source/Core/StutterEngine.h"
#include "../Source/Core/WavEncoder.h"

using namespace cray;
static int failures = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)

static StereoBuffer constant (size_t n, float v, double sr = 48000.0)
{ StereoBuffer b; b.sampleRate = sr; b.l.assign (n, v); b.r.assign (n, v); return b; }
static StereoBuffer ramp (size_t n, double sr = 48000.0)
{ StereoBuffer b; b.sampleRate = sr; b.resize (n); for (size_t i = 0; i < n; ++i) b.l[i] = b.r[i] = (float) (i + 1) / (float) n; return b; }

static void testDivisions()
{
    CHECK (std::llround (divisionSamples (1, 120, 48000)) == 24000);     // 1/4 @120
    CHECK (std::llround (divisionSamples (4, 120, 48000)) == 8000);      // 1/8T
    CHECK (std::llround (divisionSamples (6, 120, 48000)) == 9000);      // 1/16 dotted
    CHECK (std::llround (divisionSamples (10, 120, 48000)) == 1500);     // 1/64
    CHECK (std::llround (divisionSamples (2, 90, 44100)) == 14700);      // 1/8 @90
    CHECK (divisionFromLegacy (1) == 0 && divisionFromLegacy (4) == 5 && divisionFromLegacy (6) == 10);
    CHECK (divisionFromLegacy (99) == 10 && divisionFromLegacy (-5) == 0);
}

static void testFullSelection()
{
    RenderSettings rs; rs.bpm = 120; rs.stutter.division = 5;           // 1/16 = 6000 samples
    rs.stutter.repeats = 8;
    const auto sel = ramp (48000);                                      // 8 steps of 6000 cover it exactly
    RenderStats st;
    auto scan = renderStutter (sel, rs, &st);
    float mx = 0; for (float v : scan.l) mx = std::max (mx, v);
    CHECK (mx > 0.97f);                                                 // tail of the selection is reached
    CHECK (st.slices == 8);
    rs.stutter.scan = false;
    auto fixed = renderStutter (sel, rs);
    mx = 0; for (float v : fixed.l) mx = std::max (mx, v);
    CHECK (mx < 6000.0f / 48000.0f + 0.01f);                            // only the first step
}

static void testTimingAndGate()
{
    RenderSettings rs; rs.bpm = 120; rs.stutter.division = 2;           // 1/8 = 12000
    rs.stutter.repeats = 4; rs.stutter.gate = 0.5f;
    auto out = renderStutter (constant (20000, 1.0f), rs);
    CHECK (out.size() == 48000);
    for (int r = 0; r < 4; ++r)
    {
        const size_t s = (size_t) r * 12000;
        CHECK (std::fabs (out.l[s + 1000] - 1.0f) < 1e-4f);             // inside gate
        CHECK (out.l[s + 9000] == 0.0f);                                // gap after gate (6000)
    }
    CHECK (out.l[0] < 0.05f);                                           // starts from silence (fade in)
    // swing delays odd steps
    rs.stutter.swing = 1.0f; rs.stutter.gate = 1.0f;
    auto sw = renderStutter (constant (20000, 1.0f), rs);
    CHECK (sw.l[12000 + 100] < 0.5f);                                   // 2nd step has not started (delayed 4000)
    CHECK (std::fabs (sw.l[12000 + 4000 + 1000] - 1.0f) < 1e-3f);
}

static void testFadesNoClicks()
{
    RenderSettings rs; rs.bpm = 140; rs.stutter.division = 8; rs.stutter.repeats = 32; rs.stutter.fadeMs = 3;
    auto out = renderStutter (constant (30000, 0.9f), rs);
    float maxStep = 0;
    for (size_t i = 1; i < out.size(); ++i) maxStep = std::max (maxStep, std::fabs (out.l[i] - out.l[i - 1]));
    CHECK (maxStep < 0.9f * 2.0f / (0.003f * 48000.0f));                // equal-power ramp slope bound
    // PingPong joins are continuous: reversed neighbours share edge values, so no extra dips are inserted
    rs.stutter.direction = Direction::PingPong; rs.stutter.gate = 1.0f;
    auto pp = renderStutter (ramp (30000), rs);
    for (float v : pp.l) CHECK (std::isfinite (v));
    // Scanning changes source offsets, so ping-pong joins must fade rather than assume matching edge samples.
    rs.stutter.scan = true;
    auto scanned = renderStutter (ramp (30000), rs);
    float scannedMaxJump = 0.0f;
    for (size_t i = 1; i < scanned.size(); ++i)
        scannedMaxJump = std::max (scannedMaxJump, std::fabs (scanned.l[i] - scanned.l[i - 1]));
    CHECK (scannedMaxJump < 0.08f);
}

static void testDeterminismAndCancel()
{
    RenderSettings rs; rs.stutter.humanize = 0.8f; rs.stutter.direction = Direction::Random; rs.stutter.repeats = 16;
    auto a = renderStutter (ramp (20000), rs), b = renderStutter (ramp (20000), rs);
    CHECK (a.l == b.l && a.r == b.r);
    rs.stutter.seed = 7;
    auto c = renderStutter (ramp (20000), rs);
    CHECK (c.l != a.l);
    std::atomic<bool> cancel { true };
    CHECK (renderStutter (ramp (20000), rs, nullptr, &cancel).empty());
}

static void testToneStability()
{
    Rng rng (5);
    StereoBuffer noise; noise.sampleRate = 96000; noise.resize (96000 * 4);
    for (size_t i = 0; i < noise.size(); ++i) { noise.l[i] = rng.bipolar(); noise.r[i] = rng.bipolar(); }
    for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        noise.sampleRate = sr;
        RenderSettings rs; rs.stutter.repeats = 32; rs.stutter.division = 8; rs.phone = true; rs.saturation = 1.0f;
        RenderStats st;
        auto out = renderStutter (noise, rs, &st);
        bool ok = ! out.empty(); for (float v : out.l) ok = ok && std::isfinite (v) && std::fabs (v) <= 1.0001f;
        CHECK (ok);
        double mean = 0; for (float v : out.l) mean += v; mean /= (double) out.size();
        CHECK (std::fabs (mean) < 0.01);                                 // no DC build-up
    }
    // phone filter really band-limits: 100 Hz sine must lose >12 dB
    StereoBuffer sine; sine.sampleRate = 48000; sine.resize (48000);
    for (size_t i = 0; i < sine.size(); ++i) sine.l[i] = sine.r[i] = 0.5f * (float) std::sin (2 * kPi * 100.0 * (double) i / 48000.0);
    RenderSettings rs; rs.stutter.repeats = 2; rs.stutter.division = 0; rs.phone = true;
    auto o = renderStutter (sine, rs);
    float pk = 0; for (size_t i = 20000; i < 40000; ++i) pk = std::max (pk, std::fabs (o.l[i]));
    CHECK (pk < 0.5f * 0.25f);
}


static double goertzelMagnitude (const std::vector<float>& x, size_t begin, size_t length, double frequency, double sr)
{
    const double w = 2.0 * kPi * frequency / sr;
    const double coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (size_t i = begin; i < begin + length && i < x.size(); ++i)
    { const double s0 = (double) x[i] + coeff * s1 - s2; s2 = s1; s1 = s0; }
    return std::sqrt (s1 * s1 + s2 * s2 - coeff * s1 * s2);
}

static void testSpectralEffectsAndProgress()
{
    StereoBuffer sine; sine.sampleRate = 48000.0; sine.resize (48000);
    for (size_t i = 0; i < sine.size(); ++i)
        sine.l[i] = sine.r[i] = 0.25f * (float) std::sin (2.0 * kPi * 440.0 * (double) i / sine.sampleRate);
    RenderSettings rs; rs.bpm = 120.0; rs.stutter.division = 0; rs.stutter.repeats = 2; rs.stutter.scan = false;
    rs.pitchSemitones = 12.0f;
    std::atomic<float> progress { 0.0f };
    auto shifted = renderStutter (sine, rs, nullptr, nullptr, &progress);
    CHECK (! shifted.empty() && shifted.size() == 96000);
    const double at440 = goertzelMagnitude (shifted.l, 12000, 24000, 440.0, 48000.0);
    const double at880 = goertzelMagnitude (shifted.l, 12000, 24000, 880.0, 48000.0);
    CHECK (at880 > at440 * 4.0);
    CHECK (progress.load() >= 0.99f);

    rs.pitchSemitones = 0.0f; rs.formantSemitones = 5.0f;
    auto formants = renderStutter (sine, rs);
    bool finite = ! formants.empty();
    for (float v : formants.l) finite = finite && std::isfinite (v);
    CHECK (finite);

    rs.formantSemitones = 0.0f; rs.delayMix = 0.45f; rs.delayDivision = 5; rs.delayFeedback = 0.55f;
    rs.reverbMix = 0.25f; rs.reverbSize = 0.7f;
    auto ambience = renderStutter (sine, rs);
    finite = ! ambience.empty();
    for (float v : ambience.l) finite = finite && std::isfinite (v);
    CHECK (finite);
    CHECK (peakOf (ambience) > 0.0);
    CHECK (peakOf (ambience) <= 0.981); // effects summing cannot hard-clip the 24-bit export
}

static void testEffectTails()
{
    RenderSettings rs;
    rs.bpm = 120.0;
    rs.stutter.division = 2;
    rs.stutter.repeats = 4;
    rs.stutter.scan = false;
    rs.delayDivision = 5;
    rs.delayMix = 0.5f;
    rs.delayFeedback = 0.5f;
    auto delayed = renderStutter (constant (48000, 0.5f), rs);
    CHECK (delayed.size() > 48000);                                    // delay tail extends beyond the dry phrase
    bool tailAudible = false;
    for (size_t i = 48000; i < delayed.size(); ++i)
        if (std::fabs (delayed.l[i]) > 1.0e-4f) { tailAudible = true; break; }
    CHECK (tailAudible);

    rs.delayMix = 0.0f;
    rs.reverbMix = 0.3f;
    rs.reverbSize = 0.7f;
    auto reverbed = renderStutter (constant (48000, 0.5f), rs);
    CHECK (reverbed.size() > 48000);                                   // reverb is not truncated at the phrase end
    bool reverbTailAudible = false;
    for (size_t i = 48000; i < reverbed.size(); ++i)
        if (std::fabs (reverbed.l[i]) > 1.0e-4f) { reverbTailAudible = true; break; }
    CHECK (reverbTailAudible);
}

static void testWav()
{
    auto b = ramp (1000, 48000);
    for (auto f : { WavFormat::Pcm24, WavFormat::Float32 })
    {
        auto w = encodeWav (b, f);
        const size_t bytes = f == WavFormat::Pcm24 ? 3 : 4;
        CHECK (w.size() == 44 + (f == WavFormat::Float32 ? 12 : 0) + 1000 * 2 * bytes);
        CHECK (std::memcmp (w.data(), "RIFF", 4) == 0 && std::memcmp (w.data() + 8, "WAVE", 4) == 0);
        std::ofstream o (f == WavFormat::Pcm24 ? "/tmp/sl_test24.wav" : "/tmp/sl_test32f.wav", std::ios::binary);
        o.write ((const char*) w.data(), (std::streamsize) w.size());
    }
}

static void testCapture()
{
    CaptureEngine c; c.prepare (48000.0, 1.0);                            // 48000-sample capacity
    std::vector<float> in (64), in2 (64);
    for (int i = 0; i < 64; ++i) { in[i] = (float) i; in2[i] = -(float) i; }
    c.process (in.data(), in2.data(), 64);
    CHECK (c.numSamples() == 0);                                         // not recording -> nothing stored
    c.requestStart();
    for (int b = 0; b < 3; ++b) c.process (in.data(), in2.data(), 64);
    CHECK (c.isRecording() && c.numSamples() == 192);
    StereoBuffer s;
    CHECK (c.snapshotAll (s));                                           // snapshot safely stops an active take
    CHECK (! c.isRecording() && c.numSamples() == 192);
    CHECK (s.size() == 192 && s.l[65] == 1.0f && s.r[65] == -1.0f);
    // re-capture must not leak stale samples
    std::vector<float> ones (64, 1.0f);
    c.requestStart(); c.process (ones.data(), nullptr, 64); c.requestStop(); c.process (ones.data(), nullptr, 64);
    CHECK (c.snapshotAll (s) && s.size() == 64 && s.l[10] == 1.0f && s.r[10] == 1.0f);   // mono -> dual mono
    // capacity limit stops safely
    c.requestStart();
    std::vector<float> blk (1000, 0.5f);
    for (int i = 0; i < 60; ++i) c.process (blk.data(), blk.data(), 1000);
    CHECK (! c.isRecording() && c.numSamples() == 48000 && c.hitLimit());
    // sample-rate change resamples the take
    c.prepare (96000.0, 1.0);
    CHECK (std::abs (c.numSamples() - 96000) < 100 || c.numSamples() == 96000);
    CHECK (c.sampleRate() == 96000.0);
    c.clear(); CHECK (c.numSamples() == 0);
    // state recall before prepare is held until prepare
    CaptureEngine d; d.load (ramp (1000, 48000.0)); d.prepare (48000.0, 1.0);
    CHECK (d.numSamples() == 1000);
    float mn[4], mx[4]; d.peaks (0, 1000, 4, mn, mx);
    CHECK (mx[3] > mx[0]);
}

static void testPreview()
{
    PreviewEngine p; p.prepare (48000.0);
    std::vector<float> l (256, 0.25f), r (256, -0.25f);
    p.process (l.data(), r.data(), 256);                                 // never played -> bit-exact pass-through
    bool exact = true; for (int i = 0; i < 256; ++i) exact = exact && l[i] == 0.25f && r[i] == -0.25f;
    CHECK (exact);
    auto* b = new PreviewBuffer; b->audio = constant (4000, 0.8f);
    for (size_t i = 0; i < 200; ++i) { const float g = (float) i / 200.0f; b->audio.l[3999 - i] *= g; b->audio.r[3999 - i] *= g; }   // renders end faded (see StutterEngine fades)
    p.submit (b); p.play (false);
    std::vector<float> out; 
    for (int i = 0; i < 20; ++i) { std::vector<float> a (256, 0.0f), c (256, 0.0f); p.process (a.data(), c.data(), 256); out.insert (out.end(), a.begin(), a.end()); }
    CHECK (std::fabs (out[1000] - 0.8f) < 1e-5f);                        // exact buffer after the 4 ms ramp
    float maxJump = 0; for (size_t i = 1; i < out.size(); ++i) maxJump = std::max (maxJump, std::fabs (out[i] - out[i - 1]));
    CHECK (maxJump < 0.01f);                                             // no click on start, end of buffer or stop ramp
    CHECK (! p.isPlaying());
    // loop + live swap while playing keeps running
    p.play (true);
    for (int i = 0; i < 10; ++i) { std::vector<float> a (256, 0.0f); p.process (a.data(), nullptr, 256); }
    auto* b2 = new PreviewBuffer; b2->audio = constant (1000, 0.5f); b2->keepPosition = true; p.submit (b2);
    std::vector<float> a (256, 0.0f); p.process (a.data(), nullptr, 256);
    CHECK (p.lengthSamples() == 1000 && p.isPlaying());
    p.collectGarbage();

    // The preview must reproduce the generated buffer bit-for-bit after its short safety ramp.
    PreviewEngine exactPreview;
    exactPreview.prepare (48000.0);
    RenderSettings settings;
    settings.bpm = 120.0;
    settings.stutter.division = 2; // 1/8 = 12,000 samples at 120 BPM
    settings.stutter.repeats = 4;
    settings.stutter.scan = false;
    const auto rendered = renderStutter (ramp (12000), settings);
    CHECK (rendered.size() == 48000);
    auto* renderedBuffer = new PreviewBuffer;
    renderedBuffer->audio = rendered;
    exactPreview.submit (renderedBuffer);
    exactPreview.play (false);
    std::vector<float> auditionL, auditionR;
    for (int block = 0; block < 375; ++block)
    {
        std::vector<float> a (128, 0.0f), b (128, 0.0f);
        exactPreview.process (a.data(), b.data(), 128);
        auditionL.insert (auditionL.end(), a.begin(), a.end());
        auditionR.insert (auditionR.end(), b.begin(), b.end());
    }
    bool identical = auditionL.size() == rendered.size() && auditionR.size() == rendered.size();
    for (size_t i = 192; identical && i < rendered.size(); ++i)
        identical = std::fabs (auditionL[i] - rendered.l[i]) < 1.0e-6f
                 && std::fabs (auditionR[i] - rendered.r[i]) < 1.0e-6f;
    CHECK (identical);
}

static void testThreadStress()
{
    CaptureEngine cap; PreviewEngine prev; cap.prepare (48000.0, 2.0); prev.prepare (48000.0);
    std::atomic<bool> run { true }; std::atomic<long> blocks { 0 };
    std::thread audio ([&] {
        std::vector<float> l (128), r (128);
        while (run) { for (int i = 0; i < 128; ++i) l[i] = r[i] = 0.1f; cap.process (l.data(), r.data(), 128); prev.process (l.data(), r.data(), 128); ++blocks;
                      std::this_thread::sleep_for (std::chrono::microseconds (100)); }
    });
    auto t0 = std::chrono::steady_clock::now();
    int n = 0;
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds (1500))
    {
        cap.requestStart(); std::this_thread::sleep_for (std::chrono::milliseconds (7));
        std::vector<float> mn (32), mx (32); cap.peaks (0, (size_t) cap.numSamples(), 32, mn.data(), mx.data());
        cap.requestStop();  std::this_thread::sleep_for (std::chrono::milliseconds (3));
        StereoBuffer s; cap.snapshotAll (s);
        auto* b = new PreviewBuffer; b->audio = constant (2000 + (size_t) (n % 5) * 1000, 0.3f); b->keepPosition = (n & 1);
        prev.submit (b); prev.play ((n & 2) != 0); prev.collectGarbage();
        if ((n % 7) == 0) cap.clear();
        if ((n % 11) == 0) cap.load (ramp (3000));
        ++n;
    }
    run = false; audio.join();
    CHECK (blocks.load() > 100 && n > 20);
}

int main()
{
    testDivisions(); testFullSelection(); testTimingAndGate(); testFadesNoClicks(); testDeterminismAndCancel();
    testToneStability(); testSpectralEffectsAndProgress(); testEffectTails(); testWav(); testCapture(); testPreview(); testThreadStress();
    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
