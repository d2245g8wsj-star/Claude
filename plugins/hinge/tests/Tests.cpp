/*
    Offline checks for the claims Hinge makes about itself.

    Two kinds. The first asks whether the circuit is the circuit: whether the
    network, before any sampling, does what the hardware's own documentation
    says it does. The second asks whether the plugin is that circuit: whether
    the filters at a sample rate follow it, and whether the switches, routing
    and dynamics do what the panel says.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/ChannelEq.h"
#include "dsp/MatchedDesign.h"
#include "dsp/Network.h"

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

constexpr double twoPi = juce::MathConstants<double>::twoPi;

double db (double gain) { return 20.0 * std::log10 (juce::jmax (gain, 1.0e-12)); }

juce::String strip (int index, const char* name) { return hinge::pid::forStrip (index, name); }

/** The analogue shelf at a wiper position, in dB, at one frequency. */
double analogLowShelfDb (double position, double capacitance, double hz)
{
    const auto w  = hinge::network::wiper (position);
    const auto f  = hinge::MatchedDesign::analogResponse (hinge::network::lowShelfHalf (w.toStart, w.toEnd, capacitance), twoPi * hz);
    const auto fm = hinge::MatchedDesign::analogResponse (hinge::network::lowShelfHalf (w.toEnd, w.toStart, capacitance), twoPi * hz);
    return db (std::abs (f / fm));
}

double analogHighShelfDb (double position, double capacitance, double hz)
{
    const auto w = hinge::network::wiper (position);
    const std::complex<double> s (0.0, twoPi * hz);

    auto half = [&] (double a, double b)
    {
        const auto h = hinge::network::highShelfHalf (a, b, capacitance);
        return (1.0 + s * h.tN) / (1.0 + s * h.tD);
    };

    return db (std::abs (half (w.toStart, w.toEnd) / half (w.toEnd, w.toStart)));
}

// ---------------------------------------------------------------------------

void testNetwork()
{
    section ("The network does what the hardware documentation says");

    using namespace hinge::network;

    // The attenuator: twenty one detents, half a dB apart, measured at each
    // low shelf's own legend frequency.
    double worstStep = 0.0, worstEnd = 0.0;

    for (size_t s = 0; s < lowShelfCapacitors.size(); ++s)
    {
        for (int detent = 0; detent < levelDetents; ++detent)
        {
            const auto g = analogLowShelfDb (detent, lowShelfCapacitors[s], lowShelfHz[s]);
            worstStep = std::max (worstStep, std::abs (g - dbForPosition (detent)));
        }

        worstEnd = std::max (worstEnd, std::abs (analogLowShelfDb (20, lowShelfCapacitors[s], lowShelfHz[s]) - 5.0));
    }

    std::cout << "  low shelf, worst detent against 0.5 dB steps at the legend: " << worstStep << " dB" << std::endl;
    check (worstStep < 0.2, "every low shelf detent is within 0.2 dB of its nominal half dB step at the legend");
    check (worstEnd < 0.2, "full boost is +5 dB at the low shelf legend: its bottom shoulder");

    // The low shelf keeps rising below its legend, but only by about a dB.
    for (size_t s = 0; s < lowShelfCapacitors.size(); ++s)
    {
        const auto below = analogLowShelfDb (20, lowShelfCapacitors[s], lowShelfHz[s] / 4.0);
        check (below > 5.0 && below < 6.3, "low shelf plateau just beyond +5 dB below the shoulder, got " + juce::String (below, 2));
    }

    // "The frequency noted resides in the middle of the slope; the shoulder
    // is approximately twice it, where the boost is 5 dB."
    for (size_t s = 0; s < highShelfCapacitors.size(); ++s)
    {
        const auto at     = analogHighShelfDb (20, highShelfCapacitors[s], highShelfHz[s]);
        const auto octave = analogHighShelfDb (20, highShelfCapacitors[s], 2.0 * highShelfHz[s]);

        check (at > 2.4 && at < 3.6, "high shelf legend is mid slope, got " + juce::String (at, 2) + " dB at " + juce::String (highShelfHz[s]));
        check (octave > 4.4 && octave < 5.3, "high shelf is about +5 dB an octave above its legend, got " + juce::String (octave, 2));
    }

    // Swapping the halves mirrors the curve: a cut is the boost upside down.
    double worstMirror = 0.0;

    for (double hz : { 30.0, 100.0, 300.0, 1000.0, 3000.0, 10000.0 })
        for (double pos : { 0.0, 3.0, 7.5, 14.0 })
        {
            worstMirror = std::max (worstMirror, std::abs (analogLowShelfDb (pos, 209e-9, hz) + analogLowShelfDb (20.0 - pos, 209e-9, hz)));
            worstMirror = std::max (worstMirror, std::abs (analogHighShelfDb (pos, 5.2e-9, hz) + analogHighShelfDb (20.0 - pos, 5.2e-9, hz)));
        }

    check (worstMirror < 1.0e-9, "cut and boost are exact mirror images");
    check (std::abs (analogLowShelfDb (10.0, 89e-9, 200.0)) < 1.0e-12, "centre detent is flat");

    // Cuts: second order, -3 dB at their natural frequency.
    check (std::abs (lowCutOmega (1) / twoPi - 11.52) < 0.01, "12 Hz cut sits at 11.5 Hz");
    check (std::abs (highCutOmega (7) / twoPi - 7153.0) < 2.0, "7.5 kHz cut sits at 7.15 kHz");
    check (std::abs (highCutQ - 0.7069) < 1.0e-4, "high cut is a hair under Butterworth");

    // Output pot.
    check (std::abs (outputGain (0) - 1.0) < 1.0e-12, "output centre is unity");
    check (std::abs (db (outputGain (10)) - 6.16) < 0.01 && std::abs (db (outputGain (-10)) + 6.16) < 0.01,
           "output spans +-6.2 dB");
}

