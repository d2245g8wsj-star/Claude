#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace breakwater
{

/** Balance: how the low end and the rest share the limiter's reduction.

    A wideband limiter turns the whole mix down for a peak, and most peaks in
    a master are the kick and the bass. Every one of them ducks the vocal and
    the cymbals too. Balance splits the limited signal at about 200 Hz and
    re-weights the reduction between the two bands:

      * Towards Lows, the low band takes more of the reduction and the top
        less, so the mix stays bright and present under heavy limiting.
      * Towards Highs, the top takes more and the low end is left fuller.

    The split is a state variable low pass run in double precision, which
    stays accurate at oversampled rates where a direct form biquad at 200 Hz
    would not; the high band is what remains, so at the centre setting the two
    sum back to exactly the limiter's output. Away from the centre the result
    can sit a fraction above the ceiling, which the true peak stage after the
    oversampler is there to take care of.
*/
class Balance
{
public:
    void prepare (double sampleRate) noexcept
    {
        const auto g = std::tan (3.14159265358979323846 * 200.0 / sampleRate);
        const auto k = 1.4142135623730951;

        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;

        reset();
    }

    void reset() noexcept
    {
        for (auto& c : channels)
            c = { };
    }

    /** @param amount  -1 (lows take the reduction) ... +1 (highs take it) */
    void setAmount (float amount) noexcept { balance = std::clamp (amount, -1.0f, 1.0f); }

    bool isActive() const noexcept { return std::abs (balance) > 0.001f; }

    /** @param input        the signal before the limiter's gain
        @param reductionDb  the reduction the limiter applied to it */
    float process (int channel, float input, float reductionDb) noexcept
    {
        auto& c = channels[(size_t) channel];

        // Simper's trapezoidal SVF, low pass output.
        const auto v3 = (double) input - c.ic2;
        const auto v1 = a1 * c.ic1 + a2 * v3;
        const auto v2 = c.ic2 + a2 * c.ic1 + a3 * v3;
        c.ic1 = 2.0 * v1 - c.ic1;
        c.ic2 = 2.0 * v2 - c.ic2;

        const auto low  = (float) v2;
        const auto high = input - low;

        const auto lowGain  = std::pow (10.0f, -reductionDb * (1.0f - balance) * 0.05f);
        const auto highGain = std::pow (10.0f, -reductionDb * (1.0f + balance) * 0.05f);

        return low * lowGain + high * highGain;
    }

private:
    struct Channel { double ic1 { 0.0 }, ic2 { 0.0 }; };

    std::array<Channel, 2> channels { };
    double a1 { 0.0 }, a2 { 0.0 }, a3 { 0.0 };
    float balance { 0.0f };
};

} // namespace breakwater
