/*
    Offline checks for the claims Swell makes about itself.

    An equaliser that saturates is easy to break silently: a flat band that is
    not quite flat, a cut that stops matching its boost, a mid/side matrix that
    does not reconstruct, a bypass that shifts the signal by the oversampler's
    latency. Each is asserted here.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/HarmonicBand.h"
#include "dsp/Saturation.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <map>

namespace
{

int failures = 0;
int checks   = 0;

void check (bool condition, const juce::String& what)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::cout << "  FAIL  " << what << std::endl;
    }
}

void section (const juce::String& name)
{
    std::cout << "\n== " << name << std::endl;
}

using Settings = std::map<juce::String, float>;

/** Renders a stereo signal through a fresh instance with the given settings,
    and returns the output with the reported latency removed. */
juce::AudioBuffer<float> render (const Settings& settings,
                                 const std::function<std::pair<float, float> (int)>& source,
                                 int numSamples, double sampleRate = 48000.0, int blockSize = 512,
                                 int* latencyOut = nullptr)
{
    swell::SwellProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);

    for (const auto& [id, value] : settings)
    {
        auto* parameter = processor.apvts.getParameter (id);
        jassert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    processor.prepareToPlay (sampleRate, blockSize);

    const auto latency = processor.getLatencySamples();

    if (latencyOut != nullptr)
        *latencyOut = latency;

    const auto total = numSamples + latency;
    juce::AudioBuffer<float> output (2, numSamples);
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start < total; start += blockSize)
    {
        const auto count = juce::jmin (blockSize, total - start);
        block.setSize (2, count, false, false, true);

        for (int i = 0; i < count; ++i)
        {
            const auto [l, r] = start + i < numSamples ? source (start + i) : std::pair { 0.0f, 0.0f };
            block.setSample (0, i, l);
            block.setSample (1, i, r);
        }

        processor.processBlock (block, midi);

        for (int i = 0; i < count; ++i)
        {
            const auto at = start + i - latency;

            if (at >= 0 && at < numSamples)
                for (int ch = 0; ch < 2; ++ch)
                    output.setSample (ch, at, block.getSample (ch, i));
        }
    }

    return output;
}

std::function<std::pair<float, float> (int)> tone (double hz, float amplitude, double sampleRate = 48000.0)
{
    return [=] (int n)
    {
        const auto v = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * n / sampleRate);
        return std::pair { v, v };
    };
}

/** Amplitude of one frequency over [start, start + length). With a whole number
    of cycles in the window this is exact without a window function. */
double amplitudeAt (const juce::AudioBuffer<float>& buffer, int channel, double hz,
                    int start, int length, double sampleRate = 48000.0)
{
    double re = 0.0, im = 0.0;

    for (int i = 0; i < length; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * (start + i) / sampleRate;
        const auto x = (double) buffer.getSample (channel, start + i);
        re += x * std::cos (phase);
        im += x * std::sin (phase);
    }

    return 2.0 * std::hypot (re, im) / length;
}

double db (double gain) { return 20.0 * std::log10 (juce::jmax (gain, 1.0e-12)); }

juce::String strip (int index, const char* name) { return swell::pid::forStrip (index, name); }

// ---------------------------------------------------------------------------

