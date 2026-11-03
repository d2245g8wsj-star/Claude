#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "Bands.h"
#include "MatchedDesign.h"

namespace tide
{

/** What one channel's controls say. Switch positions, as on the panel. */
struct ChannelSettings
{
    int  lowFreq   { 2 };
    bool lowShelf  { true };
    int  lowGain   { bands::centreStep };

    int  midFreq   { 2 };
    int  midGain   { bands::centreStep };

    int  highFreq  { 2 };
    bool highShelf { true };
    int  highGain  { bands::centreStep };

    bool hpfIn     { false };
    bool hpfTimes5 { false };
    int  hpfFreq   { 0 };

    int  level     { bands::centreStep };
};

/**
    One channel, in the order of the hardware's block diagram:

        high band → mid band → low band → high pass → level

    Audio runs per sample in double. Everything that moves a filter runs at a
    control rate, every few samples: the smoothing of the gains and
    frequencies, and the redesign of whatever moved. A setting that has
    stopped moving costs nothing.

    The low and high bands keep a shelf and a peak side by side and crossfade
    between them, so the shelf/peak switch can be thrown under signal. A band
    at 0 dB is not in the path at all, so a flat panel is the input, bit for
    bit.
*/
class ChannelEq
{
public:
    void prepare (double sampleRate, int controlInterval)
    {
        design.prepare (sampleRate);
        interval = controlInterval;

        const auto controlRate = sampleRate / controlInterval;

        for (auto* v : { &lowDb, &midDb, &highDb })
            v->reset (controlRate, 0.03);

        for (auto* v : { &lowW, &midW, &highW, &hpfW })
            v->reset (controlRate, 0.04);

        for (auto* v : { &lowShelfMix, &highShelfMix, &hpfWet })
            v->reset (sampleRate, 0.02);

        level.reset (sampleRate, 0.02);

        reset();
        designed = false;
    }

    void reset()
    {
        for (auto* f : { &lowShelfF, &lowPeakF, &midF, &highShelfF, &highPeakF, &hpfF })
            f->reset();
    }

    void setTargets (const ChannelSettings& s)
    {
        auto at = [] (const auto& table, int index) { return table[(size_t) juce::jlimit (0, bands::numFrequencies - 1, index)]; };
        auto db = [] (int step) { return bands::gainDb[(size_t) juce::jlimit (0, bands::numGainSteps - 1, step)]; };

        lowDb .setTargetValue (db (s.lowGain));
        midDb .setTargetValue (db (s.midGain));
        highDb.setTargetValue (db (s.highGain));

        lowW .setTargetValue (bands::omega (at (bands::lowHz,  s.lowFreq)));
        midW .setTargetValue (bands::omega (at (bands::midHz,  s.midFreq)));
        highW.setTargetValue (bands::omega (at (bands::highHz, s.highFreq)));

        lowShelfMix .setTargetValue (s.lowShelf  ? 1.0 : 0.0);
        highShelfMix.setTargetValue (s.highShelf ? 1.0 : 0.0);

        // A high pass switched out keeps its last frequency and fades out at
        // it; switched in, it starts at the new one and fades in.
        const auto hpf = bands::hpfOmega (juce::jlimit (0, bands::numFrequencies - 1, s.hpfFreq), s.hpfTimes5);

        if (s.hpfIn && hpfWet.getTargetValue() < 0.5)
            hpfW.setCurrentAndTargetValue (hpf);
        else
            hpfW.setTargetValue (hpf);

        hpfWet.setTargetValue (s.hpfIn ? 1.0 : 0.0);

        level.setTargetValue (juce::Decibels::decibelsToGain (db (s.level)));
    }

    /** Jumps every control to where it is going, for the first block. */
    void snapToTargets()
    {
        for (auto* v : { &lowDb, &midDb, &highDb, &lowShelfMix, &highShelfMix, &hpfWet })
            v->setCurrentAndTargetValue (v->getTargetValue());

        for (auto* v : { &lowW, &midW, &highW, &hpfW, &level })
            v->setCurrentAndTargetValue (v->getTargetValue());

        designed = false;
        control();
    }

