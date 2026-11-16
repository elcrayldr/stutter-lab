#pragma once
#include <cmath>
#include "Common.h"

namespace cray
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void reset() noexcept { z1 = z2 = 0; }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    { b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0; }
    void lowpass (double sr, double f, double q = 0.70710678) noexcept
    {
        f = std::min (f, sr * 0.45);
        const double w = 2 * kPi * f / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
    }
    void highpass (double sr, double f, double q = 0.70710678) noexcept
    {
        f = std::min (f, sr * 0.45);
        const double w = 2 * kPi * f / sr, c = std::cos (w), a = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
    }
    float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
};
} // namespace cray
