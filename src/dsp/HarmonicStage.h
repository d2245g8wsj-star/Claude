#pragma once

#include "LowSplit.h"
#include "ColorStage.h"

#include <cmath>
#include <algorithm>

namespace crest
{

/** Harmonics generated from the material the clipper is already working on.

    Two decisions separate this from a saturation stage bolted on the front.

    The first is *when*. It is scaled by the same envelope that drives the
    adaptation, so it colours sustained material and leaves transients alone —
    and produces nothing at all while the clipper is idle. That keeps the
    plugin's one hard promise intact: below the ceiling the signal comes out
    exactly as it went in, whatever this control is set to. The colour is a
    property of the clipping rather than a separate effect mixed alongside it.

    The second is *where in the spectrum*. Harmonics of the low and low mid are
    heard as weight and body; harmonics of the top octaves are heard as fizz.
    The reason most saturation controls sound cheap is that they distort the
    whole spectrum. Generation here is fed from a band limited copy, so the
    series lands where it is musical and nothing is added above it.

    Even order is the octave: thickness. Odd order is the fifth: density and
    edge. The series control is the axis those two sit on, which is the axis a
    row of named transformer models is quietly travelling anyway.

    Runs before the shaper, so the ceiling is still guaranteed by construction:
    whatever this adds, the clipper is still the last word on level.
*/
class HarmonicStage
{
public:
    void prepare (double sampleRate) noexcept
    {
        body.prepare (sampleRate);
        body.setCutoff (bandLimitHz);
        offset.prepare (sampleRate);
        reset();
    }

    void reset() noexcept { body.reset(); offset.reset(); }

    /** @param newAmount  0 ... 1
        @param newSeries  -1 = even only, 0 = both, +1 = odd only */
    void setParameters (float newAmount, float newSeries) noexcept
    {
        amount = std::clamp (newAmount, 0.0f, 1.0f);
        series = std::clamp (newSeries, -1.0f, 1.0f);
    }

    /** @param density  the shaped clip envelope, 0 ... 1 */
    float process (float x, float density) noexcept
    {
        // The filter runs whether or not the result is used, so that turning
        // the control up does not start from a stale state.
        const auto low = std::clamp (body.process (x), -2.0f, 2.0f);

        const auto scale = amount * density;

        if (scale <= 0.0f)
            return x;

        const auto squared = low * low;

        const auto evenGain = 0.5f * (1.0f - series);
        const auto oddGain  = 0.5f * (1.0f + series);

        // The odd term is subtracted: that is the compressive direction, the
        // one that sounds like a stage running out of headroom rather than one
        // being asked to produce more than it has.
        // A squared term carries an offset with it. Removing it here rather
        // than downstream matters: the high pass only ever sees the generated
        // signal, which is silent while the clipper is idle, so the main path
        // keeps its phase response no matter what this control is set to.
        return x + offset.process (scale * (evenGain * evenWeight * squared
                                            - oddGain * oddWeight * squared * low));
    }

private:
    static constexpr float bandLimitHz = 4000.0f;
    static constexpr float evenWeight  = 0.22f;
    static constexpr float oddWeight   = 0.28f;

    LowSplit  body;
    DCBlocker offset;
    float     amount { 0.0f };
    float    series { 0.0f };
};

} // namespace crest