    /** One control step: advance the smoothing and redesign whatever moved. */
    void control()
    {
        const auto lowDbNow  = lowDb .skip (interval);
        const auto midDbNow  = midDb .skip (interval);
        const auto highDbNow = highDb.skip (interval);
        const auto lowWNow   = lowW  .skip (interval);
        const auto midWNow   = midW  .skip (interval);
        const auto highWNow  = highW .skip (interval);
        const auto hpfWNow   = hpfW  .skip (interval);

        if (! designed || differs (lowDbNow, lastLowDb) || differs (lowWNow, lastLowW))
        {
            lowActive = ! isZero (lowDbNow);
            lowShelfF.c = lowActive ? design (bands::lowShelf (lowWNow, lowDbNow)) : Coefficients {};
            lowPeakF .c = lowActive ? design (bands::peak     (lowWNow, lowDbNow)) : Coefficients {};
            lastLowDb = lowDbNow;
            lastLowW  = lowWNow;
        }

        if (! designed || differs (midDbNow, lastMidDb) || differs (midWNow, lastMidW))
        {
            midActive = ! isZero (midDbNow);
            midF.c = midActive ? design (bands::peak (midWNow, midDbNow)) : Coefficients {};
            lastMidDb = midDbNow;
            lastMidW  = midWNow;
        }

        if (! designed || differs (highDbNow, lastHighDb) || differs (highWNow, lastHighW))
        {
            highActive = ! isZero (highDbNow);
            highShelfF.c = highActive ? design (bands::highShelf (highWNow, highDbNow)) : Coefficients {};
            highPeakF .c = highActive ? design (bands::peak      (highWNow, highDbNow)) : Coefficients {};
            lastHighDb = highDbNow;
            lastHighW  = highWNow;
        }

        if (! designed || differs (hpfWNow, lastHpfW))
        {
            hpfF.c = design (bands::highPass (hpfWNow));
            lastHpfW = hpfWNow;
        }

        designed = true;
    }

    /** Runs the audio between two control steps, in place. */
    void process (double* data, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            auto x = data[i];

            const auto highMix = highShelfMix.getNextValue();
            const auto lowMix  = lowShelfMix .getNextValue();
            const auto wet     = hpfWet      .getNextValue();
            const auto gain    = level       .getNextValue();

            // Both shapes run all the time a band is in, so the state of the
            // one faded in is already right when the switch is thrown.
            if (highActive)
                x = blend (highPeakF.process (x), highShelfF.process (x), highMix);
            else
                follow (x, { &highPeakF, &highShelfF });

            if (midActive)
                x = midF.process (x);
            else
                follow (x, { &midF });

            if (lowActive)
                x = blend (lowPeakF.process (x), lowShelfF.process (x), lowMix);
            else
                follow (x, { &lowPeakF, &lowShelfF });

            // The high pass is a relay in the hardware: out means out. The
            // fade only covers the switch itself.
            if (wet > 0.0)
            {
                const auto hp = hpfF.process (x);
                x += wet * (hp - x);
            }
            else
            {
                hpfF.reset();
            }

            data[i] = x * gain;
        }
    }

    const MatchedDesign& getDesign() const noexcept { return design; }

private:
    /** Exact comparison, on purpose: a setting that has stopped moving must
        stop redesigning, and one that moves at all must not be missed. */
    static bool differs (double a, double b) noexcept { return a < b || a > b; }

    /** A band at 0 dB is out of the path, but its filters keep the history
        of a filter that passes everything, so a gain moved off the centre
        picks up where the signal is rather than from silence or from old
        state. */
    static void follow (double x, std::initializer_list<Biquad*> filters) noexcept
    {
        for (auto* f : filters)
        {
            f->x2 = f->x1;  f->x1 = x;
            f->y2 = f->y1;  f->y1 = x;
        }
    }

    /** Exactly one or the other at the ends, so a settled switch is that shape alone. */
    static double blend (double peakOut, double shelfOut, double shelfAmount) noexcept
    {
        if (shelfAmount >= 1.0) return shelfOut;
        if (shelfAmount <= 0.0) return peakOut;
        return peakOut + shelfAmount * (shelfOut - peakOut);
    }

    using Linear         = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;
    using Multiplicative = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Multiplicative>;

    MatchedDesign design;
    int interval { 16 };

    Linear         lowDb, midDb, highDb;
    Multiplicative lowW { 1.0 }, midW { 1.0 }, highW { 1.0 }, hpfW { 1.0 };
    Linear         lowShelfMix, highShelfMix, hpfWet;
    Multiplicative level { 1.0 };

    Biquad lowShelfF, lowPeakF, midF, highShelfF, highPeakF, hpfF;
    bool lowActive { false }, midActive { false }, highActive { false };

    bool   designed { false };
    double lastLowDb { 0.0 }, lastMidDb { 0.0 }, lastHighDb { 0.0 };
    double lastLowW { 0.0 }, lastMidW { 0.0 }, lastHighW { 0.0 }, lastHpfW { 0.0 };
};

} // namespace tide