// ---------------------------------------------------------------------------

void testDesign()
{
    section ("The filters follow the network");

    using namespace hinge;

    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        MatchedDesign design;
        design.prepare (rate);

        const auto top = std::min (20000.0, 0.45 * rate);
        double worstShelf = 0.0, worstCut = 0.0;

        auto compare = [&] (const std::vector<network::Analog>& analog, const std::vector<Coefficients>& digital)
        {
            double worst = 0.0;

            for (int k = 0; k <= 200; ++k)
            {
                const auto hz = 20.0 * std::pow (top / 20.0, k / 200.0);
                std::complex<double> a (1.0), d (1.0);

                for (auto& h : analog)  a *= MatchedDesign::analogResponse (h, twoPi * hz);
                for (auto& c : digital) d *= c.response (twoPi * hz / rate);

                worst = std::max (worst, std::abs (db (std::abs (d)) - db (std::abs (a))));
            }

            return worst;
        };

        for (size_t s = 0; s < 8; ++s)
        {
            for (double pos : { 0.0, 2.0, 5.5, 9.0, 13.0, 17.5, 20.0 })
            {
                const auto w = network::wiper (pos);

                {
                    const auto c = network::lowShelfCapacitors[s];
                    auto mirror  = network::lowShelfHalf (w.toEnd, w.toStart, c);
                    std::swap (mirror.a, mirror.b);
                    const auto pair = designLowShelf (design, pos, c);
                    worstShelf = std::max (worstShelf, compare ({ network::lowShelfHalf (w.toStart, w.toEnd, c), mirror },
                                                                { pair[0], pair[1] }));
                }

                {
                    const auto c = network::highShelfCapacitors[s];
                    const auto f = network::highShelfHalf (w.toStart, w.toEnd, c);
                    const auto m = network::highShelfHalf (w.toEnd, w.toStart, c);
                    network::Analog h;
                    h.b = { 1.0, f.tN + m.tD, f.tN * m.tD };
                    h.a = { 1.0, f.tD + m.tN, f.tD * m.tN };
                    const auto pair = designHighShelf (design, pos, c);
                    worstShelf = std::max (worstShelf, compare ({ h }, { pair[0], pair[1] }));
                }
            }
        }

        for (int position = 1; position < network::numPositions; ++position)
        {
            const auto lc = network::highPass (network::lowCutOmega (position), network::lowCutQ);
            const auto hc = network::lowPass (network::highCutOmega (position), network::highCutQ);
            worstCut = std::max (worstCut, compare ({ lc }, { design (lc) }));
            worstCut = std::max (worstCut, compare ({ hc }, { design (hc) }));
        }

        std::cout << "  " << rate << " Hz: shelves within " << juce::String (worstShelf, 3)
                  << " dB, cuts within " << juce::String (worstCut, 3) << " dB of the network, 20 Hz to "
                  << juce::String (top / 1000.0, 1) << " kHz" << std::endl;

        check (worstShelf < 0.2, "shelves follow the analogue network at " + juce::String (rate));
        check (worstCut < 0.25, "cuts follow the analogue network at " + juce::String (rate));
    }
}

