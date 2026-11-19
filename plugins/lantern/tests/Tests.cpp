/*
    Offline checks for the claims Lantern makes about itself.

    An equaliser modelled on hardware is easy to break silently: a band that
    no longer lands on the frequency its switch names, a section that drifts
    from its analogue prototype near the top of the band, a range switch that
    does not divide, a "flat" unit that is not quite. Each is asserted here.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "indiewave/SessionCompat.h"
#include "dsp/Eq.h"

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
using Source   = std::function<std::pair<float, float> (int)>;
using lantern::pid::band;

juce::AudioBuffer<float> render (const Settings& settings, const Source& source, int numSamples,
                                 double sampleRate = 48000.0, int blockSize = 512, int* latencyOut = nullptr,
                                 const std::function<void (lantern::LanternProcessor&, int)>& perBlock = {})
{
    lantern::LanternProcessor processor;
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

    for (int start = 0, index = 0; start < total; start += blockSize, ++index)
    {
        if (perBlock)
            perBlock (processor, index);

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

Source tone (double hz, float amplitude, double sampleRate = 48000.0)
{
    return [=] (int n)
    {
        const auto v = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * n / sampleRate);
        return std::pair { v, v };
    };
}

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

double thdOf (const juce::AudioBuffer<float>& buffer, int channel, double hz, int start, int length)
{
    const auto fundamental = amplitudeAt (buffer, channel, hz, start, length);
    double sum = 0.0;

    for (int h = 2; h <= 10; ++h)
        if (hz * h < 20000.0)
            sum += std::pow (amplitudeAt (buffer, channel, hz * h, start, length), 2.0);

    return 100.0 * std::sqrt (sum) / fundamental;
}

/** Gain of the processor at one frequency, in dB, with whole cycles in the
    window. */
double gainAt (const Settings& settings, double hz, float amplitude = 0.1f)
{
    // A window of exactly one second holds a whole number of cycles of any
    // whole frequency.
    auto out = render (settings, tone (hz, amplitude), 72000);
    return db (amplitudeAt (out, 0, hz, 24000, 48000) / amplitude);
}

int positionOf (int bandIndex, float hz)
{
    const auto& f = lantern::eq::frequencies[(size_t) bandIndex];
    for (int i = 0; i < lantern::eq::positions; ++i)
        if (std::abs (f[(size_t) i] - hz) < 0.5f)
            return i;
    jassertfalse;
    return 0;
}

// ---------------------------------------------------------------------------

void testPositions()
{
    section ("Band positions");

    const std::array<std::pair<float, float>, 4> spans { { { 22.0f, 1000.0f }, { 90.0f, 4800.0f }, { 260.0f, 11000.0f }, { 600.0f, 28000.0f } } };

    for (int b = 0; b < lantern::eq::numBands; ++b)
    {
        const auto& f = lantern::eq::frequencies[(size_t) b];
        bool rising = true;
        for (size_t i = 1; i < f.size(); ++i)
            rising = rising && f[i] > f[i - 1];

        check (rising, "band " + juce::String (b) + " rises at every step");
        check (std::abs (f.front() - spans[(size_t) b].first) < 0.5f && std::abs (f.back() - spans[(size_t) b].second) < 0.5f,
               "band " + juce::String (b) + " spans the published range");
    }
}

