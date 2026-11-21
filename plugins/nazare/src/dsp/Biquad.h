#pragma once

/*
    The biquads of the reference model (dsp/model.py), coefficient for
    coefficient: RBJ cookbook shelves and peak, first order high and low pass.
*/

#include <cmath>

namespace nazare
{

struct Coefficients
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    Coefficients inverse() const noexcept
    {
        // the shelves are minimum phase, so swapping numerator and denominator is a stable exact inverse
        const double n = b0;
        return { 1.0 / n, a1 / n, a2 / n, b1 / n, b2 / n };
    }
};

enum class Kind { highPass1, lowPass1, highPass2, peak, lowShelf, highShelf };

inline Coefficients design (Kind kind, double f, double fs, double gainDb = 0.0, double q = 0.70710678118654752)
{
    const double pi = 3.14159265358979323846;
    const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * pi * f / fs;
    const double c = std::cos (w), s = std::sin (w), al = s / (2.0 * q);
    double b[3], a[3];

    switch (kind)
    {
        case Kind::highPass1: { const double k = std::tan (w / 2); b[0] = 1; b[1] = -1; b[2] = 0; a[0] = 1 + k; a[1] = k - 1; a[2] = 0; break; }
        case Kind::lowPass1:  { const double k = std::tan (w / 2); b[0] = k; b[1] = k;  b[2] = 0; a[0] = 1 + k; a[1] = k - 1; a[2] = 0; break; }
        case Kind::highPass2: b[0] = (1 + c) / 2; b[1] = -(1 + c); b[2] = (1 + c) / 2; a[0] = 1 + al; a[1] = -2 * c; a[2] = 1 - al; break;
        case Kind::peak:      b[0] = 1 + al * A; b[1] = -2 * c; b[2] = 1 - al * A; a[0] = 1 + al / A; a[1] = -2 * c; a[2] = 1 - al / A; break;
        case Kind::lowShelf:
        {
            const double sq = 2 * std::sqrt (A) * al;
            b[0] = A * ((A + 1) - (A - 1) * c + sq); b[1] = 2 * A * ((A - 1) - (A + 1) * c); b[2] = A * ((A + 1) - (A - 1) * c - sq);
            a[0] = (A + 1) + (A - 1) * c + sq;       a[1] = -2 * ((A - 1) + (A + 1) * c);     a[2] = (A + 1) + (A - 1) * c - sq;
            break;
        }
        case Kind::highShelf:
        default:
        {
            const double sq = 2 * std::sqrt (A) * al;
            b[0] = A * ((A + 1) + (A - 1) * c + sq); b[1] = -2 * A * ((A - 1) + (A + 1) * c); b[2] = A * ((A + 1) + (A - 1) * c - sq);
            a[0] = (A + 1) - (A - 1) * c + sq;       a[1] = 2 * ((A - 1) - (A + 1) * c);      a[2] = (A + 1) - (A - 1) * c - sq;
            break;
        }
    }

    return { b[0] / a[0], b[1] / a[0], b[2] / a[0], a[1] / a[0], a[2] / a[0] };
}

/** Transposed direct form II, which is what scipy's lfilter runs. */
struct Biquad
{
    Coefficients k;
    double z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }

    double process (double x) noexcept
    {
        const double y = k.b0 * x + z1;
        z1 = k.b1 * x - k.a1 * y + z2;
        z2 = k.b2 * x - k.a2 * y;
        return y;
    }
};

} // namespace nazare
