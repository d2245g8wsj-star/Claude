#pragma once

#include "Primitives.h"

#include <array>

namespace zenith
{

/** The lookahead peak limiter at the heart of the Modern engine, and of the
    final true peak stage.

    It works on gain reduction in dB, one value per channel per sample, in four
    steps:

      1. Requirement.  What each sample needs to sit at or under 1.0, from the
         detector the caller provides (the sample itself, or an estimate of
         the true peak around it). Channels are linked here, by blending each
         channel's detector towards the louder one.

      2. Hold.  The largest requirement over the lookahead window. From the
         moment a peak enters the detector, the hold already asks for at least
         its reduction, and keeps asking until the peak has left the delayed
         audio path.

      3. Release.  Reduction is taken on instantly and given back with a one
         pole, whose time constant moves with the programme (below). Release
         link blends each channel towards the most reduced one.

      4. Attack.  Two running means in series smooth the edge into an S shaped
         ramp: a box convolved with a box is a triangle, the integral of which
         has no corner at either end.

    The ceiling guarantee follows from the ordering. Every value inside the
    hold window is at least the peak's requirement, the release only ever
    adds to the hold, and a mean of values that are all at least R is at least
    R. As long as both means fit inside the window the audio is delayed by,
    the gain applied to a peak cannot be less than the gain it needs. The
    proof is exact; only float rounding sits between it and the sample, and
    the processor's final clamp takes care of that.

    The release is where the character is. A fixed release either pumps on
    dense material or distorts on sparse material, so three measurements
    adjust it continuously:

      * Density: a slow average of the reduction. Sustained limiting is
        released slowly, which is what keeps a loud master from breathing.
      * Spike: how far the current reduction stands above that average. A
        lone transient is given back fast, so it does not leave a hole.
      * Bass: the share of the detector energy below about 120 Hz. Gain that
        moves within one cycle of a bass note modulates it, which is heard as
        distortion; with a lot of low end the release is held above that.
*/
class PeakLimiter
{
public:
    static constexpr int maxChannels = 2;

    struct Settings
    {
        int   lookahead      { 0 };      ///< samples; also the latency
        int   attack         { 0 };      ///< samples of ramp, at most lookahead
        float releaseMs      { 100.0f };
        float adaptive       { 0.5f };   ///< 0...1, how far density and spikes move the release
        float bassAware      { 0.5f };   ///< 0...1, how far low end holds the release
        float linkTransients { 1.0f };   ///< 0...1
        float linkRelease    { 1.0f };   ///< 0...1
    };

    /** Allocates for the longest lookahead that will be asked for, in samples
        at the fastest rate. Nothing after this allocates. */
    void prepare (double newSampleRate, int maximumLookahead)
    {
        maxLookahead = std::max (0, maximumLookahead);

        for (auto& c : channels)
        {
            c.hold.prepare (maxLookahead + 1);
            c.ramp1.prepare (maxLookahead + 2);
            c.ramp2.prepare (maxLookahead + 2);
            c.delay.prepare (maxLookahead);
        }

        configured = false;
        setSampleRate (newSampleRate);
    }

    /** Moves to another rate within what prepare() allowed for, and restarts. */
    void setSampleRate (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;

        densityCoeff = onePole (400.0, sampleRate);
        bassCoeff    = (float) std::exp (-2.0 * 3.14159265358979 * 120.0 / sampleRate);
        energyCoeff  = onePole (30.0, sampleRate);

        configured = false;
        configure (settings);
    }

    /** A lookahead change restarts the limiter, since it moves the delay. An
        attack change only re-lengthens the ramp, holding its current level.
        Everything else is free to change on any sample. */
    void configure (const Settings& s) noexcept
    {
        const auto lookahead = std::clamp (s.lookahead, 0, maxLookahead);
        const auto attack    = std::clamp (s.attack, 0, lookahead);

        const auto restart = ! configured || lookahead != settings.lookahead;
        const auto reshape = restart || attack != settings.attack;

        settings = s;
        settings.lookahead = lookahead;
        settings.attack    = attack;

        if (reshape)
        {
            // The two means span attack + 2 samples between them, which must
            // fit in the hold: (len1 - 1) + (len2 - 1) <= lookahead.
            const auto len1 = attack / 2 + 1;
            const auto len2 = attack - attack / 2 + 1;

            for (auto& c : channels)
            {
                c.hold.setWindow (lookahead + 1);
                c.ramp1.setLength (len1, ! restart);
                c.ramp2.setLength (len2, ! restart);
            }
        }

        if (restart)
        {
            for (auto& c : channels)
                c.delay.setDelay (lookahead);

            configured = true;
            reset();
        }
    }

