#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "MatchedDesign.h"
#include "Network.h"

namespace hinge
{

/** What one channel's controls say. */
struct ChannelSettings
{
    int    lowCut      { 0 };       // 0 is out
    int    lowShelf    { 0 };
    double lowLevelDb  { 0.0 };
    double highLevelDb { 0.0 };
    int    highShelf   { 1 };
    int    highCut     { 0 };       // 0 is out
    int    outputStep  { 0 };
    bool   engaged     { true };

    double thresholdDb { -24.0 };
    double lowDynDb    { 0.0 };
    double highDynDb   { 0.0 };
};

/** Side chain level of each shelf's own band, in dB, a full scale sine at 0. */
struct BandLevels
{
    double low  { -200.0 };
    double high { -200.0 };

    static BandLevels louder (const BandLevels& x, const BandLevels& y) noexcept
    {
        return { std::max (x.low, y.low), std::max (x.high, y.high) };
    }
};

/** The two filters of a shelf: one half of the network, then the inverse of
    the same half mirrored. At the centre detent they are each other's exact
    inverse, so a flat shelf leaves the signal as it was. */
inline std::array<Coefficients, 2> designLowShelf (const MatchedDesign& design, double position, double capacitance)
{
    const auto w = network::wiper (position);
    const auto forward = design (network::lowShelfHalf (w.toStart, w.toEnd, capacitance));
    const auto mirror  = design (network::lowShelfHalf (w.toEnd, w.toStart, capacitance));

    if (mirror.isInvertible())
        return { forward, mirror.inverted() };

    auto reciprocal = network::lowShelfHalf (w.toEnd, w.toStart, capacitance);
    std::swap (reciprocal.a, reciprocal.b);
    return { forward, design (reciprocal) };
}

inline std::array<Coefficients, 2> designHighShelf (const MatchedDesign& design, double position, double capacitance)
{
    const auto w = network::wiper (position);

    auto firstOrder = [] (network::HighHalf half)
    {
        network::Analog h;
        h.b = { 1.0, half.tN, 0.0 };
        h.a = { 1.0, half.tD, 0.0 };
        return h;
    };

    const auto forward = design (firstOrder (network::highShelfHalf (w.toStart, w.toEnd, capacitance)));
    const auto mirror  = design (firstOrder (network::highShelfHalf (w.toEnd, w.toStart, capacitance)));

    if (mirror.isInvertible())
        return { forward, mirror.inverted() };

    auto reciprocal = firstOrder (network::highShelfHalf (w.toEnd, w.toStart, capacitance));
    std::swap (reciprocal.a, reciprocal.b);
    return { forward, design (reciprocal) };
}

/**
    One channel: low cut, high cut, the two shelves, the output pot.

    Audio runs per sample. Everything that moves a filter runs at a control
    rate, every few samples: the smoothing of the controls and the dynamic
    shelves. That is also the only time coefficients are designed, and only
    when something they depend on has moved.
*/
class ChannelEq
{
public:
    /** Range of the dynamic shelves: the full DYN amount arrives this far
        above the threshold. */
    static constexpr double dynamicSpanDb = 12.0;

    void prepare (double sampleRate, int controlInterval)
    {
        design.prepare (sampleRate);
        rate = sampleRate;
        interval = controlInterval;

        const auto controlRate = sampleRate / controlInterval;

        lowDb    .reset (controlRate, 0.03);
        highDb   .reset (controlRate, 0.03);
        lowCap   .reset (controlRate, 0.04);
        highCap  .reset (controlRate, 0.04);
        lowCutW  .reset (controlRate, 0.04);
        highCutW .reset (controlRate, 0.04);
        lowDyn   .reset (controlRate, 0.03);
        highDyn  .reset (controlRate, 0.03);
        threshold.reset (controlRate, 0.03);

        lowCutWet .reset (sampleRate, 0.02);
        highCutWet.reset (sampleRate, 0.02);
        output    .reset (sampleRate, 0.02);
        engaged   .reset (sampleRate, 0.02);

        // Side chain envelope: fast enough to catch a kick, slow enough not
        // to ride the waveform of a bass note.
        attack  = std::exp (-1.0 / (0.008 * sampleRate));
        release = std::exp (-1.0 / (0.160 * sampleRate));

        reset();
        designed = false;
    }

    void reset()
    {
        for (auto* f : { &lowCutFilter, &highCutFilter, &low[0], &low[1], &high[0], &high[1], &lowSide, &highSide })
            f->reset();

        lowPower = highPower = 0.0;
    }

