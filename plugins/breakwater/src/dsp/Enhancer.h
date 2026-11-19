#pragma once

#include "Primitives.h"

#include <array>

namespace breakwater
{

/** Transients and Detail: the two stages in front of the limiter that decide
    what the limiter is given to work with.

    Transients is a differential envelope transient shaper. A fast and a slow
    peak follower track the same linked signal; their ratio rises above 1 only
    during an onset, for as long as the slow follower takes to catch up. That
    ratio, raised to a power set by the control, becomes the gain. Positive
    settings push the onsets up before the limiter, so after limiting they
    still stand out from the body around them, which is where the sense of
    punch lives in a loud master. Negative settings soften them.

    Detail is the reverse problem. A loud master loses the quiet events
    between the hits: reverb tails, ghost notes, breaths. A short RMS against
    a long RMS tells the two apart, and the gaps are lifted, by at most 6 dB,
    and never below a floor 50 dB down from the programme, so silence and
    noise stay where they are.

    Low Punch is the same shaper on the low band only, split at 150 Hz by a
    Linkwitz-Riley crossover: it pushes or softens the kick and bass attack without touching
    the snare and the cymbals, which a full band shaper cannot tell apart.

    All three are linked across channels, so the stereo image does not move.
*/
class Enhancer
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;

        fastAttack  = onePole (0.3,  sampleRate);
        slowAttack  = onePole (15.0, sampleRate);
        peakRelease = onePole (60.0, sampleRate);

        shortRms = onePole (8.0,   sampleRate);
        longRms  = onePole (400.0, sampleRate);

        transientSmoothing = onePole (0.2, sampleRate);
        detailSmoothing    = onePole (20.0, sampleRate);

        // The low band slow follower catches up within about 25 ms, so only
        // the front of a kick is shaped and its body is left alone.
        lowFastAttack = onePole (0.5,  sampleRate);
        lowSlowAttack = onePole (8.0,  sampleRate);
        lowRelease    = onePole (150.0, sampleRate);
        lowRecovery   = onePole (25.0, sampleRate);
        // Butterworth sections for a fourth order Linkwitz-Riley split at 150 Hz.
        const auto g = std::tan (3.14159265358979323846 * 150.0 / sampleRate);
        const auto k = 1.4142135623730951;
        svfA1 = 1.0 / (1.0 + g * (g + k));
        svfA2 = g * svfA1;
        svfA3 = g * svfA2;
        svfK  = k;

