#include "Chip.h"

#include <algorithm>
#include <cmath>

namespace afx
{
namespace
{
int sat (int v, int bits, bool& hit)
{
    const int hi = (1 << (bits - 1)) - 1, lo = -(1 << (bits - 1));
    hit = v < lo || v > hi;
    return std::clamp (v, lo, hi);
}
int sign (int v) { return (v > 0) - (v < 0); }
} // namespace

void ChipCore::reset()
{
    w.fill (0);
    x.fill (0);
    satFlag = false;
}

ChipCore::Out ChipCore::step (int u, int r)
{
    u = std::clamp (u, -128, 127);
    r = std::clamp (r, -128, 127);
    // The delay line, then the FIR on the Q2.8 weights (arithmetic shift: floor).
    for (int k = taps - 1; k > 0; --k) x[(size_t) k] = x[(size_t) k - 1];
    x[0] = u;
    int acc = 0;
    for (int k = 0; k < taps; ++k) acc += x[(size_t) k] * (w[(size_t) k] >> firShift);
    bool s1 = false, s2 = false, s3 = false;
    Out o;
    o.y = sat ((acc + (1 << 7)) >> 8, 8, s1);
    const int eInt = r - o.y;   // fits 9 bits
    o.e = sat (eInt, 8, s2);
    if (adaptEn)
    {
        const int g = muCode() * sign (eInt);
        for (int k = 0; k < taps; ++k)
        {
            bool s = false;
            w[(size_t) k] = sat (w[(size_t) k] + g * sign (x[(size_t) k]), weightBits, s);
            s3 = s3 || s;
        }
    }
    satFlag = satFlag || s1 || s2 || s3;
    return o;
}

ChipCore::Readout ChipCore::readout (int k) const
{
    Readout o;
    o.weight = w[(size_t) k] >> 6;   // the top 8 of 14 bits
    o.tap = x[(size_t) k];
    return o;
}

int toQ17 (double v)
{
    if (! std::isfinite (v)) return 0;
    // Python's round() is round-half-even; so is nearbyint in the default mode.
    const double q = std::nearbyint (std::clamp (v, -2.0, 2.0) * 128.0);
    return (int) std::clamp (q, -128.0, 127.0);
}
} // namespace afx