void testSections()
{
    section ("Every section matches its analogue prototype");

    using lantern::eq::Shape;
    constexpr double fs = 192000.0;

    struct Case { Shape shape; float hz, db, q; };
    const std::array<Case, 7> cases { {
        { Shape::bell,      1200.0f,  9.0f, 0.7f },
        { Shape::bell,      11000.0f, -6.0f, 2.0f },
        { Shape::lowShelf,  100.0f,   12.0f, 0.7071f },
        { Shape::highShelf, 16000.0f, 8.0f, 0.7071f },
        { Shape::highShelf, 28000.0f, -10.0f, 0.7071f },
        { Shape::highPass,  80.0f,    0.0f, 0.7071f },
        { Shape::lowPass,   12000.0f, 0.0f, 0.5412f },
    } };

    for (const auto& c : cases)
    {
        lantern::eq::Section s;
        s.set (lantern::eq::design (c.shape, c.hz, c.db, c.q, fs));

        auto worst = 0.0, worstLow = 0.0;
        for (double hz : { 25.0, 100.0, 440.0, 1000.0, 3000.0, 8000.0, 15000.0, 20000.0 })
        {
            s.reset();
            const int n = (int) fs;
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const auto phase = juce::MathConstants<double>::twoPi * hz * i / fs;
                const auto y = (double) s.process ((float) std::sin (phase));
                if (i >= n / 2)
                {
                    re += y * std::cos (phase);
                    im += y * std::sin (phase);
                }
            }

            const auto measured = db (2.0 * std::hypot (re, im) / (n / 2));
            const auto expected = 20.0 * std::log10 (std::abs (lantern::eq::analog (c.shape, c.hz, c.db, c.q, hz)));
            if (expected > -60.0)
            {
                worst = juce::jmax (worst, std::abs (measured - expected));
                if (hz <= 10000.0)
                    worstLow = juce::jmax (worstLow, std::abs (measured - expected));
            }
        }

        // The bilinear transform cramps the top octave a little even at 4x;
        // below 10 kHz it stays under 0.06 dB.
        check (worstLow < 0.06 && worst < 0.35,
               "shape " + juce::String ((int) c.shape) + " at " + juce::String (c.hz) + " Hz: within "
                   + juce::String (worstLow, 4) + " dB of the prototype to 10 kHz, " + juce::String (worst, 3) + " dB to 20 kHz");
    }

    // A section at 0 dB is a wire.
    lantern::eq::Section wire;
    wire.set (lantern::eq::design (Shape::bell, 1000.0f, 0.0f, 0.7f, fs));
    juce::Random random (7);
    auto exact = true;
    for (int i = 0; i < 1000; ++i)
    {
        const auto x = random.nextFloat() * 2.0f - 1.0f;
        exact = exact && std::abs (wire.process (x) - x) < 1.0e-6f;
    }
    check (exact, "a bell at 0 dB passes its input unchanged");
}

void testIdle()
{
    section ("Idle: flat and clean");

    for (double hz : { 30.0, 1000.0, 15000.0 })
    {
        const auto g = gainAt ({}, hz);
        check (std::abs (g) < 0.02, "flat at " + juce::String (hz) + " Hz, got " + juce::String (g, 4) + " dB");
    }

    auto out = render ({}, tone (1000.0, lantern::level::nominalAmplitude()), 48000);
    const auto thd = thdOf (out, 0, 1000.0, 24000, 24000);
    std::cout << "  THD at +4 dBu, GREEN: " << thd << " %" << std::endl;
    check (thd < 0.0025, "THD at +4 dBu under the specified 0.0025 %");

    // Bands switched out do nothing, whatever their gain.
    Settings out4;
    for (int b = 0; b < 4; ++b)
    {
        out4[band (b, "gain")] = 12.0f;
        out4[band (b, "in")]   = 0.0f;
    }
    check (std::abs (gainAt (out4, 1000.0)) < 0.02, "bands switched out are flat");
}