// ---------------------------------------------------------------------------

using Settings = std::map<juce::String, float>;

juce::AudioBuffer<float> render (const Settings& settings,
                                 const std::function<std::pair<float, float> (int)>& source,
                                 int numSamples, double sampleRate = 48000.0, int blockSize = 512,
                                 const std::function<void (hinge::HingeProcessor&, int)>& automate = {})
{
    hinge::HingeProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);

    for (const auto& [id, value] : settings)
    {
        auto* parameter = processor.apvts.getParameter (id);
        jassert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    processor.prepareToPlay (sampleRate, blockSize);

    juce::AudioBuffer<float> output (2, numSamples);
    juce::AudioBuffer<float> block (2, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start < numSamples; start += blockSize)
    {
        if (automate)
            automate (processor, start);

        const auto count = juce::jmin (blockSize, numSamples - start);
        block.setSize (2, count, false, false, true);

        for (int i = 0; i < count; ++i)
        {
            const auto [l, r] = source (start + i);
            block.setSample (0, i, l);
            block.setSample (1, i, r);
        }

        processor.processBlock (block, midi);

        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, start, block, ch, 0, count);
    }

    return output;
}

std::function<std::pair<float, float> (int)> tone (double hz, float amplitude, double sampleRate = 48000.0)
{
    return [=] (int n)
    {
        const auto v = amplitude * (float) std::sin (twoPi * hz * n / sampleRate);
        return std::pair { v, v };
    };
}

/** Amplitude of one frequency over [start, start + length), Hann windowed. */
double amplitudeAt (const juce::AudioBuffer<float>& buffer, int channel, double hz,
                    int start, int length, double sampleRate = 48000.0)
{
    double re = 0.0, im = 0.0, weight = 0.0;

    for (int i = 0; i < length; ++i)
    {
        const auto w = 0.5 - 0.5 * std::cos (twoPi * i / (length - 1));
        const auto phase = twoPi * hz * (start + i) / sampleRate;
        const auto x = (double) buffer.getSample (channel, start + i);
        re += w * x * std::cos (phase);
        im += w * x * std::sin (phase);
        weight += w;
    }

    return 2.0 * std::hypot (re, im) / weight;
}

double residualDb (const juce::AudioBuffer<float>& out, const std::function<std::pair<float, float> (int)>& in, int from = 0)
{
    double error = 0.0, signal = 0.0;

    for (int i = from; i < out.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto x = ch == 0 ? in (i).first : in (i).second;
            error  += std::pow ((double) out.getSample (ch, i) - x, 2.0);
            signal += (double) x * x;
        }

    return 10.0 * std::log10 (error / signal + 1.0e-30);
}

// ---------------------------------------------------------------------------

void testPluginResponse()
{
    section ("The plugin measures like the network");

    struct Case { int lowShelf; float lowDb; int highShelf; float highDb; };

    for (const auto& c : { Case { 0, 5.0f, 0, 5.0f }, Case { 3, -3.5f, 2, 2.0f }, Case { 7, 2.5f, 7, -5.0f } })
    {
        const Settings s { { strip (0, hinge::pid::lowShelf), (float) c.lowShelf }, { strip (0, hinge::pid::lowLevel), c.lowDb },
                           { strip (0, hinge::pid::highShelf), (float) c.highShelf }, { strip (0, hinge::pid::highLevel), c.highDb } };

        double worst = 0.0;

        for (double hz : { 40.0, 120.0, 400.0, 1000.0, 3000.0, 8000.0, 15000.0 })
        {
            auto out = render (s, tone (hz, 0.25f), 24000);
            const auto measured = db (amplitudeAt (out, 0, hz, 8000, 16000) / 0.25);
            const auto expected = analogLowShelfDb (hinge::network::positionForDb (c.lowDb),
                                                    hinge::network::lowShelfCapacitors[(size_t) c.lowShelf], hz)
                                + analogHighShelfDb (hinge::network::positionForDb (c.highDb),
                                                     hinge::network::highShelfCapacitors[(size_t) c.highShelf], hz);
            worst = std::max (worst, std::abs (measured - expected));
        }

        std::cout << "  measured against the network, 40 Hz to 15 kHz: within " << juce::String (worst, 3) << " dB" << std::endl;
        check (worst < 0.2, "measured response within 0.2 dB of the network, got " + juce::String (worst, 3));
    }

    {
        // The cuts at their natural frequency: Butterworth, -3 dB.
        auto out = render ({ { strip (0, hinge::pid::lowCut), 1.0f } }, tone (11.52, 0.25f), 96000);
        const auto g = db (amplitudeAt (out, 0, 11.52, 48000, 48000) / 0.25);
        check (std::abs (g + 3.01) < 0.1, "12 Hz cut is -3 dB at 11.5 Hz, got " + juce::String (g, 2));

        out = render ({ { strip (0, hinge::pid::highCut), 7.0f } }, tone (7153.0, 0.25f), 24000);
        const auto h = db (amplitudeAt (out, 0, 7153.0, 8000, 16000) / 0.25);
        check (std::abs (h + 3.0) < 0.15, "7.5 kHz cut is -3 dB at 7.15 kHz, got " + juce::String (h, 2));
    }

    {
        auto out = render ({ { strip (0, hinge::pid::output), 10.0f } }, tone (1000.0, 0.25f), 12000);
        const auto g = db (amplitudeAt (out, 0, 1000.0, 2000, 10000) / 0.25);
        check (std::abs (g - 6.16) < 0.02, "output at full is +6.16 dB, got " + juce::String (g, 3));
    }
}

