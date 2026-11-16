// CRAY LDR - STUTTER LAB  |  Core (JUCE-free, std only => unit-testable off-host)
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cray
{
constexpr double kPi = 3.14159265358979323846;

struct StereoBuffer
{
    std::vector<float> l, r;
    double sampleRate = 44100.0;
    size_t size() const noexcept { return l.size(); }
    bool empty() const noexcept { return l.empty(); }
    void resize (size_t n) { l.assign (n, 0.0f); r.assign (n, 0.0f); }
    double seconds() const noexcept { return sampleRate > 0.0 ? (double) size() / sampleRate : 0.0; }
};

// Deterministic RNG: the same seed always yields the same pattern (Preview == Render == recall).
struct Rng
{
    explicit Rng (uint32_t seed = 1u) : s (seed ? seed : 0x9E3779B9u) {}
    uint32_t next() noexcept { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uniform() noexcept { return (float) (next() >> 8) * (1.0f / 16777216.0f); }
    float bipolar() noexcept { return uniform() * 2.0f - 1.0f; }
    uint32_t s;
};

inline float peakOf (const StereoBuffer& b) noexcept
{
    float p = 0.0f;
    for (float v : b.l) p = std::max (p, std::fabs (v));
    for (float v : b.r) p = std::max (p, std::fabs (v));
    return p;
}

// Band-limited windowed-sinc resampler (anti-alias cut-off follows the ratio).
inline std::vector<float> sincResample (const std::vector<float>& x, double step, size_t outLen)
{
    std::vector<float> y (outLen, 0.0f);
    if (x.empty() || outLen == 0 || step <= 0.0) return y;
    const double cutoff = std::min (1.0, 1.0 / step) * 0.97;
    const int half = 24;
    const long n = (long) x.size();
    for (size_t i = 0; i < outLen; ++i)
    {
        const double pos = (double) i * step;
        const long centre = (long) std::floor (pos);
        const double frac = pos - (double) centre;
        double acc = 0.0, norm = 0.0;
        for (int k = -half + 1; k <= half; ++k)
        {
            const double d = (double) k - frac;
            const double a = kPi * cutoff * d;
            const double sinc = std::fabs (a) < 1e-9 ? 1.0 : std::sin (a) / a;
            const double wp = d / (double) half;
            const double w = 0.42 + 0.5 * std::cos (kPi * wp) + 0.08 * std::cos (2.0 * kPi * wp);
            const double c = sinc * w * cutoff;
            norm += c;
            const long idx = centre + k;
            if (idx >= 0 && idx < n) acc += c * (double) x[(size_t) idx];
        }
        y[i] = (float) (norm > 1e-9 ? acc / norm : acc);
    }
    return y;
}

inline StereoBuffer resampleBuffer (const StereoBuffer& in, double newRate)
{
    StereoBuffer out;
    if (in.empty() || in.sampleRate <= 0.0 || newRate <= 0.0) { out = in; return out; }
    if (std::fabs (newRate - in.sampleRate) < 0.5) { out = in; out.sampleRate = newRate; return out; }
    const double step = in.sampleRate / newRate;
    const size_t n = (size_t) std::max (1.0, std::floor ((double) in.size() / step));
    out.sampleRate = newRate;
    out.l = sincResample (in.l, step, n);
    out.r = sincResample (in.r, step, n);
    return out;
}
} // namespace cray