void testBands()
{
    section ("Each band lands where its switches say");

    // The inner bells reach their gain at their centre.
    for (int b : { 1, 2 })
        for (float hz : { lantern::eq::frequencies[(size_t) b][2], lantern::eq::frequencies[(size_t) b][7] })
            for (float gain : { 9.0f, -9.0f })
            {
                Settings s { { band (b, "freq"), (float) positionOf (b, hz) }, { band (b, "gain"), gain } };
                const auto g = gainAt (s, hz);
                check (std::abs (g - gain) < 0.15, "band " + juce::String (b) + " " + juce::String (gain, 0) + " dB at "
                                                       + juce::String (hz) + " Hz, got " + juce::String (g, 2));
            }

    // The outer bands are shelves unless BELL is in.
    {
        Settings s { { band (0, "freq"), (float) positionOf (0, 150.0f) }, { band (0, "gain"), 10.0f } };
        const auto low = gainAt (s, 25.0), high = gainAt (s, 5000.0);
        std::cout << "  LF shelf +10 at 150 Hz: " << low << " dB at 25 Hz, " << high << " dB at 5 kHz" << std::endl;
        check (std::abs (low - 10.0) < 0.6 && std::abs (high) < 0.1, "LF shelf lifts the bottom and leaves the top");

        s[band (0, "bell")] = 1.0f;
        check (std::abs (gainAt (s, 150.0) - 10.0) < 0.15 && gainAt (s, 25.0) < 5.0, "LF with BELL in is a bell");
    }
    {
        Settings s { { band (3, "freq"), (float) positionOf (3, 2200.0f) }, { band (3, "gain"), -8.0f } };
        const auto low = gainAt (s, 100.0), high = gainAt (s, 16000.0);
        check (std::abs (low) < 0.1 && std::abs (high + 8.0) < 0.6, "HF shelf cuts the top and leaves the bottom");
    }

    // RANGE: moderate makes the same knob worth a third.
    {
        Settings s { { band (1, "freq"), (float) positionOf (1, 1200.0f) }, { band (1, "gain"), 12.0f },
                     { lantern::pid::gainRange, (float) lantern::GainRange::moderate } };
        const auto g = gainAt (s, 1200.0);
        check (std::abs (g - 4.0) < 0.1, "moderate range: +12 on the knob is +4 dB, got " + juce::String (g, 3));
    }

    // Q: proportional narrows with gain, constant does not; HI-Q is narrower.
    {
        const auto widthOf = [] (const lantern::eq::Settings::Band& b)
        {
            // Octaves between the points where the boost is half its peak, in dB.
            lantern::eq::Settings s;
            s.bands = {};
            for (auto& x : s.bands) x.in = false;
            s.bands[1] = b;
            const auto peak = lantern::eq::responseDb (s, b.hz);
            double lo = b.hz, hi = b.hz;
            while (lantern::eq::responseDb (s, lo) > 0.5 * peak) lo /= 1.01;
            while (lantern::eq::responseDb (s, hi) > 0.5 * peak) hi *= 1.01;
            return std::log2 (hi / lo);
        };

        const auto bandWith = [] (float gain, float q)
        {
            lantern::eq::Settings::Band b;
            b.hz = 1200.0f; b.db = gain; b.q = q; b.shape = lantern::eq::Shape::bell; b.in = true;
            return b;
        };

        const auto prop3  = widthOf (bandWith (3.0f,  lantern::eq::proportionalQ (lantern::eq::bellQ, 3.0f)));
        const auto prop12 = widthOf (bandWith (12.0f, lantern::eq::proportionalQ (lantern::eq::bellQ, 12.0f)));
        const auto con3   = widthOf (bandWith (3.0f,  lantern::eq::bellQ));
        const auto con12  = widthOf (bandWith (12.0f, lantern::eq::bellQ));
        const auto hiq    = widthOf (bandWith (12.0f, lantern::eq::highQ));

        std::cout << "  width at half the boost, octaves: proportional " << prop3 << " (3 dB) / " << prop12
                  << " (12 dB); constant " << con3 << " / " << con12 << "; HI-Q " << hiq << std::endl;
        check (prop3 > 1.5 * prop12, "proportional Q: a small boost is broader than a large one");
        check (std::abs (con3 - con12) < 0.25 * con12, "constant Q: about one width at every gain");
        check (hiq < 0.6 * con12, "HI-Q is narrower");

        // And the processor applies the mode.
        Settings s { { band (1, "freq"), (float) positionOf (1, 1200.0f) }, { band (1, "gain"), 3.0f } };
        const auto propOff = gainAt (s, 600.0);
        s[lantern::pid::qMode] = (float) lantern::QMode::constant;
        const auto constOff = gainAt (s, 600.0);
        check (propOff > constOff + 0.2, "Q MODE reaches the audio: a small proportional boost spreads further");
    }
}

