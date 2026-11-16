// Rhythmic divisions. Index order is stable (used in saved state).
#pragma once
#include <algorithm>

namespace cray
{
constexpr int kNumDivisions = 11;
// length in quarter-note beats
constexpr double kDivisionBeats[kNumDivisions] = {
    2.0,        // 1/2
    1.0,        // 1/4
    0.5,        // 1/8
    0.75,       // 1/8 dotted
    1.0 / 3.0,  // 1/8 triplet
    0.25,       // 1/16
    0.375,      // 1/16 dotted
    1.0 / 6.0,  // 1/16 triplet
    0.125,      // 1/32
    1.0 / 12.0, // 1/32 triplet
    0.0625      // 1/64
};
constexpr const char* kDivisionNames[kNumDivisions] = {
    "1/2", "1/4", "1/8", "1/8 D", "1/8 T", "1/16", "1/16 D", "1/16 T", "1/32", "1/32 T", "1/64" };

// Migration of the 0.1/0.2 state (rateDivision 1..6 = 1/2,1/4,1/8,1/16,1/32,1/64).
inline int divisionFromLegacy (int legacy1to6)
{
    static const int map[6] = { 0, 1, 2, 5, 8, 10 };
    return map[std::max (1, std::min (6, legacy1to6)) - 1];
}

inline double divisionSamples (int index, double bpm, double sampleRate)
{
    const double b = std::max (20.0, std::min (400.0, bpm));
    return 60.0 / b * kDivisionBeats[std::max (0, std::min (kNumDivisions - 1, index))] * sampleRate;
}
} // namespace cray