void testTransparency()
{
    section ("What should do nothing does nothing");

    const auto source = tone (220.0, 0.4f);

    {
        // Default panel: cuts out, levels at the centre detent.
        const auto r = residualDb (render ({}, source, 24000), source);
        std::cout << "  flat panel residual: " << r << " dB" << std::endl;
        check (r < -120.0, "a flat panel nulls against the input");
    }

    const Settings busy {
        { strip (0, hinge::pid::lowCut), 3.0f }, { strip (0, hinge::pid::lowLevel), 4.0f },
        { strip (0, hinge::pid::highLevel), -3.0f }, { strip (0, hinge::pid::highCut), 5.0f },
        { strip (0, hinge::pid::output), 4.0f }, { strip (0, hinge::pid::lowDyn), -3.0f }
    };

    {
        auto s = busy;
        s[strip (0, hinge::pid::engage)] = 0.0f;
        check (residualDb (render (s, source, 24000), source) < -140.0, "ENGAGE out nulls against the input");
    }

    {
        auto s = busy;
        s[hinge::pid::bypass] = 1.0f;
        check (residualDb (render (s, source, 24000), source) < -140.0, "bypass nulls against the input");
    }

    {
        // Dynamics at zero: the side chain runs but moves nothing.
        auto with = busy;
        with[strip (0, hinge::pid::lowDyn)] = 0.0f;
        auto without = with;
        with[strip (0, hinge::pid::threshold)] = -48.0f;

        auto a = render (with, source, 24000);
        auto b = render (without, source, 24000);
        double difference = 0.0;

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                difference = std::max (difference, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));

        check (! (difference > 0.0), "DYN at zero is bit identical whatever the threshold");
    }

    {
        // Impulse in, impulse out: no latency to report or compensate.
        auto out = render ({ { strip (0, hinge::pid::highLevel), 2.0f } },
                           [] (int n) { return n == 1000 ? std::pair { 1.0f, 1.0f } : std::pair { 0.0f, 0.0f }; }, 4000);
        check (std::abs (out.getSample (0, 999)) < 1.0e-9f && std::abs (out.getSample (0, 1000)) > 0.5f, "zero latency");
    }
}