    void setTargets (const ChannelSettings& s)
    {
        lowDb .setTargetValue (s.lowLevelDb);
        highDb.setTargetValue (s.highLevelDb);
        lowCap .setTargetValue (network::lowShelfCapacitors [(size_t) juce::jlimit (0, 7, s.lowShelf)]);
        highCap.setTargetValue (network::highShelfCapacitors[(size_t) juce::jlimit (0, 7, s.highShelf)]);

        // A cut switched out keeps its last frequency and fades out at it;
        // switched in, it starts at the new one and fades in.
        if (s.lowCut > 0)
        {
            if (lowCutWet.getTargetValue() < 0.5)
                lowCutW.setCurrentAndTargetValue (network::lowCutOmega (s.lowCut));
            else
                lowCutW.setTargetValue (network::lowCutOmega (s.lowCut));
        }

        if (s.highCut > 0)
        {
            if (highCutWet.getTargetValue() < 0.5)
                highCutW.setCurrentAndTargetValue (network::highCutOmega (s.highCut));
            else
                highCutW.setTargetValue (network::highCutOmega (s.highCut));
        }

        lowCutWet .setTargetValue (s.lowCut  > 0 ? 1.0 : 0.0);
        highCutWet.setTargetValue (s.highCut > 0 ? 1.0 : 0.0);

        lowDyn   .setTargetValue (s.lowDynDb);
        highDyn  .setTargetValue (s.highDynDb);
        threshold.setTargetValue (s.thresholdDb);

        output .setTargetValue (network::outputGain (s.outputStep));
        engaged.setTargetValue (s.engaged ? 1.0 : 0.0);
    }

    /** Jumps every control to where it is going, for the first block. */
    void snapToTargets()
    {
        for (auto* v : { &lowDb, &highDb, &lowDyn, &highDyn, &threshold, &lowCutWet, &highCutWet, &engaged })
            v->setCurrentAndTargetValue (v->getTargetValue());

        for (auto* v : { &lowCap, &highCap, &lowCutW, &highCutW, &output })
            v->setCurrentAndTargetValue (v->getTargetValue());

        designed = false;
        control (levels());
    }

    BandLevels levels() const noexcept
    {
        return { toDb (lowPower), toDb (highPower) };
    }

    /** One control step: advance the smoothing, apply the dynamics against the
        side chain levels given, and redesign whatever moved. */
    void control (const BandLevels& side)
    {
        const auto lowDbNow  = lowDb .skip (interval);
        const auto highDbNow = highDb.skip (interval);
        const auto lowC      = lowCap .skip (interval);
        const auto highC     = highCap.skip (interval);
        const auto lowCutNow = lowCutW .skip (interval);
        const auto highCutNow= highCutW.skip (interval);
        const auto lowDynNow = lowDyn  .skip (interval);
        const auto highDynNow= highDyn .skip (interval);
        const auto thresh    = threshold.skip (interval);

        // Dynamics: past the threshold, the shelf is moved by up to DYN dB,
        // the full amount dynamicSpanDb above it. Positive lifts, negative
        // tames. The wiper cannot leave the attenuator, so neither can this.
        auto dynamic = [thresh] (double level, double amountDb)
        {
            const auto over = juce::jlimit (0.0, 1.0, (level - thresh) / dynamicSpanDb);
            return amountDb * over;
        };

        const auto lowTotal  = juce::jlimit (-network::maxLevelDb, network::maxLevelDb, lowDbNow  + dynamic (side.low,  lowDynNow));
        const auto highTotal = juce::jlimit (-network::maxLevelDb, network::maxLevelDb, highDbNow + dynamic (side.high, highDynNow));

        lowMoved  = (float) (lowTotal  - lowDbNow);
        highMoved = (float) (highTotal - highDbNow);

        const auto lowPos  = network::positionForDb (lowTotal);
        const auto highPos = network::positionForDb (highTotal);

        if (! designed || differs (lowPos, lastLowPos) || differs (lowC, lastLowC))
        {
            const auto s = designLowShelf (design, lowPos, lowC);
            low[0].c = s[0];
            low[1].c = s[1];
            lastLowPos = lowPos;
            lastLowC   = lowC;
        }

        if (! designed || differs (highPos, lastHighPos) || differs (highC, lastHighC))
        {
            const auto s = designHighShelf (design, highPos, highC);
            high[0].c = s[0];
            high[1].c = s[1];
            lastHighPos = highPos;
            lastHighC   = highC;
        }

        if (! designed || differs (lowCutNow, lastLowCut))
        {
            lowCutFilter.c = design (network::highPass (lowCutNow, network::lowCutQ));
            lastLowCut = lowCutNow;
        }

        if (! designed || differs (highCutNow, lastHighCut))
        {
            highCutFilter.c = design (network::lowPass (highCutNow, network::highCutQ));
            lastHighCut = highCutNow;
        }

        // The side chain listens where each shelf works: below twice the low
        // shelf's shoulder, above the middle of the high shelf's slope.
        const auto lowSideHz  = 2.0 * network::lowShelfK  / lowC;
        const auto highSideHz = network::highShelfK / highC;

        if (! designed || differs (lowSideHz, lastLowSide) || differs (highSideHz, lastHighSide))
        {
            lowSide .c = sideFilter (lowSideHz,  false);
            highSide.c = sideFilter (highSideHz, true);
            lastLowSide  = lowSideHz;
            lastHighSide = highSideHz;
        }

        designed = true;
    }