void testCurves()
{
    section ("Saturation curves");

    using swell::Curve;

    for (auto curve : { Curve::hard, Curve::tube, Curve::smooth })
    {
        // Unity slope at the origin: a quiet band is a linear filter.
        const auto slope = (swell::saturation::apply (curve, 1.0e-4f) - swell::saturation::apply (curve, -1.0e-4f)) / 2.0e-4f;
        check (std::abs (slope - 1.0f) < 1.0e-3f, "unity slope at zero, curve " + juce::String ((int) curve));

        check (std::abs (swell::saturation::apply (curve, 0.0f)) < 1.0e-6f, "passes through zero");

        // Bounded, and monotonic, so no setting can fold the signal back.
        float previous = swell::saturation::apply (curve, -50.0f);

        for (float u = -50.0f; u <= 50.0f; u += 0.01f)
        {
            const auto v = swell::saturation::apply (curve, u);

            if (v < previous - 1.0e-6f)
            {
                check (false, "monotonic, curve " + juce::String ((int) curve) + " at " + juce::String (u));
                break;
            }

            previous = v;
        }

        check (std::abs (swell::saturation::apply (curve, 1000.0f)) < 3.0f, "bounded");
    }

    // The hard curve clips flat; the smooth one never quite arrives.
    check (swell::saturation::hard (1.2f) >= 1.0f, "hard curve is flat past its knee");
    check (swell::saturation::smooth (5.0f) < 1.0f, "smooth curve is asymptotic");

    // The tube curve is the only asymmetric one: it is where the even series comes from.
    check (std::abs (swell::saturation::tube (2.0f) + swell::saturation::tube (-2.0f)) > 0.05f, "tube curve is asymmetric");
    check (std::abs (swell::saturation::smooth (2.0f) + swell::saturation::smooth (-2.0f)) < 1.0e-6f, "smooth curve is symmetric");
}

void testTransparentWhenFlat()
{
    section ("Flat settings are transparent");

    const int n = 48000;
    auto out = render ({}, tone (1000.0, 0.25f), n);
    const auto in = tone (1000.0, 0.25f);

    double error = 0.0, signal = 0.0;

    for (int i = 4800; i < n; ++i)
    {
        const auto x = (double) in (i).first;
        error  += std::pow (out.getSample (0, i) - x, 2.0);
        signal += x * x;
    }

    const auto residual = 10.0 * std::log10 (error / signal);
    std::cout << "  residual against the input, latency removed: " << residual << " dB" << std::endl;
    // What is left is the oversampling filters' passband ripple, a few
    // thousandths of a dB.
    check (residual < -60.0, "flat settings null against the input below -60 dB");

    for (double hz : { 50.0, 1000.0, 10000.0, 18000.0 })
    {
        auto o = render ({}, tone (hz, 0.1f), n);
        const auto g = db (amplitudeAt (o, 0, hz, 24000, 24000) / 0.1);
        check (std::abs (g) < 0.05, "flat to 0.05 dB at " + juce::String (hz) + " Hz, got " + juce::String (g, 3));
    }
}

void testLinearResponse()
{
    section ("Quiet signals follow the linear curve");

    struct Case { const char* gain; const char* freq; swell::HarmonicBand::Shape shape; float corner; };

    const Case cases[] = {
        { swell::pid::loGain,  swell::pid::loFreq,  swell::HarmonicBand::Shape::lowShelf,  100.0f  },
        { swell::pid::midGain, swell::pid::midFreq, swell::HarmonicBand::Shape::peak,      1000.0f },
        { swell::pid::hiGain,  swell::pid::hiFreq,  swell::HarmonicBand::Shape::highShelf, 8000.0f },
    };

    double worst = 0.0;

    for (const auto& c : cases)
    {
        for (float gainDb : { -10.0f, -4.0f, 6.0f, 10.0f })
        {
            Settings settings { { strip (0, c.gain), gainDb }, { strip (0, c.freq), c.corner } };

            for (double hz : { 40.0, 100.0, 300.0, 1000.0, 3000.0, 8000.0, 15000.0 })
            {
                // -50 dBFS: far below where any curve bends.
                auto out = render (settings, tone (hz, 0.00316f), 24000);
                const auto measured = db (amplitudeAt (out, 0, hz, 12000, 12000) / 0.00316);
                const auto expected = db (swell::HarmonicBand::linearMagnitude (c.shape, hz, c.corner, gainDb));

                worst = juce::jmax (worst, std::abs (measured - expected));

                check (std::abs (measured - expected) < 0.35,
                       "band " + juce::String ((int) c.shape) + " " + juce::String (gainDb) + " dB at "
                           + juce::String (hz) + " Hz: " + juce::String (measured, 2) + " vs "
                           + juce::String (expected, 2));
            }
        }
    }

    std::cout << "  worst deviation from the analogue prototype: " << worst << " dB" << std::endl;

    // The level control reaches its stated range at the band's own extreme.
    {
        auto out = render ({ { strip (0, swell::pid::midGain), 10.0f }, { strip (0, swell::pid::midFreq), 1000.0f } },
                           tone (1000.0, 0.00316f), 24000);
        const auto g = db (amplitudeAt (out, 0, 1000.0, 12000, 12000) / 0.00316);
        check (std::abs (g - 10.0) < 0.1, "mid band reaches +10 dB at its centre, got " + juce::String (g, 2));
    }
}