void testRouting()
{
    section ("Link and mid/side");

    const auto source = tone (220.0, 0.4f);

    auto maxDifference = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int cha, int chb)
    {
        double d = 0.0;
        for (int i = 0; i < a.getNumSamples(); ++i)
            d = std::max (d, (double) std::abs (a.getSample (cha, i) - b.getSample (chb, i)));
        return d;
    };

    {
        Settings s { { strip (0, hinge::pid::lowLevel), 3.0f }, { strip (1, hinge::pid::lowLevel), -5.0f },
                     { strip (1, hinge::pid::output), 10.0f } };

        auto out = render (s, source, 24000);
        check (maxDifference (out, out, 0, 1) < 1.0e-7, "linked: both channels follow row A");

        s[hinge::pid::link] = 0.0f;
        out = render (s, source, 24000);
        const auto left  = db (amplitudeAt (out, 0, 220.0, 8000, 16000) / 0.4);
        const auto right = db (amplitudeAt (out, 1, 220.0, 8000, 16000) / 0.4);
        check (left > 0.5 && right < 4.0 && right > left, "unlinked: row B drives the right channel on its own");
    }

    {
        Settings plain { { hinge::pid::midSide, 1.0f }, { hinge::pid::link, 0.0f } };
        auto withSide = plain;
        withSide[strip (1, hinge::pid::lowLevel)]  = 5.0f;
        withSide[strip (1, hinge::pid::highLevel)] = -5.0f;
        withSide[strip (1, hinge::pid::output)]    = 10.0f;

        auto a = render (plain, source, 24000);
        auto b = render (withSide, source, 24000);

        check (std::max (maxDifference (a, b, 0, 0), maxDifference (a, b, 1, 1)) < 1.0e-6,
               "mid/side: the S row does nothing to a mono source");
        check (residualDb (a, source) < -120.0, "mid/side matrix reconstructs");

        auto antiphase = [] (int n)
        {
            const auto v = 0.3f * (float) std::sin (twoPi * 330.0 * n / 48000.0);
            return std::pair { v, -v };
        };

        Settings mid { { hinge::pid::midSide, 1.0f }, { hinge::pid::link, 0.0f },
                       { strip (0, hinge::pid::lowLevel), 5.0f }, { strip (0, hinge::pid::output), -10.0f } };

        check (residualDb (render (mid, antiphase, 24000), antiphase) < -120.0,
               "mid/side: the M row does nothing to a side only source");
    }
}

void testDynamics()
{
    section ("Dynamic shelves");

    // +5 dB of low shelf that a loud bass takes back, all of it.
    const Settings tame { { strip (0, hinge::pid::lowShelf), 0.0f }, { strip (0, hinge::pid::lowLevel), 5.0f },
                          { strip (0, hinge::pid::lowDyn), -5.0f }, { strip (0, hinge::pid::threshold), -30.0f } };

    {
        auto out = render (tame, tone (50.0, 0.5f), 48000);
        const auto g = db (amplitudeAt (out, 0, 50.0, 24000, 24000) / 0.5);
        std::cout << "  loud 50 Hz through +5 dB, DYN -5: " << juce::String (g, 2) << " dB" << std::endl;
        check (std::abs (g) < 0.5, "a loud bass pulls a +5 dB low shelf back to flat");
    }

    {
        auto out = render (tame, tone (50.0, 0.003f), 48000);
        const auto g = db (amplitudeAt (out, 0, 50.0, 24000, 24000) / 0.003);
        check (g > 5.0, "a quiet bass keeps the boost, got " + juce::String (g, 2));
    }

    {
        // The low side chain does not hear the treble.
        auto out = render (tame, tone (6000.0, 0.5f), 24000);
        auto ref = render ({ { strip (0, hinge::pid::lowShelf), 0.0f }, { strip (0, hinge::pid::lowLevel), 5.0f } },
                           tone (6000.0, 0.5f), 24000);
        const auto g = db (amplitudeAt (out, 0, 6000.0, 8000, 16000) / amplitudeAt (ref, 0, 6000.0, 8000, 16000));
        check (std::abs (g) < 0.01, "a loud treble does not move the low shelf");
    }

    {
        // Positive DYN lifts: a cymbal wash gets more air the louder it is.
        const Settings lift { { strip (0, hinge::pid::highShelf), 1.0f }, { strip (0, hinge::pid::highDyn), 4.0f },
                              { strip (0, hinge::pid::threshold), -36.0f } };
        auto out = render (lift, tone (12000.0, 0.5f), 48000);
        const auto g = db (amplitudeAt (out, 0, 12000.0, 24000, 24000) / 0.5);
        const auto full = analogHighShelfDb (hinge::network::positionForDb (4.0), hinge::network::highShelfCapacitors[1], 12000.0);
        check (std::abs (g - full) < 0.3, "positive DYN lifts the high shelf as far as it says, got "
                                           + juce::String (g, 2) + " expected " + juce::String (full, 2));
    }

    {
        // Linked, the side chain is shared: bass in the left alone moves both sides.
        auto leftOnly = [] (int n)
        {
            const auto v = (float) (0.5 * std::sin (twoPi * 50.0 * n / 48000.0));
            const auto w = (float) (0.003 * std::sin (twoPi * 50.0 * n / 48000.0));
            return std::pair { v, w };
        };

        auto out = render (tame, leftOnly, 48000);
        const auto right = db (amplitudeAt (out, 1, 50.0, 24000, 24000) / 0.003);
        check (std::abs (right) < 0.5, "linked side chain: the right channel follows the left's bass");

        auto unlinked = tame;
        unlinked[hinge::pid::link] = 0.0f;
        unlinked[strip (1, hinge::pid::lowShelf)]  = 0.0f;
        unlinked[strip (1, hinge::pid::lowLevel)]  = 5.0f;
        unlinked[strip (1, hinge::pid::lowDyn)]    = -5.0f;
        unlinked[strip (1, hinge::pid::threshold)] = -30.0f;
        out = render (unlinked, leftOnly, 48000);
        check (db (amplitudeAt (out, 1, 50.0, 24000, 24000) / 0.003) > 5.0, "unlinked: each channel listens to itself");
    }
}