    /** Runs the audio between two control steps, in place. */
    void process (float* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto dry = (double) data[i];
            auto x = dry;

            // The cuts are relays in the hardware: out means out. The fade
            // only covers the switch itself.
            const auto lw = lowCutWet.getNextValue();
            const auto lc = lowCutFilter.process (x);
            x += lw * (lc - x);

            const auto hw = highCutWet.getNextValue();
            const auto hc = highCutFilter.process (x);
            x += hw * (hc - x);

            follow (lowPower,  lowSide .process (x));
            follow (highPower, highSide.process (x));

            x = high[1].process (high[0].process (low[1].process (low[0].process (x))));
            x *= output.getNextValue();

            const auto wet = engaged.getNextValue();
            data[i] = (float) (dry + wet * (x - dry));
        }
    }

    /** How far the dynamics have moved each shelf at the last control step, dB. */
    float getLowMovement()  const noexcept { return lowMoved; }
    float getHighMovement() const noexcept { return highMoved; }

    const MatchedDesign& getDesign() const noexcept { return design; }

private:
    /** Exact comparison, on purpose: a setting that has stopped moving must
        stop redesigning, and one that moves at all must not be missed. */
    static bool differs (double a, double b) noexcept { return a < b || a > b; }

    static double toDb (double power) noexcept
    {
        return 10.0 * std::log10 (2.0 * power + 1.0e-20);
    }

    void follow (double& power, double x) const noexcept
    {
        const auto p = x * x;
        const auto k = p > power ? attack : release;
        power = p + k * (power - p);
    }

    /** RBJ second order side chain filter. It only has to listen, not be heard. */
    Coefficients sideFilter (double hz, bool highPass) const noexcept
    {
        const auto w     = 2.0 * juce::MathConstants<double>::pi * std::min (hz, 0.45 * rate) / rate;
        const auto alpha = std::sin (w) / (2.0 * 0.70710678118654752);
        const auto cosw  = std::cos (w);
        const auto a0    = 1.0 + alpha;

        Coefficients c;
        c.b1 = (highPass ? -(1.0 + cosw) : (1.0 - cosw)) / a0;
        c.b0 = c.b2 = (highPass ? (1.0 + cosw) : (1.0 - cosw)) * 0.5 / a0;
        c.a1 = -2.0 * cosw / a0;
        c.a2 = (1.0 - alpha) / a0;
        return c;
    }

    using Linear = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;
    using Multiplicative = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative>;

    MatchedDesign design;
    double rate { 48000.0 };
    int interval { 16 };

    Linear         lowDb, highDb, lowDyn, highDyn, threshold;
    Multiplicative lowCap { 1.0 }, highCap { 1.0 }, lowCutW { 1.0 }, highCutW { 1.0 };
    Linear         lowCutWet, highCutWet, engaged;
    Multiplicative output { 1.0 };

    Biquad lowCutFilter, highCutFilter;
    std::array<Biquad, 2> low, high;
    Biquad lowSide, highSide;

    double lowPower { 0.0 }, highPower { 0.0 };
    double attack { 0.0 }, release { 0.0 };

    bool   designed { false };
    double lastLowPos { 0.0 }, lastLowC { 0.0 }, lastHighPos { 0.0 }, lastHighC { 0.0 };
    double lastLowCut { 0.0 }, lastHighCut { 0.0 }, lastLowSide { 0.0 }, lastHighSide { 0.0 };

    float lowMoved { 0.0f }, highMoved { 0.0f };
};

} // namespace hinge