    void reset() noexcept
    {
        for (auto& c : channels)
        {
            c.hold.reset();
            c.ramp1.reset();
            c.ramp2.reset();
            c.delay.reset();
            c.envelope = c.density = c.low1 = c.low2 = c.lowEnergy = c.energy = 0.0f;
            c.releaseCoeff = onePole (settings.releaseMs, sampleRate);
        }

        counter = 0;
        lastReductionDb = 0.0f;
    }

    int getLatency() const noexcept { return settings.lookahead; }

    /** Reduction applied to the most recent output sample, the larger channel, in dB. */
    float getReductionDb() const noexcept { return lastReductionDb; }

    /** One frame.
        @param input   audio, numChannels values
        @param detect  detector magnitudes aligned with input; 1.0 is the ceiling
        @param output  receives the input, delayed by the lookahead, with gain applied
    */
    void process (const float* input, const float* detect, float* output, int numChannels) noexcept
    {
        numChannels = std::clamp (numChannels, 1, maxChannels);

        float loudest = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            loudest = std::max (loudest, detect[ch]);

        const auto updateRelease = (counter++ & 7) == 0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& c = channels[(size_t) ch];

            // 1. Requirement, transient-linked. The blend can only raise a
            //    channel's own detector, never lower it.
            const auto d = detect[ch] + settings.linkTransients * (loudest - detect[ch]);
            const auto required = d > 1.0f ? gainToDb (d) : 0.0f;

            // 2. Hold.
            const auto held = c.hold.push (required);

            // 3. Release.
            trackProgramme (c, input[ch]);

            if (updateRelease)
                c.releaseCoeff = releaseCoefficient (c);

            if (held >= c.envelope)
                c.envelope = held;
            else
                c.envelope = held + (c.envelope - held) * c.releaseCoeff;

            c.density += (1.0f - densityCoeff) * (c.envelope - c.density);
        }

        float most = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            most = std::max (most, channels[(size_t) ch].envelope);

        float reported = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& c = channels[(size_t) ch];

            // Release link, again only ever towards more reduction.
            const auto linked = c.envelope + settings.linkRelease * (most - c.envelope);

            // 4. Attack.
            const auto smoothed = std::max (0.0f, c.ramp2.push (c.ramp1.push (linked)));

            output[ch] = c.delay.process (input[ch]) * dbToGain (-smoothed);
            reported = std::max (reported, smoothed);
        }

        lastReductionDb = reported;
    }

private:
    struct Channel
    {
        SlidingMax  hold;
        BoxAverage  ramp1, ramp2;
        SampleDelay delay;

        float envelope     { 0.0f };   ///< dB of reduction after release
        float density      { 0.0f };   ///< slow mean of envelope, dB
        float releaseCoeff { 0.0f };

        float low1 { 0.0f }, low2 { 0.0f };
        float lowEnergy { 0.0f }, energy { 0.0f };
    };

    void trackProgramme (Channel& c, float x) noexcept
    {
        if (settings.bassAware <= 0.0f)
            return;

        // Two cascaded one poles at about 120 Hz: a gentle low band, enough to
        // tell a kick and bass apart from everything above them.
        c.low1 = x      + bassCoeff * (c.low1 - x);
        c.low2 = c.low1 + bassCoeff * (c.low2 - c.low1);

        c.lowEnergy += (1.0f - energyCoeff) * (c.low2 * c.low2 - c.lowEnergy);
        c.energy    += (1.0f - energyCoeff) * (x * x - c.energy);
    }

    float releaseCoefficient (const Channel& c) const noexcept
    {
        auto ms = (double) settings.releaseMs;

        // Sustained reduction: up to four times slower at 6 dB of average.
        const auto density = std::clamp (c.density / 6.0f, 0.0f, 1.0f);

        // A spike well above the running average: up to 60 % faster.
        const auto spike = c.envelope > 0.01f
                               ? std::clamp ((c.envelope - c.density) / c.envelope, 0.0f, 1.0f)
                               : 0.0f;

        ms *= 1.0 + 3.0 * settings.adaptive * density;
        ms *= 1.0 - 0.6 * settings.adaptive * spike;

        if (settings.bassAware > 0.0f && c.energy > 1.0e-9f)
        {
            const auto lowShare = std::clamp (c.lowEnergy / c.energy, 0.0f, 1.0f);

            // Never quicker than about two cycles at 40 Hz while the low end dominates.
            ms = std::max (ms, 50.0 * settings.bassAware * lowShare);
            ms *= 1.0 + 1.5 * settings.bassAware * lowShare;
        }

        return onePole (std::max (ms, 0.05), sampleRate);
    }

    double sampleRate   { 48000.0 };
    int    maxLookahead { 0 };
    bool   configured   { false };
    Settings settings;

    std::array<Channel, maxChannels> channels;

    float densityCoeff { 0.0f };
    float bassCoeff    { 0.0f };
    float energyCoeff  { 0.0f };

    uint32_t counter         { 0 };
    float    lastReductionDb { 0.0f };
};

} // namespace zenith