void testHarmonicsFollowLevel()
{
    section ("Harmonics arrive with level");

    // LO band, hard curve: odd series. 3rd harmonic of a 60 Hz tone.
    const Settings lo { { strip (0, swell::pid::loGain), 10.0f }, { strip (0, swell::pid::loFreq), 200.0f } };

    double previous = -400.0;

    for (float levelDb : { -40.0f, -6.0f, -3.0f, 0.0f })
    {
        const auto amplitude = juce::Decibels::decibelsToGain (levelDb);
        auto out = render (lo, tone (60.0, amplitude), 48000);

        const auto fundamental = amplitudeAt (out, 0, 60.0, 24000, 24000);
        const auto third       = amplitudeAt (out, 0, 180.0, 24000, 24000);
        const auto dbc         = db (third / fundamental);

        std::cout << "  LO +10 dB, 60 Hz at " << levelDb << " dBFS: 3rd at " << dbc << " dBc" << std::endl;
        if (levelDb > -40.0f)
            check (dbc > previous + 3.0, "3rd harmonic rises with level");

        previous = dbc;

        if (levelDb <= -40.0f)
            check (dbc < -90.0, "no audible harmonics at -40 dBFS");
    }

    check (previous > -40.0, "clearly saturated at -3 dBFS");

    // MID band, tube curve: the only one that produces a 2nd harmonic.
    {
        auto out = render ({ { strip (0, swell::pid::midGain), 10.0f }, { strip (0, swell::pid::midFreq), 500.0f } },
                           tone (500.0, 0.5f), 48000);
        const auto second = db (amplitudeAt (out, 0, 1000.0, 24000, 24000) / amplitudeAt (out, 0, 500.0, 24000, 24000));
        std::cout << "  MID +10 dB, 500 Hz at -6 dBFS: 2nd at " << second << " dBc" << std::endl;
        check (second > -50.0, "mid band generates even harmonics");
    }

    {
        auto out = render ({ { strip (0, swell::pid::hiGain), 10.0f }, { strip (0, swell::pid::hiFreq), 5000.0f } },
                           tone (500.0, 0.5f), 48000);
        const auto second = db (amplitudeAt (out, 0, 1000.0, 24000, 24000) / amplitudeAt (out, 0, 500.0, 24000, 24000));
        check (second < -100.0, "hi band is symmetric: no 2nd harmonic");
    }

    // Drive follows the size of the boost or cut, not its sign: 6 dB of cut
    // leaves as much 3rd harmonic as 6 dB of boost, to within a few dB.
    {
        auto boost = render ({ { strip (0, swell::pid::loGain),  6.0f }, { strip (0, swell::pid::loFreq), 400.0f } },
                             tone (60.0, 0.9f), 48000);
        auto cut   = render ({ { strip (0, swell::pid::loGain), -6.0f }, { strip (0, swell::pid::loFreq), 400.0f } },
                             tone (60.0, 0.9f), 48000);

        const auto boostThird = db (amplitudeAt (boost, 0, 180.0, 24000, 24000));
        const auto cutThird   = db (amplitudeAt (cut,   0, 180.0, 24000, 24000));

        std::cout << "  3rd harmonic, +6 dB: " << boostThird << " dBFS, -6 dB: " << cutThird << " dBFS" << std::endl;
        check (cutThird > -80.0, "a cut also saturates");
        check (std::abs (boostThird - cutThird) < 8.0, "cut and boost of the same size saturate comparably");
    }
}