void testRobustness()
{
    section ("Extreme settings and automation stay finite and smooth");

    Settings extreme;

    for (int s = 0; s < 2; ++s)
    {
        extreme[strip (s, hinge::pid::lowCut)]    = 1.0f;
        extreme[strip (s, hinge::pid::lowShelf)]  = 7.0f;
        extreme[strip (s, hinge::pid::lowLevel)]  = 5.0f;
        extreme[strip (s, hinge::pid::highLevel)] = 5.0f;
        extreme[strip (s, hinge::pid::highShelf)] = 0.0f;
        extreme[strip (s, hinge::pid::highCut)]   = 1.0f;
        extreme[strip (s, hinge::pid::output)]    = 10.0f;
        extreme[strip (s, hinge::pid::lowDyn)]    = 5.0f;
        extreme[strip (s, hinge::pid::highDyn)]   = -5.0f;
        extreme[strip (s, hinge::pid::threshold)] = -48.0f;
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
                    finite = finite && std::isfinite (out.getSample (ch, i)) && std::abs (out.getSample (ch, i)) < 16.0f;

            check (finite, "finite at " + juce::String (rate) + " Hz, block " + juce::String (blockSize));
        }
    }

    // Every switch thrown every few blocks under a steady tone: what comes out
    // must still be a continuous signal, no step bigger than the tone itself
    // could make.
    juce::Random turns (99);
    auto automate = [&turns] (hinge::HingeProcessor& p, int)
    {
        for (auto* id : { hinge::pid::lowCut, hinge::pid::lowShelf, hinge::pid::lowLevel, hinge::pid::highLevel,
                          hinge::pid::highShelf, hinge::pid::highCut, hinge::pid::output, hinge::pid::engage })
            if (turns.nextFloat() < 0.15f)
                p.apvts.getParameter (strip (0, id))->setValueNotifyingHost (turns.nextFloat());
    };

    auto out = render ({}, tone (100.0, 0.5f), 96000, 48000.0, 256, automate);
    double worstStep = 0.0;

    for (int i = 1; i < out.getNumSamples(); ++i)
        worstStep = std::max (worstStep, (double) std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));

    // A 100 Hz tone at 0.5 moves by 0.0065 per sample; through the most the
    // low shelf and the output can add, by 0.024.
    std::cout << "  largest sample to sample step under automation: " << worstStep << std::endl;
    check (worstStep < 0.03, "switching under signal makes no clicks");
}

void testState()
{
    section ("State survives a save");

    hinge::HingeProcessor a;
    a.apvts.getParameter (strip (1, hinge::pid::highShelf))->setValueNotifyingHost (5.0f / 7.0f);
    a.apvts.getParameter (strip (0, hinge::pid::lowDyn))->setValueNotifyingHost (0.2f);
    a.apvts.getParameter (hinge::pid::midSide)->setValueNotifyingHost (1.0f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    hinge::HingeProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    check (std::abs (b.apvts.getParameter (strip (1, hinge::pid::highShelf))->getValue() - 5.0f / 7.0f) < 1.0e-4f, "switch restored");
    check (std::abs (b.apvts.getParameter (strip (0, hinge::pid::lowDyn))->getValue() - 0.2f) < 1.0e-4f, "dynamics restored");
    check (b.apvts.getParameter (hinge::pid::midSide)->getValue() > 0.99f, "mid/side restored");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testNetwork();
    testDesign();
    testPluginResponse();
    testTransparency();
    testRouting();
    testDynamics();
    testRobustness();
    testState();

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
