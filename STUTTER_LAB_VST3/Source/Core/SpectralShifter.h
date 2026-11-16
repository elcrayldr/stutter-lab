// Offline phase-vocoder pitch/formant shifter. The spectral envelope is estimated
// independently from the fine structure so pitch and formant movement are decoupled.
#pragma once
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include "Common.h"

namespace cray
{
namespace spectral
{
using Complex = std::complex<float>;
inline void fft (std::vector<Complex>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float angle = (inverse ? 2.0f : -2.0f) * (float) kPi / (float) len;
        const Complex root (std::cos (angle), std::sin (angle));
        for (size_t i = 0; i < n; i += len)
        {
            Complex w (1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j)
            {
                const Complex u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= root;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= (float) n;
}

inline float wrapPhase (float p)
{
    while (p > (float) kPi) p -= 2.0f * (float) kPi;
    while (p < -(float) kPi) p += 2.0f * (float) kPi;
    return p;
}

inline void shiftChannel (std::vector<float>& audio, float pitchSemitones, float formantSemitones,
                          const std::atomic<bool>* cancel, std::atomic<float>* progress, float progressBase, float progressSpan)
{
    constexpr int nfft = 2048, hop = 512, bins = nfft / 2 + 1, smoothRadius = 10;
    const float pitch = std::pow (2.0f, pitchSemitones / 12.0f);
    const float formant = std::pow (2.0f, formantSemitones / 12.0f);
    if (audio.size() < 128 || (std::abs (pitchSemitones) < 0.01f && std::abs (formantSemitones) < 0.01f)) return;

    const size_t originalSize = audio.size();
    const size_t paddedSize = originalSize + nfft;
    std::vector<float> accum (paddedSize, 0.0f), weight (paddedSize, 0.0f);
    std::vector<Complex> spectrum (nfft), outSpectrum (nfft);
    std::vector<float> previousPhase (bins, 0.0f), synthesisPhase (bins, 0.0f), magnitude (bins), envelope (bins);
    std::vector<float> window (nfft);
    for (int i = 0; i < nfft; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * (float) kPi * (float) i / (float) (nfft - 1));

    const int frames = (int) ((originalSize + hop - 1) / hop) + nfft / hop;
    for (int frame = 0; frame < frames; ++frame)
    {
        if (progress && (frame & 15) == 0) progress->store (progressBase + progressSpan * (float) frame / (float) frames, std::memory_order_release);
        if (cancel && cancel->load (std::memory_order_relaxed)) return;
        const int start = frame * hop - nfft / 2;
        for (int i = 0; i < nfft; ++i)
        {
            const int source = start + i;
            const float x = source >= 0 && (size_t) source < originalSize ? audio[(size_t) source] : 0.0f;
            spectrum[(size_t) i] = Complex (x * window[(size_t) i], 0.0f);
        }
        fft (spectrum, false);
        for (int k = 0; k < bins; ++k) magnitude[(size_t) k] = std::abs (spectrum[(size_t) k]);
        for (int k = 0; k < bins; ++k)
        {
            const int lo = std::max (0, k - smoothRadius), hi = std::min (bins - 1, k + smoothRadius);
            float sum = 0.0f; for (int j = lo; j <= hi; ++j) sum += magnitude[(size_t) j];
            envelope[(size_t) k] = std::max (1.0e-8f, sum / (float) (hi - lo + 1));
        }
        std::fill (outSpectrum.begin(), outSpectrum.end(), Complex{});
        // Keep this file JUCE-independent: use std::clamp/round for bin mapping.
        for (int k = 0; k < bins; ++k)
        {
            const int src = std::clamp ((int) std::lround ((float) k / pitch), 0, bins - 1);
            const float phase = std::arg (spectrum[(size_t) src]);
            const float expected = 2.0f * (float) kPi * (float) src * (float) hop / (float) nfft;
            float advance = expected;
            if (frame > 0) advance += wrapPhase (phase - previousPhase[(size_t) src] - expected);
            previousPhase[(size_t) src] = phase;
            if (frame == 0) synthesisPhase[(size_t) k] = phase;
            else synthesisPhase[(size_t) k] += advance * pitch;

            const int envIndex = std::clamp ((int) std::lround ((float) k / formant), 0, bins - 1);
            const float excitation = magnitude[(size_t) src] / envelope[(size_t) src];
            const float outMagnitude = excitation * envelope[(size_t) envIndex];
            outSpectrum[(size_t) k] = std::polar (outMagnitude, synthesisPhase[(size_t) k]);
            if (k > 0 && k < nfft / 2) outSpectrum[(size_t) (nfft - k)] = std::conj (outSpectrum[(size_t) k]);
        }
        fft (outSpectrum, true);
        for (int i = 0; i < nfft; ++i)
        {
            const size_t at = (size_t) (start + i + nfft / 2);
            if (at < paddedSize)
            {
                accum[at] += outSpectrum[(size_t) i].real() * window[(size_t) i];
                weight[at] += window[(size_t) i] * window[(size_t) i];
            }
        }
    }
    for (size_t i = 0; i < originalSize; ++i)
    {
        const size_t at = i + nfft / 2;
        audio[i] = weight[at] > 1.0e-8f ? accum[at] / weight[at] : 0.0f;
    }
}

inline void shift (StereoBuffer& audio, float pitchSemitones, float formantSemitones,
                   const std::atomic<bool>* cancel = nullptr, std::atomic<float>* progress = nullptr)
{
    shiftChannel (audio.l, pitchSemitones, formantSemitones, cancel, progress, 0.65f, 0.08f);
    shiftChannel (audio.r, pitchSemitones, formantSemitones, cancel, progress, 0.73f, 0.08f);
}
} // namespace spectral
} // namespace cray