void testFilters()
{
    section ("High and low pass");

    for (const auto steep : { false, true })
    {
        Settings s { { lantern::pid::hpfIn, 1.0f }, { lantern::pid::hpf, 100.0f }, { lantern::pid::hpfSteep, steep ? 1.0f : 0.0f } };
        const auto corner = gainAt (s, 100.0), below = gainAt (s, 25.0), pass = gainAt (s, 2000.0);
        std::cout << "  HPF 100 Hz " << (steep ? "24" : "12") << " dB/oct: " << corner << " dB at 100 Hz, " << below << " at 25 Hz" << std::endl;
        check (std::abs (corner + 3.01) < 0.15, "HPF is -3 dB at its corner");
        check (std::abs (below + (steep ? 48.0 : 24.0)) < 1.5, "and falls at its slope two octaves down");
        check (std::abs (pass) < 0.02, "and passes the mids");
    }

    for (const auto steep : { false, true })
    {
        Settings s { { lantern::pid::lpfIn, 1.0f }, { lantern::pid::lpf, 8000.0f }, { lantern::pid::lpfSteep, steep ? 1.0f : 0.0f } };
        const auto corner = gainAt (s, 8000.0), above = gainAt (s, 16000.0), pass = gainAt (s, 500.0);
        std::cout << "  LPF 8 kHz " << (steep ? "24" : "12") << " dB/oct: " << corner << " dB at 8 kHz, " << above << " at 16 kHz" << std::endl;
        check (std::abs (corner + 3.01) < 0.15, "LPF is -3 dB at its corner");
        check (std::abs (above + (steep ? 24.0 : 12.0)) < 1.5, "and falls at its slope an octave up");
        check (std::abs (pass) < 0.02, "and passes the mids");
    }

    Settings out { { lantern::pid::hpf, 300.0f }, { lantern::pid::lpf, 6000.0f } };
    check (std::abs (gainAt (out, 100.0)) < 0.02 && std::abs (gainAt (out, 12000.0)) < 0.02, "switched out, the filters do nothing");
}

void testColour()
{
    section ("THD and the output stage");

    const auto nominal = lantern::level::nominalAmplitude();

    for (float steps : { 25.0f, 50.0f, 100.0f })
    {
        auto out = render ({ { lantern::pid::thd, steps } }, tone (1000.0, nominal), 48000);
        const auto measured = thdOf (out, 0, 1000.0, 24000, 24000);
        const auto expected = lantern::range::thdPercent (steps);
        std::cout << "  THD " << steps << ": measured " << measured << " %, reads " << expected << " %" << std::endl;
        check (std::abs (measured - expected) < 0.15 * expected, "THD knob is calibrated at " + juce::String (steps));
    }

    {
        auto out = render ({ { lantern::pid::iron, 1.0f } }, tone (1000.0, nominal), 48000);
        const auto thd = thdOf (out, 0, 1000.0, 24000, 24000);
        std::cout << "  RED at +4 dBu, 1 kHz: " << thd << " %" << std::endl;
        check (thd > 0.02 && thd < 0.08, "the tube output sits near the specified 0.04 %");
        check (std::abs (db (amplitudeAt (out, 0, 1000.0, 24000, 24000) / nominal)) < 0.2, "and keeps the level");
    }

    {
        Settings red { { lantern::pid::iron, 1.0f } };
        auto plain = render (red, tone (60.0, 0.3f), 48000);
        red[lantern::pid::output]  = 10.0f;
        red[lantern::pid::ironPad] = 10.0f;
        auto driven = render (red, tone (60.0, 0.3f), 48000);
        const auto level = db (amplitudeAt (driven, 0, 60.0, 24000, 24000) / amplitudeAt (plain, 0, 60.0, 24000, 24000));
        check (std::abs (level) < 1.0 && thdOf (driven, 0, 60.0, 24000, 24000) > 3.0 * thdOf (plain, 0, 60.0, 24000, 24000),
               "Output up into the PAD drives the iron harder at the same level");

        check (std::abs (gainAt ({ { lantern::pid::ironPad, 10.0f } }, 1000.0)) < 0.02, "the PAD does nothing in GREEN");
    }
}

