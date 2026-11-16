// Minimal RIFF/WAVE encoder (24-bit PCM or 32-bit float, stereo). Returns bytes so the JUCE layer
// can write them with Unicode-safe file APIs.
#pragma once
#include <cstring>
#include <string>
#include "Common.h"

namespace cray
{
enum class WavFormat { Pcm24, Float32 };

inline std::vector<uint8_t> encodeWav (const StereoBuffer& b, WavFormat fmt)
{
    const uint32_t ch = 2, sr = (uint32_t) std::llround (b.sampleRate);
    const uint32_t bytes = fmt == WavFormat::Pcm24 ? 3 : 4;
    const uint32_t frames = (uint32_t) b.size();
    const uint32_t dataSize = frames * ch * bytes;
    const bool fl = fmt == WavFormat::Float32;
    const uint32_t fmtSize = 16, factSize = fl ? 12 : 0;
    std::vector<uint8_t> o;
    o.reserve (44 + factSize + dataSize);
    auto u32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back ((uint8_t) (v >> (8 * i))); };
    auto u16 = [&] (uint16_t v) { o.push_back ((uint8_t) v); o.push_back ((uint8_t) (v >> 8)); };
    auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) o.push_back ((uint8_t) t[i]); };
    tag ("RIFF"); u32 (4 + 8 + fmtSize + factSize + 8 + dataSize); tag ("WAVE");
    tag ("fmt "); u32 (fmtSize); u16 (fl ? 3 : 1); u16 ((uint16_t) ch); u32 (sr);
    u32 (sr * ch * bytes); u16 ((uint16_t) (ch * bytes)); u16 ((uint16_t) (bytes * 8));
    if (fl) { tag ("fact"); u32 (4); u32 (frames); }
    tag ("data"); u32 (dataSize);
    for (uint32_t i = 0; i < frames; ++i)
        for (const float* src : { &b.l[i], &b.r[i] })
        {
            float v = *src;
            if (! std::isfinite (v)) v = 0.0f;
            if (fl) { uint32_t u; std::memcpy (&u, &v, 4); u32 (u); }
            else
            {
                v = std::max (-1.0f, std::min (1.0f, v));
                int32_t s = (int32_t) std::lround ((double) v * 8388607.0);
                o.push_back ((uint8_t) s); o.push_back ((uint8_t) (s >> 8)); o.push_back ((uint8_t) (s >> 16));
            }
        }
    return o;
}
} // namespace cray