void testSwitching()
{
    section ("Switches null where they should");

    const Settings busy {
        { strip (0, swell::pid::input), 6.0f }, { strip (0, swell::pid::loGain), 8.0f },
        { strip (0, swell::pid::midGain), -5.0f }, { strip (0, swell::pid::hiGain), 7.0f },
        { strip (0, swell::pid::output), -3.0f }
    };

    auto residualAgainstInput = [] (const juce::AudioBuffer<float>& out, const std::function<std::pair<float, float> (int)>& in)
    {
        double error = 0.0, signal = 0.0;

        for (int i = 4800; i < out.getNumSamples(); ++i)
            for (int ch = 0; ch < 2; ++ch)
            {
                const auto x = ch == 0 ? in (i).first : in (i).second;
                error  += std::pow (out.getSample (ch, i) - x, 2.0);
                signal += x * x;
            }

        return 10.0 * std::log10 (error / signal + 1.0e-30);
    };

    const auto source = tone (220.0, 0.4f);

    {
        auto settings = busy;
        settings[strip (0, swell::pid::active)] = 0.0f;
        const auto r = residualAgainstInput (render (settings, source, 24000), source);
        check (r < -140.0, "strip IN off nulls against the input, got " + juce::String (r, 1) + " dB");
    }

    {
        auto settings = busy;
        settings[swell::pid::bypass] = 1.0f;
        const auto r = residualAgainstInput (render (settings, source, 24000), source);
        check (r < -140.0, "bypass nulls against the input, latency included, got " + juce::String (r, 1) + " dB");
    }

    {
        auto settings = busy;

        for (auto* in : { swell::pid::loIn, swell::pid::midIn, swell::pid::hiIn })
            settings[strip (0, in)] = 0.0f;

        // Bands out leaves the trims: +6 in, -3 out.
        auto out = render (settings, source, 24000);
        const auto g = db (amplitudeAt (out, 0, 220.0, 12000, 12000) / 0.4);
        check (std::abs (g - 3.0) < 0.05, "all bands out leaves only the trims, got " + juce::String (g, 3) + " dB");
    }

    {
        // Stereo: strip B is ignored and both channels follow A.
        auto settings = busy;
        settings[swell::pid::mode] = (float) swell::StripMode::stereo;
        settings[strip (1, swell::pid::loGain)] = -10.0f;
        settings[strip (1, swell::pid::output)] = 20.0f;

        auto out = render (settings, source, 24000);
        double difference = 0.0;

        for (int i = 0; i < out.getNumSamples(); ++i)
            difference = juce::jmax (difference, (double) std::abs (out.getSample (0, i) - out.getSample (1, i)));

        check (difference < 1.0e-6, "stereo mode: both channels follow strip A");
    }

    {
        // Dual mono: B is its own strip.
        auto settings = busy;
        settings[swell::pid::mode] = (float) swell::StripMode::dualMono;
        settings[strip (1, swell::pid::output)] = -6.0f;

        auto out = render (settings, source, 24000);
        const auto right = db (amplitudeAt (out, 1, 220.0, 12000, 12000) / 0.4);
        check (std::abs (right + 6.0) < 0.05, "dual mono: strip B applies to the right channel only");
    }

    {
        // Mid/side: a mono source has no side, so the S strip can do anything
        // and the output must not change.
        Settings plain { { swell::pid::mode, (float) swell::StripMode::midSide } };
        auto withSide = plain;
        withSide[strip (1, swell::pid::loGain)]  = 10.0f;
        withSide[strip (1, swell::pid::hiGain)]  = -10.0f;
        withSide[strip (1, swell::pid::output)]  = 12.0f;

        auto a = render (plain, source, 24000);
        auto b = render (withSide, source, 24000);

        double difference = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                difference = juce::jmax (difference, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));

        check (difference < 1.0e-5, "mid/side: the S strip does nothing to a mono source");

        const auto r = residualAgainstInput (a, source);
        check (r < -60.0, "mid/side matrix reconstructs, got " + juce::String (r, 1) + " dB");

        // And an S-only source is untouched by the M strip.
        auto antiphase = [] (int n)
        {
            const auto v = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 330.0 * n / 48000.0);
            return std::pair { v, -v };
        };

        Settings mid { { swell::pid::mode, (float) swell::StripMode::midSide },
                       { strip (0, swell::pid::midGain), 10.0f }, { strip (0, swell::pid::output), -12.0f } };

        check (residualAgainstInput (render (mid, antiphase, 24000), antiphase) < -60.0,
               "mid/side: the M strip does nothing to a side-only source");
    }
}