void testModes()
{
    section ("Modes, bypass and links");

    const auto source = tone (1200.0, 0.2f);
    const Settings boost { { band (1, "freq"), (float) positionOf (1, 1200.0f) }, { band (1, "gain"), 9.0f } };

    {
        auto s = boost;
        s[lantern::pid::thd] = 60.0f;
        s[lantern::pid::bypass] = 1.0f;
        auto out = render (s, source, 24000);
        double error = 0.0, signal = 0.0;
        for (int i = 4800; i < out.getNumSamples(); ++i)
        {
            const auto x = source (i).first;
            error  += std::pow (out.getSample (0, i) - x, 2.0);
            signal += x * x;
        }
        check (10.0 * std::log10 (error / signal + 1.0e-30) < -140.0, "bypass nulls against the input");
    }

    {
        // Mid/side: a mono source is all mid, so the side's settings do not
        // touch it and a side-only source takes them.
        Settings ms { { lantern::pid::mode, (float) lantern::ChannelMode::midSide },
                      { lantern::pid::side (band (1, "freq")), (float) positionOf (1, 1200.0f) },
                      { lantern::pid::side (band (1, "gain")), 9.0f } };

        auto mono = render (ms, source, 48000);
        check (std::abs (db (amplitudeAt (mono, 0, 1200.0, 24000, 24000) / 0.2)) < 0.02, "mid/side: a mono source ignores the side's boost");

        auto antiphase = [] (int n)
        {
            const auto v = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 1200.0 * n / 48000.0);
            return std::pair { v, -v };
        };
        auto sideOnly = render (ms, antiphase, 48000);
        check (std::abs (db (amplitudeAt (sideOnly, 0, 1200.0, 24000, 24000) / 0.2) - 9.0) < 0.15,
               "mid/side: a side-only source takes it");

        auto stereo = ms;
        stereo[lantern::pid::mode] = (float) lantern::ChannelMode::stereo;
        auto ignored = render (stereo, antiphase, 48000);
        check (std::abs (db (amplitudeAt (ignored, 0, 1200.0, 24000, 24000) / 0.2)) < 0.02,
               "outside mid/side the second set does nothing");

        // The iron is wired left and right.
        Settings iron { { lantern::pid::mode, (float) lantern::ChannelMode::midSide }, { lantern::pid::iron, 1.0f }, { lantern::pid::ironPad, 6.0f } };
        auto c = render (iron, source, 24000);
        check (std::abs (db (amplitudeAt (c, 0, 1200.0, 12000, 12000) / 0.2) + 6.0) < 0.2
                   && std::abs (db (amplitudeAt (c, 1, 1200.0, 12000, 12000) / 0.2) + 6.0) < 0.2,
               "mid/side: IRON and its pad act on left and right");
    }

    {
        Settings s = boost;
        s[lantern::pid::mode] = (float) lantern::ChannelMode::dualMono;
        auto linked = render (s, source, 48000);
        check (std::abs (db (amplitudeAt (linked, 1, 1200.0, 24000, 24000) / 0.2) - 9.0) < 0.15,
               "dual mono, unsplit: the right takes the left's settings");

        s[lantern::pid::dualSplit] = 1.0f;
        auto split = render (s, source, 48000);
        check (std::abs (db (amplitudeAt (split, 1, 1200.0, 24000, 24000) / 0.2)) < 0.02
                   && std::abs (db (amplitudeAt (split, 0, 1200.0, 24000, 24000) / 0.2) - 9.0) < 0.15,
               "split: the right takes the second set (here flat), the left keeps its own");
    }
}

