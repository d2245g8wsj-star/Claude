#pragma once

#include "Primitives.h"

#include <array>

namespace zenith
{

/** The Analog engine: a limiter with no lookahead at all.

    A lookahead limiter starts turning the gain down before a transient
    arrives, which is what makes it brickwall, and also what flattens the
    front of a drum hit: the attack is gone by the time it plays. This engine
    reacts to each sample as it comes, the way a circuit does, so the leading
    edge passes with its shape and only what follows it is reduced. Whatever
    pokes through is left for the final stage, which is short and only ever
    shaves fractions of a dB.

    The sidechain is modelled on a rectifier charging a pair of capacitors:

      * Attack. The fast capacitor charges through a diode, whose conduction
        grows exponentially with the voltage across it. Small overs charge it
        gently; a big one charges it hard. That is program dependent attack
        without a single threshold or switch in it.

      * Release. The fast capacitor drains through a resistor (the release
        control). A second, larger capacitor charges slowly from the first
        and drains slowly too. A lone peak never fills the slow one, so it
        is released quickly; sustained limiting fills it, and the release
        stretches. Classic dual time constant behaviour, from two parts.

    The gain cell sits in the log domain, so the reduction in dB is the
    capacitor voltage, with a soft 2 dB knee into it.
*/
class AnalogLimiter
{
public:
    static constexpr int maxChannels = 2;

    struct Settings
    {
        float attackMs   { 0.2f };   ///< time constant of the fast charge at 6 dB over
        float releaseMs  { 60.0f };
        float link       { 1.0f };   ///< 0...1
    };

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        configure (settings);
        reset();
    }

    void configure (const Settings& s) noexcept
    {
        settings = s;

        attackRate      = (float) (1.0 - std::exp (-1000.0 / (std::max (0.005, (double) s.attackMs) * sampleRate)));
        fastRelease     = onePole (s.releaseMs, sampleRate);
        slowCharge      = onePole (std::max (60.0, 4.0 * (double) s.releaseMs), sampleRate);
        slowRelease     = onePole (std::max (400.0, 12.0 * (double) s.releaseMs), sampleRate);
    }

    void reset() noexcept
    {
        for (auto& c : channels)
            c = { };

        lastReductionDb = 0.0f;
    }

    float getReductionDb() const noexcept { return lastReductionDb; }

    void process (const float* input, float* output, int numChannels) noexcept
    {
        numChannels = std::clamp (numChannels, 1, maxChannels);

        float loudest = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            loudest = std::max (loudest, std::abs (input[ch]));

        float reported = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& c = channels[(size_t) ch];

            const auto own = std::abs (input[ch]);
            const auto d   = own + settings.link * (loudest - own);

            // Soft knee into the rectifier: 2 dB wide, centred on the ceiling.
            const auto level = gainToDb (d);
            float target;

            if (level <= -1.0f)      target = 0.0f;
            else if (level >= 1.0f)  target = level;
            else                     target = (level + 1.0f) * (level + 1.0f) * 0.25f;

            // Fast capacitor: diode charge, resistive drain.
            if (target > c.fast)
            {
                const auto across = target - c.fast;

                // Diode conduction, normalised so 6 dB across charges at the
                // nominal attack rate; small differences charge more gently.
                const auto conduction = std::min (4.0f, std::exp ((across - 6.0f) / 4.0f));
                c.fast += across * std::min (1.0f, attackRate * conduction);
            }
            else
            {
                c.fast = target + (c.fast - target) * fastRelease;
            }

            // Slow capacitor, charged from the fast one.
            if (c.fast > c.slow)
                c.slow = c.fast + (c.slow - c.fast) * slowCharge;
            else
                c.slow *= slowRelease;

            // The slow capacitor holds back no more than 85 % of the
            // reduction, so a held release still moves.
            const auto reduction = std::max (c.fast, 0.85f * c.slow);

            output[ch] = input[ch] * dbToGain (-reduction);
            reported = std::max (reported, reduction);
        }

        lastReductionDb = reported;
    }

private:
    struct Channel
    {
        float fast { 0.0f };
        float slow { 0.0f };
    };

    double   sampleRate { 48000.0 };
    Settings settings;

    float attackRate  { 1.0f };
    float fastRelease { 0.0f };
    float slowCharge  { 0.0f };
    float slowRelease { 0.0f };

    std::array<Channel, maxChannels> channels;
    float lastReductionDb { 0.0f };
};

} // namespace zenith
