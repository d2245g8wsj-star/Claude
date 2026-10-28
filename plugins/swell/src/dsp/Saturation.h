#pragma once

#include <cmath>

namespace swell
{

/** The three saturation curves, one per band.

    Every curve has a slope of exactly 1 at the origin, so a band driven gently
    is a plain linear filter and the harmonics arrive only as level rises. The
    curve is applied to the band's own signal, never to the full programme, so
    what is generated is generated from the frequencies the band is working on.
*/
enum class Curve
{
    hard,     ///< LO: a clipper with a short rounded corner. Odd series, dense.
    tube,     ///< MID: an offset tanh. Even and odd, weighted to the 2nd.
    smooth    ///< HI: algebraic, the slowest corner of the three.
};

namespace saturation
{
    /** Width of the rounded part of the hard clipper's corner, either side of
        1.0. Small enough that the curve still clips flat, large enough that the
        corner does not alias like a square one at 4x. */
    inline constexpr float hardKnee = 0.15f;

    /** Offset of the tube curve's operating point. Sets the even to odd ratio. */
    inline constexpr float tubeBias = 0.30f;

    inline float hard (float u) noexcept
    {
        const auto a = std::abs (u);

        if (a <= 1.0f - hardKnee)
            return u;

        if (a >= 1.0f + hardKnee)
            return std::copysign (1.0f, u);

        // Quadratic blend: value and slope both continuous at either end.
        const auto t = a - (1.0f - hardKnee);
        return std::copysign (a - t * t / (4.0f * hardKnee), u);
    }

    inline float tube (float u) noexcept
    {
        static const float offset = std::tanh (tubeBias);
        static const float slope  = 1.0f - offset * offset;

        return (std::tanh (u + tubeBias) - offset) / slope;
    }

    inline float smooth (float u) noexcept
    {
        return u / std::sqrt (1.0f + u * u);
    }

    inline float apply (Curve curve, float u) noexcept
    {
        switch (curve)
        {
            case Curve::hard:   return hard (u);
            case Curve::tube:   return tube (u);
            case Curve::smooth: return smooth (u);
        }

        return u;
    }
}

} // namespace swell