void testLatency()
{
    section ("Latency");

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        int latency = 0;
        auto out = render ({}, [] (int n) { return n == 1000 ? std::pair { 0.5f, 0.5f } : std::pair { 0.0f, 0.0f }; },
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
    section ("Extreme settings and automation stay finite");

    Settings extreme { { lantern::pid::hpfIn, 1.0f }, { lantern::pid::hpf, 300.0f }, { lantern::pid::hpfSteep, 1.0f },
                       { lantern::pid::lpfIn, 1.0f }, { lantern::pid::lpf, 6000.0f }, { lantern::pid::lpfSteep, 1.0f },
                       { lantern::pid::thd, 100.0f }, { lantern::pid::output, 15.0f }, { lantern::pid::iron, 1.0f } };
    for (int b = 0; b < 4; ++b)
    {
        extreme[band (b, "gain")] = 15.0f;
        extreme[band (b, "freq")] = 11.0f;
    }

    juce::Random random (1234);
    auto noise = [&random] (int) { return std::pair { random.nextFloat() * 2.0f - 1.0f, random.nextFloat() * 2.0f - 1.0f }; };

    for (double rate : { 44100.0, 96000.0, 192000.0 })
        for (int blockSize : { 1, 64, 1024 })
        {
            auto out = render (extreme, noise, 8192, rate, blockSize);
            auto finite = true;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                    finite = finite && std::isfinite (out.getSample (ch, i));
            check (finite, "finite at " + juce::String (rate) + " Hz, block " + juce::String (blockSize));
        }

    // Random automation of every control, every block.
    {
        juce::Random moves (99);
        auto out = render ({}, tone (440.0, 0.3f), 48000, 48000.0, 128, nullptr,
                           [&moves] (lantern::LanternProcessor& p, int)
                           {
                               for (auto* parameter : p.getParameters())
                                   if (parameter != p.getBypassParameter() && moves.nextFloat() < 0.3f)
                                       parameter->setValueNotifyingHost (moves.nextFloat());
                           });

        auto finite = true;
        auto peak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < out.getNumSamples(); ++i)
            {
                finite = finite && std::isfinite (out.getSample (ch, i));
                peak = juce::jmax (peak, std::abs (out.getSample (ch, i)));
            }
        check (finite && peak < 60.0f, "random automation stays finite and bounded, peak " + juce::String (peak, 2));
    }

    // A frequency switch glides rather than jumping.
    {
        auto out = render ({ { band (1, "gain"), 12.0f }, { band (1, "freq"), 0.0f } }, tone (1000.0, 0.1f), 24000, 48000.0, 256, nullptr,
                           [] (lantern::LanternProcessor& p, int index)
                           {
                               if (index == 40)
                                   p.apvts.getParameter (band (1, "freq"))->setValueNotifyingHost (1.0f);
                           });
        auto biggest = 0.0f;
        for (int i = 9000; i < 12000; ++i)
            biggest = juce::jmax (biggest, std::abs (out.getSample (0, i) - out.getSample (0, i - 1)));
        // A 1 kHz tone at up to +12 dB moves at most about 0.05 per sample.
        check (biggest < 0.08f, "switching a band's frequency does not click, largest step " + juce::String (biggest, 4));
    }
}

void testState()
{
    section ("State survives a save");

    lantern::LanternProcessor a;
    a.apvts.getParameter (band (2, "gain"))->setValueNotifyingHost (0.81f);
    a.apvts.getParameter (band (3, "freq"))->setValueNotifyingHost (1.0f);
    a.apvts.getParameter (lantern::pid::gainRange)->setValueNotifyingHost (0.0f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    lantern::LanternProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    check (std::abs (b.apvts.getParameter (band (2, "gain"))->getValue() - 0.81f) < 1.0e-3f, "gain restored");
    check (b.apvts.getParameter (band (3, "freq"))->getValue() > 0.99f, "frequency restored");
    check (b.apvts.getParameter (lantern::pid::gainRange)->getValue() < 0.01f, "range restored");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testPositions();
    testSections();
    testIdle();
    testBands();
    testFilters();
    testColour();
    testModes();
    testLatency();
    testRobustness();
    testState();

    section ("Sessions saved by released versions open unchanged");
    indiewave::compat::run ([] { return std::make_unique<lantern::LanternProcessor>(); },
                            juce::File (INDIEWAVE_COMPAT_DIR), INDIEWAVE_VERSION, check);

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
