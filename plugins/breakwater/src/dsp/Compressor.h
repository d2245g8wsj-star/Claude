#pragma once

#include "Primitives.h"

#include <array>

namespace breakwater
{

/** The compressor in front of the limiter.

    A limiter, however good, controls peaks; it does not shape how the body of
    a mix moves. That is a compressor's job, and in a mastering chain it sits
    first, so the limiter after it is given a programme that is already held
    together.

    A feed forward design on an RMS detector, with a soft 6 dB knee and a
    release that relaxes as reduction is sustained:

      * The detector runs on a copy of the signal through a high pass filter,
        so a heavy low end does not make the whole mix duck to the kick.
      * Stereo mode links the channels on the louder side. Mid/side mode
        compresses mid and side independently, so a wide chorus can be held
        without touching the centre.
      * Threshold is relative to the ceiling, like every level in the chain.

    The reduction is applied in the log domain and smoothed by attack and
    release one poles; the release coefficient blends towards three times its
    length as the reduction deepens, which is what keeps a compressor that is
    working hard from pumping.
*/
class Compressor
{
public:
    enum class Mode { stereo = 0, midSide };

    struct Settings
    {
        float thresholdDb { -6.0f };
        float ratio       { 2.0f };
        float attackMs    { 10.0f };
        float releaseMs   { 150.0f };
        float highPassHz  { 20.0f };   ///< detector high pass; 20 is effectively off
        Mode  mode        { Mode::stereo };
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
        slowRelease  = onePole (3.0 * s.releaseMs, sampleRate);
        rmsCoeff     = onePole (5.0, sampleRate);

        // Second order Butterworth high pass on the detector, bilinear.
        const auto k  = std::tan (3.14159265358979 * std::clamp ((double) s.highPassHz, 10.0, sampleRate * 0.45) / sampleRate);
        const auto q  = 0.7071067811865476;
        const auto a0 = 1.0 + k / q + k * k;

        hp.b0 = 1.0 / a0;
        hp.b1 = -2.0 / a0;
        hp.b2 = 1.0 / a0;
        hp.a1 = 2.0 * (k * k - 1.0) / a0;
        hp.a2 = (1.0 - k / q + k * k) / a0;
    }

    void reset() noexcept
    {
        for (auto& c : channels)
            c = { };

        lastReductionDb = 0.0f;
    }

    float getReductionDb() const noexcept { return lastReductionDb; }

    /** Processes one stereo (or mono) frame in place, in ceiling normalised units. */
    void process (float* frame, int numChannels) noexcept
    {
        const auto midSide = settings.mode == Mode::midSide && numChannels == 2;

        std::array<float, 2> x { frame[0], numChannels > 1 ? frame[1] : frame[0] };

        if (midSide)
            x = { 0.5f * (x[0] + x[1]), 0.5f * (x[0] - x[1]) };

        std::array<float, 2> requiredDb { };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& c = channels[(size_t) ch];
            const auto filtered = (float) highPass (c, (double) x[(size_t) ch]);

            c.power = filtered * filtered + rmsCoeff * (c.power - filtered * filtered);

            // RMS of a sine reads 3 dB under its peak; add it back so the
            // threshold means the same thing it does on the limiter.
            const auto level = 10.0f * std::log10 (std::max (c.power * 2.0f, 1.0e-12f));
            requiredDb[(size_t) ch] = gainReduction (level);
        }

        if (! midSide && numChannels == 2)
            requiredDb[0] = requiredDb[1] = std::max (requiredDb[0], requiredDb[1]);

        float reported = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& c = channels[(size_t) ch];
            const auto target = requiredDb[(size_t) ch];

            if (target > c.reductionDb)
            {
                c.reductionDb = target + attackCoeff * (c.reductionDb - target);
            }
            else
            {
                // Deeper reduction releases more slowly: blend towards the long
                // coefficient over the first 6 dB.
                const auto depth = std::clamp (c.reductionDb / 6.0f, 0.0f, 1.0f);
                const auto coeff = releaseCoeff + depth * (slowRelease - releaseCoeff);
                c.reductionDb = target + coeff * (c.reductionDb - target);
            }

            x[(size_t) ch] *= dbToGain (-c.reductionDb);
            reported = std::max (reported, c.reductionDb);
        }

        if (midSide)
            x = { x[0] + x[1], x[0] - x[1] };

        frame[0] = x[0];
        if (numChannels > 1)
            frame[1] = x[1];

        lastReductionDb = reported;
    }

    bool isActive() const noexcept { return settings.ratio > 1.001f; }

private:
    struct Biquad { double b0 {}, b1 {}, b2 {}, a1 {}, a2 {}; };

    struct Channel
    {
        double x1 {}, x2 {}, y1 {}, y2 {};
        float  power { 0.0f };
        float  reductionDb { 0.0f };
    };

    double highPass (Channel& c, double x) const noexcept
    {
        const auto y = hp.b0 * x + hp.b1 * c.x1 + hp.b2 * c.x2 - hp.a1 * c.y1 - hp.a2 * c.y2;
        c.x2 = c.x1; c.x1 = x;
        c.y2 = c.y1; c.y1 = y;
        return y;
    }

    float gainReduction (float levelDb) const noexcept
    {
        constexpr float knee = 6.0f;
        const auto over  = levelDb - settings.thresholdDb;
        const auto slope = 1.0f - 1.0f / settings.ratio;

        if (over <= -0.5f * knee)
            return 0.0f;

        if (over >= 0.5f * knee)
            return slope * over;

        return slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
    }

    double   sampleRate { 48000.0 };
    Settings settings;
    Biquad   hp;

    float attackCoeff { 0.0f }, releaseCoeff { 0.0f }, slowRelease { 0.0f }, rmsCoeff { 0.0f };

    std::array<Channel, 2> channels;
    float lastReductionDb { 0.0f };
};

} // namespace breakwater
