#pragma once

#include <cmath>
#include <algorithm>

namespace crest
{

/** The five clip characters.

    Every character is defined by a normalised "knee function" G with

        G(0) = 0,  G'(0) = 1,  G(u) -> 1  as u grows

    so that all of them share the same two guarantees, whatever the settings:

      * below the knee threshold the signal is passed through untouched
        (gain is exactly 1.0, not "almost 1.0"),
      * the output magnitude never exceeds 1.0, so the ceiling is a real
        ceiling and not a suggestion.

    What differs between them is how the corner is rounded, and therefore how
    the harmonic series above it is weighted.

    There is deliberately no "hard clip" character, because every one of these
    already is one: at a knee of zero the transition region has no width and
    all four collapse to the same square corner. A separate entry for it would
    be a fifth name for a setting the knee control already reaches.
*/
enum class Mode
{
    glass = 0,  ///< sine knee: short corner, bright and glassy
    edge,       ///< cubic knee: reaches the ceiling at a finite level
    silk,       ///< tanh knee: smooth, asymptotic, compressor-like
    tube,       ///< arctan knee: the slowest corner, low-order harmonics
    numModes
};

inline constexpr int numModes = static_cast<int> (Mode::numModes);

inline const char* modeName (Mode m) noexcept
{
    switch (m)
    {
        case Mode::glass: return "GLASS";
        case Mode::edge:  return "EDGE";
        case Mode::silk:  return "SILK";
        case Mode::tube:  return "TUBE";
        case Mode::numModes:
        default:          return "";
    }
}

/** Knee functions on u >= 0. See the contract described above. */
inline float kneeFunction (Mode m, float u) noexcept
{
    constexpr float pi     = 3.14159265358979323846f;
    constexpr float halfPi = pi * 0.5f;

    switch (m)
    {
        case Mode::glass:
            // sin(u) clamped at its own maximum: unity slope at zero, corner at pi/2.
            return u >= halfPi ? 1.0f : std::sin (u);

        case Mode::edge:
            // u - (4/27)u^3 meets 1.0 with zero slope exactly at u = 1.5.
            return u >= 1.5f ? 1.0f : u - (4.0f / 27.0f) * u * u * u;

        case Mode::silk:
            return std::tanh (u);

        case Mode::tube:
            return (2.0f / pi) * std::atan (halfPi * u);

        case Mode::numModes:
        default:
            return std::min (u, 1.0f);
    }
}

/** The clipper transfer function.

    Input and output are normalised so that 1.0 *is* the ceiling; the caller
    scales into and out of this domain. Positive and negative halves get their
    own threshold, which is where the asymmetry (and with it the even harmonic
    series) comes from. Both halves stay bounded by 1.0 either way.
*/
struct Shaper
{
    /** @param mode_   clip character
        @param kneeDb  how far below the ceiling the knee starts, in dB.
                       0 is a square corner.
        @param bias    -1 ... +1, moves the two thresholds in opposite directions

        The knee is set in dB rather than as a fraction of the ceiling, and the
        difference is not cosmetic. Amplitude is not how anyone hears level: a
        knee of "40 percent" measured on amplitude opens the soft region by only
        3.4 dB, and the first half of that control's travel is worth 4 dB while
        the second half is worth 10. In dB every degree of rotation buys the
        same thing, and a 12 dB knee - a genuinely gentle clipper - is an
        ordinary setting instead of an extreme one.
    */
    void setParameters (Mode mode_, float kneeDb, float bias) noexcept
    {
        mode = mode_;

        const auto t = std::clamp (std::pow (10.0f, -std::max (0.0f, kneeDb) / 20.0f),
                                   0.05f, 1.0f);
        const auto b = std::clamp (bias, -1.0f, 1.0f) * 0.45f;

        thresholdPos = std::clamp (t * (1.0f + b), 0.0f, 1.0f);
        thresholdNeg = std::clamp (t * (1.0f - b), 0.0f, 1.0f);
    }

    float process (float x) const noexcept
    {
        const auto negative = x < 0.0f;
        const auto a        = std::abs (x);
        const auto t        = negative ? thresholdNeg : thresholdPos;

        if (a <= t)
            return x;

        const auto range = 1.0f - t;

        // A fully closed knee is a plain hard clip; guard the division too.
        const auto y = range < 1.0e-5f ? 1.0f
                                       : t + range * kneeFunction (mode, (a - t) / range);

        return negative ? -y : y;
    }

    Mode  mode         { Mode::glass };
    float thresholdPos { 1.0f };
    float thresholdNeg { 1.0f };
};

} // namespace crest
