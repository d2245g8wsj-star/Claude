#pragma once

#include "Primitives.h"

namespace breakwater
{

/** The slow first stage of the Modern engine.

    A peak limiter asked for 8 dB of reduction has to take all 8 dB within its
    attack ramp and give it back within its release, and that is what makes
    heavy limiting audible. The leveller takes the steady part of that load
    beforehand: a soft knee compressor on a linked peak envelope, with a
    threshold a few dB above the ceiling and a moderate ratio, so it only
    ever brings the programme towards the ceiling and leaves the fast peaks
    to the stage after it.

    How much it takes, and how quickly, is set by the style.
*/
class Leveller
{
public:
    struct Settings
    {
        float ratio      { 1.0f };   ///< 1 is off
        float headroomDb { 3.0f };   ///< threshold above the ceiling
        float attackMs   { 10.0f };
        float releaseMs  { 250.0f };
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
        attackCoeff  = onePole (s.attackMs,  sampleRate);
        releaseCoeff = onePole (s.releaseMs, sampleRate);
    }

    void reset() noexcept
    {
        envelopeDb = -120.0f;
        reductionDb = 0.0f;
    }

    bool isActive() const noexcept { return settings.ratio > 1.001f; }

    float getReductionDb() const noexcept { return reductionDb; }

    /** Returns the gain to apply to this frame, given its linked peak magnitude
        in ceiling normalised units. */
    float process (float magnitude) noexcept
    {
        if (! isActive())
            return 1.0f;

        const auto level = gainToDb (magnitude);
        const auto coeff = level > envelopeDb ? attackCoeff : releaseCoeff;
        envelopeDb = level + coeff * (envelopeDb - level);

        // Soft knee, 6 dB wide, centred on the threshold.
        constexpr float knee = 6.0f;
        const auto over  = envelopeDb - settings.headroomDb;
        const auto slope = 1.0f - 1.0f / settings.ratio;

        if (over <= -0.5f * knee)
            reductionDb = 0.0f;
        else if (over >= 0.5f * knee)
            reductionDb = slope * over;
        else
            reductionDb = slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);

        return dbToGain (-reductionDb);
    }

private:
    double   sampleRate { 48000.0 };
    Settings settings;

    float attackCoeff  { 0.0f };
    float releaseCoeff { 0.0f };
    float envelopeDb   { -120.0f };
    float reductionDb  { 0.0f };
};

} // namespace breakwater