        reset();
    }

    void reset() noexcept
    {
        fast = slow = 0.0f;
        shortPower = longPower = 0.0f;
        transientDb = detailDb = 0.0f;
        lowFast = lowSlow = lowDb = 0.0f;

        for (auto& split : splits)
            split = { };
    }

    /** @param transients  -1...1
        @param detail      0...1
        @param lowPunch    -1...1 */
    void setAmounts (float transients, float detail, float lowPunch = 0.0f) noexcept
    {
        transientAmount = std::clamp (transients, -1.0f, 1.0f);
        detailAmount    = std::clamp (detail, 0.0f, 1.0f);
        lowPunchAmount  = std::clamp (lowPunch, -1.0f, 1.0f);
    }

    bool isActive() const noexcept
    {
        return std::abs (transientAmount) > 0.001f || detailAmount > 0.001f || std::abs (lowPunchAmount) > 0.001f
               || std::abs (transientDb) > 0.001f || detailDb > 0.001f || std::abs (lowDb) > 0.001f;
    }

    /** Processes one frame in place. */
    void process (float* frame, int numChannels) noexcept
    {
        float loudest = 0.0f, lowest = 0.0f;
        std::array<float, 2> low { }, high { };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // A fourth order Linkwitz-Riley split. Unlike a simple "low pass
            // and the remainder" split, its two bands are in phase with each
            // other at every frequency, so scaling the low band changes its
            // level rather than rotating its phase against the high band.
            auto& split = splits[(size_t) ch];
            const auto first = svf (split.first, (double) frame[ch]);
            low[(size_t) ch]  = (float) svf (split.low,  first.low).low;
            high[(size_t) ch] = (float) svf (split.high, first.high).high;

            loudest = std::max (loudest, std::abs (frame[ch]));
            lowest  = std::max (lowest,  std::abs (low[(size_t) ch]));
        }

        const auto gain = processGain (loudest);

        // Low Punch: the same differential envelope on the low band.
        // The slow follower tracks the fast one rather than the waveform, so a
        // sustained bass note, whose cycles the fast follower already smooths,
        // never reads as an onset.
        lowFast = lowest  + (lowest  > lowFast ? lowFastAttack : lowRelease) * (lowFast - lowest);
        lowSlow = lowFast + (lowFast > lowSlow ? lowSlowAttack : lowRelease) * (lowSlow - lowFast);

        float lowTarget = 0.0f;
        if (lowSlow > 1.0e-6f && lowFast > lowSlow)
            lowTarget = std::clamp (2.0f * lowPunchAmount * gainToDb (lowFast / lowSlow), -9.0f, 9.0f);

        // Onto the shaping fast, off it over 25 ms, so the shaping covers the
        // whole front of the kick and not only the first cycle.
        const auto coeff = std::abs (lowTarget) > std::abs (lowDb) ? transientSmoothing : lowRecovery;
        lowDb = lowTarget + coeff * (lowDb - lowTarget);
        // The split's bands sum to an all pass, not to the input, so the
        // input is only replaced by them while Low Punch is in use.
        if (std::abs (lowPunchAmount) > 0.001f || std::abs (lowDb) > 0.001f)
        {
            const auto lowGain = dbToGain (lowDb);

            for (int ch = 0; ch < numChannels; ++ch)
                frame[ch] = gain * (high[(size_t) ch] + lowGain * low[(size_t) ch]);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
                frame[ch] *= gain;
        }
    }

    /** Returns the full band gain for a frame whose linked magnitude is given. */
    float processGain (float magnitude) noexcept
    {
        fast = magnitude + (magnitude > fast ? fastAttack : peakRelease) * (fast - magnitude);
        slow = magnitude + (magnitude > slow ? slowAttack : peakRelease) * (slow - magnitude);

        const auto power = magnitude * magnitude;
        shortPower = power + shortRms * (shortPower - power);
        longPower  = power + longRms  * (longPower  - power);

        // Transients: 1.5 dB of shaping per dB of onset at full scale, held to ±9 dB.
        float transientTarget = 0.0f;

        if (slow > 1.0e-6f && fast > slow)
            transientTarget = std::clamp (1.5f * transientAmount * gainToDb (fast / slow), -9.0f, 9.0f);

        transientDb = transientTarget + transientSmoothing * (transientDb - transientTarget);

        // Detail: half of the gap between short and long RMS, up to 6 dB.
        float detailTarget = 0.0f;

        if (detailAmount > 0.0f && longPower > 1.0e-10f && shortPower > longPower * 1.0e-5f)
        {
            const auto gap = 10.0f * std::log10 (longPower / std::max (shortPower, 1.0e-12f));
            detailTarget = detailAmount * std::clamp (0.5f * gap, 0.0f, 6.0f);
        }

        detailDb = detailTarget + detailSmoothing * (detailDb - detailTarget);

        return dbToGain (transientDb + detailDb);
    }

    float getTransientDb() const noexcept { return transientDb; }
    float getLowPunchDb() const noexcept  { return lowDb; }
    float getDetailDb() const noexcept    { return detailDb; }

private:
    double sampleRate { 48000.0 };

    float transientAmount { 0.0f };
    float detailAmount    { 0.0f };
    float lowPunchAmount  { 0.0f };

    struct SvfState { double ic1 { 0.0 }, ic2 { 0.0 }; };
    struct SvfOut   { double low, high; };
    struct Split    { SvfState first, low, high; };

    /** Simper's trapezoidal state variable filter, low and high outputs. */
    SvfOut svf (SvfState& s, double x) const noexcept
    {
        const auto v3 = x - s.ic2;
        const auto v1 = svfA1 * s.ic1 + svfA2 * v3;
        const auto v2 = s.ic2 + svfA2 * s.ic1 + svfA3 * v3;
        s.ic1 = 2.0 * v1 - s.ic1;
        s.ic2 = 2.0 * v2 - s.ic2;
        return { v2, x - svfK * v1 - v2 };
    }

    std::array<Split, 2> splits { };
    double svfA1 { 0.0 }, svfA2 { 0.0 }, svfA3 { 0.0 }, svfK { 1.4142135623730951 };
    float lowFastAttack { 0.0f }, lowSlowAttack { 0.0f }, lowRelease { 0.0f }, lowRecovery { 0.0f };
    float lowFast { 0.0f }, lowSlow { 0.0f }, lowDb { 0.0f };

    float fastAttack { 0.0f }, slowAttack { 0.0f }, peakRelease { 0.0f };
    float shortRms { 0.0f }, longRms { 0.0f };
    float transientSmoothing { 0.0f }, detailSmoothing { 0.0f };

    float fast { 0.0f }, slow { 0.0f };
    float shortPower { 0.0f }, longPower { 0.0f };
    float transientDb { 0.0f }, detailDb { 0.0f };
};

} // namespace breakwater
