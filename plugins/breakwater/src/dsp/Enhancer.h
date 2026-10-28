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

    Low Punch is the same shaper on the low band only, split at about
    150 Hz: it pushes or softens the kick and bass attack without touching
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

        // The low band follower is slower: a bass onset takes longer to rise.
        lowFastAttack = onePole (2.0,  sampleRate);
        lowSlowAttack = onePole (30.0, sampleRate);
        lowRelease    = onePole (120.0, sampleRate);
        splitCoeff    = (float) std::exp (-2.0 * 3.14159265358979 * 150.0 / sampleRate);

        reset();
    }

    void reset() noexcept
    {
        fast = slow = 0.0f;
        shortPower = longPower = 0.0f;
        transientDb = detailDb = 0.0f;
        lowFast = lowSlow = lowDb = 0.0f;

        for (auto& band : lowBand)
            band = { };
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
        std::array<float, 2> low { };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // Two one poles: a gentle 12 dB per octave split. The high band is
            // what is left, so the two always sum back to the input.
            auto& band = lowBand[(size_t) ch];
            band.a = frame[ch] + splitCoeff * (band.a - frame[ch]);
            band.b = band.a    + splitCoeff * (band.b - band.a);

            low[(size_t) ch] = band.b;
            loudest = std::max (loudest, std::abs (frame[ch]));
            lowest  = std::max (lowest,  std::abs (band.b));
        }

        const auto gain = processGain (loudest);

        // Low Punch: the same differential envelope on the low band.
        lowFast = lowest + (lowest > lowFast ? lowFastAttack : lowRelease) * (lowFast - lowest);
        lowSlow = lowest + (lowest > lowSlow ? lowSlowAttack : lowRelease) * (lowSlow - lowest);

        float lowTarget = 0.0f;
        if (lowSlow > 1.0e-6f && lowFast > lowSlow)
            lowTarget = std::clamp (1.5f * lowPunchAmount * gainToDb (lowFast / lowSlow), -9.0f, 9.0f);

        lowDb = lowTarget + transientSmoothing * (lowDb - lowTarget);
        const auto lowExtra = dbToGain (lowDb) - 1.0f;

        for (int ch = 0; ch < numChannels; ++ch)
            frame[ch] = gain * (frame[ch] + lowExtra * low[(size_t) ch]);
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

    struct Band { float a { 0.0f }, b { 0.0f }; };
    std::array<Band, 2> lowBand { };
    float splitCoeff { 0.0f };
    float lowFastAttack { 0.0f }, lowSlowAttack { 0.0f }, lowRelease { 0.0f };
    float lowFast { 0.0f }, lowSlow { 0.0f }, lowDb { 0.0f };

    float fastAttack { 0.0f }, slowAttack { 0.0f }, peakRelease { 0.0f };
    float shortRms { 0.0f }, longRms { 0.0f };
    float transientSmoothing { 0.0f }, detailSmoothing { 0.0f };

    float fast { 0.0f }, slow { 0.0f };
    float shortPower { 0.0f }, longPower { 0.0f };
    float transientDb { 0.0f }, detailDb { 0.0f };
};

} // namespace breakwater
