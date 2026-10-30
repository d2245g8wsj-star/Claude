#pragma once

#include <algorithm>
#include <cmath>

namespace breakwater
{

/** The clip stage in front of the limiter, run at the oversampled rate.

    The fastest transients last a millisecond or less. Turning the gain down
    for them costs the material around them as well; shaving their tips costs
    only a little harmonic content in a place the ear forgives. The clipper
    takes the first few dB off each peak, and the limiter after it then has
    less to do.

    The curve is linear up to the knee and then bends towards the threshold
    as  y = k + (c - k) u / (1 + u),  which has unit slope where it joins the
    straight line, is monotonic, and never reaches the threshold. Everything
    here is in ceiling normalised units.
*/
class SoftClipper
{
public:
    /** @param amount  0 is off; 1 clips right at the ceiling */
    void setAmount (float amount) noexcept
    {
        amount = std::clamp (amount, 0.0f, 1.0f);
        active = amount > 0.001f;

        // The threshold comes down from 9 dB above the ceiling to the ceiling
        // itself, faster at first so the useful range has most of the travel.
        const auto thresholdDb = 9.0f * (1.0f - amount) * (1.0f - amount);
        threshold = std::pow (10.0f, thresholdDb / 20.0f);
        knee = threshold * 0.6f;
    }

    bool isActive() const noexcept { return active; }

    float process (float x) const noexcept
    {
        if (! active)
            return x;

        const auto magnitude = std::abs (x);

        if (magnitude <= knee)
            return x;

        const auto u = (magnitude - knee) / (threshold - knee);
        const auto y = knee + (threshold - knee) * u / (1.0f + u);

        return std::copysign (y, x);
    }

private:
    bool  active    { false };
    float threshold { 1.0f };
    float knee      { 0.6f };
};

} // namespace breakwater