void testLatency()
{
    section ("Latency");

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        int latency = 0;
        auto out = render ({}, [] (int n) { return n == 1000 ? std::pair { 1.0f, 1.0f } : std::pair { 0.0f, 0.0f }; },
                           4000, rate, 256, &latency);

        int peakAt = 0;
        for (int i = 0; i < out.getNumSamples(); ++i)
            if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peakAt)))
                peakAt = i;

        std::cout << "  " << rate << " Hz: reports " << latency << " samples" << std::endl;
        check (peakAt == 1000, "impulse lands where the reported latency says at " + juce::String (rate) + " Hz");
    }
}

void testRobustness()
{
    section ("Extreme settings stay finite");

    Settings extreme;

    for (int s = 0; s < 2; ++s)
    {
        extreme[strip (s, swell::pid::input)]   = 20.0f;
        extreme[strip (s, swell::pid::loGain)]  = 10.0f;
        extreme[strip (s, swell::pid::loFreq)]  = 420.0f;
        extreme[strip (s, swell::pid::midGain)] = 10.0f;
        extreme[strip (s, swell::pid::midFreq)] = 6200.0f;
        extreme[strip (s, swell::pid::hiGain)]  = 10.0f;
        extreme[strip (s, swell::pid::hiFreq)]  = 25000.0f;
        extreme[strip (s, swell::pid::output)]  = 20.0f;
    }

    juce::Random random (1234);
    auto noise = [&random] (int) { return std::pair { random.nextFloat() * 2.0f - 1.0f, random.nextFloat() * 2.0f - 1.0f }; };

    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (int blockSize : { 1, 37, 512, 4096 })
        {
            auto out = render (extreme, noise, 8192, rate, blockSize);
            bool finite = true;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    finite = finite && std::isfinite (out.getSample (ch, i));

            check (finite, "finite at " + juce::String (rate) + " Hz, block " + juce::String (blockSize));
        }
    }
}

void testState()
{
    section ("State survives a save");

    swell::SwellProcessor a;
    a.apvts.getParameter (strip (1, swell::pid::midFreq))->setValueNotifyingHost (0.73f);
    a.apvts.getParameter (swell::pid::mode)->setValueNotifyingHost (1.0f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    swell::SwellProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    check (std::abs (b.apvts.getParameter (strip (1, swell::pid::midFreq))->getValue() - 0.73f) < 1.0e-4f, "frequency restored");
    check (b.apvts.getParameter (swell::pid::mode)->getValue() > 0.99f, "mode restored");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testCurves();
    testTransparentWhenFlat();
    testLinearResponse();
    testHarmonicsFollowLevel();
    testSwitching();
    testLatency();
    testRobustness();
    testState();

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
