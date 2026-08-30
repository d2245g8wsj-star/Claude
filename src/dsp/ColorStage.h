#pragma once

#include <cmath>
#include <algorithm>

namespace crest
{

/** The analogue side of a converter front end.

    Two things happen in a transformer- or opamp-coupled input stage before the
    signal ever reaches the quantiser, and both of them are level dependent:

      1. the top octave loses a little energy and a little phase coherence,
      2. the stage adds a small amount of asymmetric non-linearity, which shows
         up as a second harmonic that rises faster than the third.

    This models both with a one-pole tilt and a quadratic/cubic term. It is a
    deliberately small effect: at amount = 1 the treble softening tops out at
    about -1.5 dB at 20 kHz and the added harmonic content stays below -35 dBc
    for a -6 dBFS sine. It is meant to be the difference between "digital" and
    "not quite digital", not an effect in its own right.

    Runs inside the oversampled domain so the added harmonics do not fold back.
*/
class ColorStage
{
public:
    void prepare (double sampleRate) noexcept
    {
        // One-pole corner at 6 kHz: everything above it is treated as "top".
        const auto x = std::exp (-2.0 * 3.14159265358979323846 * 6000.0 / sampleRate);
        lowpassCoeff = static_cast<float> (1.0 - x);
        reset();
    }

    void reset() noexcept { state = 0.0f; }

    void setAmount (float newAmount) noexcept { amount = std::clamp (newAmount, 0.0f, 1.0f); }

    float process (float x) noexcept
    {
        if (amount <= 0.0f)
            return x;

        state += lowpassCoeff * (x - state);

        const auto low  = state;
        const auto high = x - state;

        // Tilt: hold the body, ease off the top.
        auto y = low + high * (1.0f - amount * 0.16f);

        // Asymmetric soft non-linearity. The squared term is clamped so that a
        // hot transient cannot make the stage blow up instead of compress.
        const auto y2 = std::clamp (y * y, 0.0f, 4.0f);

        y += amount * (0.045f * y2 - 0.070f * y2 * y);

        return y;
    }

private:
    float amount       { 0.0f };
    float lowpassCoeff { 0.5f };
    float state        { 0.0f };
};

/** First order DC blocker, 5 Hz.

    Asymmetric clipping and the colour stage both produce a DC offset by
    definition. Removing it keeps the ceiling honest, because a signal riding on
    an offset clips on one side long before the meters say it should.

    The corner sits at 5 Hz rather than the more usual 20 Hz because any high
    pass costs phase in the band above it: at 20 Hz a 100 Hz kick drum arrives
    measurably rotated. The caller switches the filter out completely when the
    settings cannot produce an offset in the first place.
*/
class DCBlocker
{
public:
    void prepare (double sampleRate) noexcept
    {
        coeff = static_cast<float> (1.0 - (2.0 * 3.14159265358979323846 * 5.0 / sampleRate));
        coeff = std::clamp (coeff, 0.9f, 0.99999f);
        reset();
    }

    void reset() noexcept { x1 = y1 = 0.0f; }

    float process (float x) noexcept
    {
        const auto y = x - x1 + coeff * y1;
        x1 = x;
        y1 = y;
        return y;
    }

private:
    float coeff { 0.999f };
    float x1    { 0.0f };
    float y1    { 0.0f };
};

} // namespace crest
