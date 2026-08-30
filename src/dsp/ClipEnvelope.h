#pragma once

#include <cmath>
#include <algorithm>

namespace crest
{

/** How much of the recent past the clipper has spent working.

    The audibility of clipping distortion depends far more on duration than on
    amount. Three samples taken off the top of a snare are inaudible even when
    the corner is perfectly square; fifty milliseconds taken off a held bass
    note is obvious. Every clipper on the market applies the same static curve
    to both.

    What this measures is deliberately not the length of a single overshoot. A
    sustained low note only crosses the threshold briefly on each cycle, so by
    that measure every bass peak would look like a transient. What separates
    the two cases is *repetition over a span of time*: the bass clips on every
    cycle for half a second, the snare clips once and is gone.

    So this is a slow envelope on the "is it clipping" flag. A lone burst
    barely moves it; clipping that keeps coming back drives it up and the long
    release holds it there between cycles.

        isolated transient   ->  ~0.05
        clipped bass note    ->  ~0.4
        continuously driven  ->  ->1.0

    The mapping from that to a knee is in the processor, and `shape()` below
    stretches the useful part of the range into 0...1 first.
*/
class ClipEnvelope
{
public:
    void prepare (double sampleRate) noexcept
    {
        attackCoeff  = coefficientFor (attackMs,  sampleRate);
        releaseCoeff = coefficientFor (releaseMs, sampleRate);
        reset();
    }

    void reset() noexcept { state = 0.0f; }

    float process (bool clipping) noexcept
    {
        const auto target = clipping ? 1.0f : 0.0f;

        state += (target > state ? attackCoeff : releaseCoeff) * (target - state);

        return state;
    }

    float value() const noexcept { return state; }

    /** Stretches the part of the range that actually occurs into 0...1, so the
        adaptation control has usable travel over real material rather than
        spending most of it on values nothing produces. */
    static float shape (float density) noexcept
    {
        return std::clamp (density * 2.5f, 0.0f, 1.0f);
    }

private:
    static float coefficientFor (float milliseconds, double sampleRate) noexcept
    {
        const auto samples = std::max (1.0, milliseconds * 0.001 * sampleRate);
        return static_cast<float> (1.0 - std::exp (-1.0 / samples));
    }

    // Chosen against the two cases that have to stay apart. A lone 3 ms burst
    // reaches 1 - exp(-3/100) = 0.03, so a transient is still treated as one.
    // A bass note clipping 2 ms out of every 20 ms settles near 0.24, because
    // the release bridges the gaps between its peaks and the repeats
    // accumulate. That is roughly an eight to one separation on material that
    // a single-overshoot measure could not tell apart at all.
    static constexpr float attackMs  = 100.0f;
    static constexpr float releaseMs = 300.0f;

    float attackCoeff  { 0.0f };
    float releaseCoeff { 0.0f };
    float state        { 0.0f };
};

} // namespace crest
