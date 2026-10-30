/*
    Offline checks for the claims Lacquer makes about itself.

    A stepped equaliser is easy to break silently: a switch position that
    lands on the wrong frequency, a section at zero that is not quite flat, a
    bell to shelf change that leaves a step, a link that lets the sides drift,
    a bypass that is off by the oversampler's latency. Each is asserted here.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/Band.h"
#include "dsp/Colour.h"
#include "dsp/Voicing.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <set>

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
    lacquer::LacquerProcessor processor;
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

juce::String strip (int index, const char* name) { return lacquer::pid::forStrip (index, name); }

/** Every colour stage out, so what is left is the filter network alone. */
Settings clean()
{
    Settings s;

    for (int i = 0; i < 2; ++i)
    {
        s[strip (i, lacquer::pid::buffer)]      = 1.0f;     // the buffer is the linear side at low level
        s[strip (i, lacquer::pid::transformer)] = 0.0f;
    }

    return s;
}

/** Gain of the processor at one frequency, measured with a quiet tone. */
double measuredGainDb (Settings settings, double hz, int channel = 0)
{
    const int n = 48000;
    const auto amplitude = 0.01f;    // -40 dBFS: the amplifier and buffer are linear here
    const auto out = render (settings, tone (hz, amplitude), n);

    // A whole number of cycles near the end, after every glide has settled.
    const auto cycles = juce::jmax (1, (int) std::floor (hz * 0.5));
    const auto length = juce::roundToInt (cycles * 48000.0 / hz);
    return db (amplitudeAt (out, channel, hz, n - length, length) / amplitude);
}

// ---------------------------------------------------------------------------

void testVoicingTables()
{
    section ("Switch positions");

    using namespace lacquer::voicing;

    check (lowBoostHz.size() == 6 && lowCutHz.size() == 6, "bass switches have six positions");
    check (midBoostHz.size() == 11, "mid boost has eleven positions");
    check (midCutHz.size() == 11 && midCutHz[0] <= 0.0f, "mid cut has ten frequencies and OUT");
    check (trebleBoostHz.size() == 12 && trebleBells == 7, "treble boost has seven bells and five shelves");
    check (trebleCutHz.size() == 6, "treble cut has six positions");

    check (std::abs (amountDb (0, 13.0f)) < 1.0e-9f, "amount at zero is flat");
    check (std::abs (amountDb (amountSteps, 13.0f) - 13.0f) < 1.0e-5f, "amount at ten is the full gain");
    check (std::abs (amountDb (amountSteps / 2, 12.0f) - 3.0f) < 1.0e-5f, "half travel is a quarter of the gain");

    for (int i = 1; i <= amountSteps; ++i)
        check (amountDb (i, 10.0f) > amountDb (i - 1, 10.0f), "amount rises at every step " + juce::String (i));

    lacquer::voicing::Settings s;
    s.midCutFreq = 0;
    s.midCutAmount = amountSteps;
    check (std::abs (targets (s)[midCut].db) < 1.0e-9f, "mid cut OUT is flat at any amount");

    s.trebleBoostAmount = amountSteps;
    s.trebleBoostFreq = 2;
    check (std::abs (targets (s)[trebleShelf].db) < 1.0e-9f && targets (s)[trebleBell].db > 0.0f, "a bell position leaves the shelf idle");
    s.trebleBoostFreq = 9;
    check (std::abs (targets (s)[trebleBell].db) < 1.0e-9f && targets (s)[trebleShelf].db > 0.0f, "a shelf position leaves the bell idle");

    s.trebleBoostFreq = 3;
    const auto lowL = targets (s)[trebleBell].q;
    s.trebleBoostFreq = 4;
    const auto highL = targets (s)[trebleBell].q;
    check (highL > lowL, "the high inductance 10 kHz bell is the narrower one");

    s.midCutFreq = 5;
    s.midCutAmount = 4;
    const auto gentleQ = targets (s)[midCut].q;
    s.midCutAmount = amountSteps;
    check (targets (s)[midCut].q > gentleQ, "mid cut narrows as it deepens");

    lacquer::voicing::Settings classic, shifted;
    shifted.shift = true;
    classic.lowBoostAmount = shifted.lowBoostAmount = 14;
    check (targets (shifted)[lowBoost].q > targets (classic)[lowBoost].q, "Shift tightens the bass boost");
    check (std::abs (targets (shifted)[lowBoost].hz - targets (classic)[lowBoost].hz) > 1.0f, "Shift moves the bass corners");
}

void testBands()
{
    section ("Filter sections match their analogue prototypes");

    using lacquer::Band;
    const double rate = 192000.0;
    double worst = 0.0;

    for (auto type : { Band::Type::lowShelf, Band::Type::highShelf, Band::Type::bell })
    {
        for (auto [corner, gain, q] : { std::tuple { 100.0f, 12.0f, 0.8f }, std::tuple { 1000.0f, -10.0f, 2.0f },
                                        std::tuple { 8000.0f, 9.0f, 0.5f } })
        {
            for (double hz : { 40.0, 200.0, 1000.0, 5000.0, 12000.0 })
            {
                Band band (type);
                band.setTargets (corner, gain, q);
                band.prepare (rate);

                const int n = 192000;
                juce::AudioBuffer<float> out (1, n);

                for (int i = 0; i < n; ++i)
                    out.setSample (0, i, band.process ((float) std::sin (juce::MathConstants<double>::twoPi * hz * i / rate)));

                const auto cycles = juce::jmax (1, (int) std::floor (hz * 0.4));
                const auto length = juce::roundToInt (cycles * rate / hz);
                const auto measured = db (amplitudeAt (out, 0, hz, n - length, length, rate));
                const auto expected = db (std::abs (Band::prototype (type, hz, corner, gain, q)));
                worst = juce::jmax (worst, std::abs (measured - expected));
            }
        }
    }

    std::cout << "  worst deviation below 12 kHz at 192 kHz: " << worst << " dB" << std::endl;
    check (worst < 0.15, "every section within 0.15 dB of its prototype");

    // A section at 0 dB is the input, sample for sample.
    for (auto type : { Band::Type::lowShelf, Band::Type::highShelf, Band::Type::bell })
    {
        Band band (type);
        band.setTargets (3000.0f, 0.0f, 1.7f);
        band.prepare (rate);
        juce::Random random (7);
        float error = 0.0f;

        for (int i = 0; i < 10000; ++i)
        {
            const auto x = random.nextFloat() * 2.0f - 1.0f;
            error = juce::jmax (error, std::abs (band.process (x) - x));
        }

        check (error < 1.0e-6f, "a section at 0 dB is transparent, type " + juce::String ((int) type));
    }
}

void testFlatResponse()
{
    section ("All amounts at zero");

    for (double hz : { 30.0, 100.0, 1000.0, 10000.0, 18000.0 })
    {
        const auto clean_ = measuredGainDb (clean(), hz);

        auto coloured = clean();
        coloured[strip (0, lacquer::pid::buffer)]      = 0.0f;
        coloured[strip (0, lacquer::pid::transformer)] = 1.0f;
        const auto withTransformer = measuredGainDb (coloured, hz);

        std::cout << "  " << hz << " Hz: " << clean_ << " dB, unbuffered through the transformer " << withTransformer << " dB" << std::endl;
        check (std::abs (clean_) < 0.02, "flat at " + juce::String (hz) + " Hz");
        check (std::abs (withTransformer) < 0.4, "transformer and direct input stay within 0.4 dB at " + juce::String (hz) + " Hz");
    }
}

void testBandResponse()
{
    section ("Bands through the processor");

    using namespace lacquer;

    struct Case { const char* freqId; float freqIndex; const char* amountId; double probeHz; double expectedDb; double tolerance; };

    const Case cases[] = {
        { pid::midBoostFreq,    4.0f, pid::midBoostAmount,    1000.0,  voicing::midBoostMaxDb,    0.3 },
        { pid::midCutFreq,      4.0f, pid::midCutAmount,      1000.0,  voicing::midCutMaxDb,      0.3 },
        { pid::trebleBoostFreq, 1.0f, pid::trebleBoostAmount, 4200.0,  voicing::trebleBoostMaxDb, 0.3 },
        { pid::lowBoostFreq,    0.0f, pid::lowBoostAmount,    20.0,    voicing::lowBoostMaxDb,    1.2 },
        { pid::lowCutFreq,      5.0f, pid::lowCutAmount,      30.0,    voicing::lowCutMaxDb,      1.2 },
        { pid::trebleCutFreq,   0.0f, pid::trebleCutAmount,   20000.0, voicing::trebleCutMaxDb,   2.0 },
    };

    for (const auto& c : cases)
    {
        auto settings = clean();
        settings[strip (0, c.freqId)]   = c.freqIndex;
        settings[strip (0, c.amountId)] = (float) voicing::amountSteps;

        const auto measured = measuredGainDb (settings, c.probeHz);
        std::cout << "  " << c.amountId << " full at " << c.probeHz << " Hz: " << measured << " dB" << std::endl;
        check (std::abs (measured - c.expectedDb) < c.tolerance, juce::String (c.amountId) + " reaches its full gain");

        settings[strip (0, pid::eqIn)] = 0.0f;
        check (std::abs (measuredGainDb (settings, c.probeHz)) < 0.05, juce::String (c.amountId) + " is gone with EQ IN off");
    }

    // The treble boost: the shelf at 1 kHz lifts all of the top, a bell at
    // 2.5 kHz leaves 15 kHz alone.
    {
        auto settings = clean();
        settings[strip (0, pid::trebleBoostAmount)] = (float) voicing::amountSteps;
        settings[strip (0, pid::trebleBoostFreq)] = 11.0f;
        check (measuredGainDb (settings, 15000.0) > voicing::trebleBoostMaxDb - 1.0, "1 kHz shelf lifts the whole top");

        settings[strip (0, pid::trebleBoostFreq)] = 0.0f;
        check (measuredGainDb (settings, 15000.0) < 1.5, "2.5 kHz bell leaves 15 kHz alone");
    }
}

void testColour()
{
    section ("Colour stages");

    using namespace lacquer;

    const int n = 48000;

    const auto harmonicDb = [n] (Settings settings, double hz, float amplitude, int harmonic)
    {
        const auto out = render (settings, tone (hz, amplitude), n);
        const auto length = 48000 / 2;
        return db (amplitudeAt (out, 0, hz * harmonic, n - length, length)
                 / amplitudeAt (out, 0, hz, n - length, length));
    };

    Settings transformerOn, transformerOff;
    transformerOn [strip (0, pid::transformer)] = 1.0f;
    transformerOff[strip (0, pid::transformer)] = 0.0f;

    const auto thirdOn  = harmonicDb (transformerOn,  30.0, 1.0f, 3);
    const auto thirdOff = harmonicDb (transformerOff, 30.0, 1.0f, 3);
    const auto thirdOnHigh = harmonicDb (transformerOn, 1000.0, 1.0f, 3);
    std::cout << "  30 Hz at 0 dBFS, 3rd harmonic, transformer in / out: " << thirdOn << " / " << thirdOff << " dBc" << std::endl;
    std::cout << "  1 kHz at 0 dBFS, 3rd harmonic, transformer in: " << thirdOnHigh << " dBc" << std::endl;
    check (thirdOn > -45.0 && thirdOn < -25.0, "the transformer core bends the lows at full level");
    check (thirdOn > thirdOff + 10.0, "the transformer is where the low third comes from");
    check (thirdOnHigh < thirdOn - 15.0, "the core leaves the mids nearly alone");

    const auto thirdQuiet = harmonicDb (transformerOn, 30.0, 0.0316f, 3);
    std::cout << "  30 Hz at -30 dBFS, 3rd harmonic: " << thirdQuiet << " dBc" << std::endl;
    check (thirdQuiet < thirdOn - 40.0, "the core is clean at low level");

    Settings buffered, direct;
    buffered[strip (0, pid::buffer)] = 1.0f;
    direct  [strip (0, pid::buffer)] = 0.0f;
    buffered[strip (0, pid::transformer)] = direct[strip (0, pid::transformer)] = 0.0f;

    const auto secondBuffered = harmonicDb (buffered, 1000.0, 1.0f, 2);
    const auto secondDirect   = harmonicDb (direct,   1000.0, 1.0f, 2);
    std::cout << "  1 kHz at 0 dBFS, 2nd harmonic, buffer in / out: " << secondBuffered << " / " << secondDirect << " dBc" << std::endl;
    check (secondBuffered > -60.0 && secondBuffered < -30.0, "the amplifiers add a little second harmonic");
    check (std::abs (secondBuffered - secondDirect) > 1.0, "the input buffer changes the second harmonic");
}

void testLinkAndBypass()
{
    section ("Link and bypass");

    using namespace lacquer;

    Settings settings;
    settings[strip (1, pid::midBoostAmount)] = (float) voicing::amountSteps;    // channel 2 only
    settings[strip (1, pid::midBoostFreq)]   = 4.0f;

    settings[pid::link] = 1.0f;
    check (std::abs (measuredGainDb (settings, 1000.0, 1)) < 0.3, "linked, channel 2's own controls are ignored");

    settings[pid::link] = 0.0f;
    check (measuredGainDb (settings, 1000.0, 1) > voicing::midBoostMaxDb - 0.5, "unlinked, channel 2 follows its controls");
    check (std::abs (measuredGainDb (settings, 1000.0, 0)) < 0.3, "unlinked, channel 1 is untouched by channel 2");

    // Bypass is the input exactly, latency included.
    settings[pid::bypass] = 1.0f;
    settings[strip (0, pid::lowBoostAmount)] = 20.0f;
    const auto source = tone (440.0, 0.5f);
    const auto out = render (settings, source, 9600);
    float error = 0.0f;

    for (int i = 0; i < 9600; ++i)
        error = juce::jmax (error, std::abs (out.getSample (0, i) - source (i).first));

    check (error < 1.0e-6f, "bypass nulls against the input");
}

void testLatency()
{
    section ("Latency");

    std::set<int> latencies;

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        int latency = 0;
        render ({}, tone (100.0, 0.1f, rate), 1024, rate, 256, &latency);
        latencies.insert (latency);
        std::cout << "  " << rate << " Hz: " << latency << " samples" << std::endl;
    }

    check (latencies.size() == 1 && *latencies.begin() > 0, "the same whole number of samples at every rate");

    // An impulse comes out where the reported latency says.
    const auto out = render (clean(), [] (int i) { return i == 1000 ? std::pair { 0.5f, 0.5f } : std::pair { 0.0f, 0.0f }; }, 4000);
    int peakAt = 0;
    for (int i = 0; i < 4000; ++i)
        if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, peakAt)))
            peakAt = i;

    check (std::abs (peakAt - 1000) <= 1, "reported latency lines the output up with the input");
}

void testRobustness()
{
    section ("Extreme settings stay finite");

    using namespace lacquer;

    for (double rate : { 44100.0, 96000.0, 192000.0 })
    {
        for (int blockSize : { 1, 64, 1024 })
        {
            Settings settings;

            for (int i = 0; i < 2; ++i)
            {
                for (auto* id : { pid::lowBoostAmount, pid::lowCutAmount, pid::midBoostAmount, pid::midCutAmount,
                                  pid::trebleBoostAmount, pid::trebleCutAmount })
                    settings[strip (i, id)] = (float) voicing::amountSteps;

                settings[strip (i, pid::trebleQ)] = (float) voicing::qSteps;
                settings[strip (i, pid::trebleBoostFreq)] = 6.0f;    // 18 kHz bell
                settings[strip (i, pid::trebleCutFreq)]   = 5.0f;    // 24 kHz
                settings[strip (i, pid::midCutFreq)]      = 10.0f;
                settings[strip (i, pid::shift)]           = 1.0f;
            }

            settings[pid::output] = 12.0f;

            juce::Random random (11);
            const auto out = render (settings, [&random] (int) { const auto v = 4.0f * (random.nextFloat() - 0.5f); return std::pair { v, -v }; },
                                     (int) rate / 4, rate, blockSize);

            auto finite = true;
            float peak = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < out.getNumSamples(); ++i)
                {
                    const auto v = out.getSample (ch, i);
                    finite = finite && std::isfinite (v);
                    peak = juce::jmax (peak, std::abs (v));
                }

            check (finite && peak < 100.0f, "finite at " + juce::String (rate) + " Hz, block " + juce::String (blockSize));
        }
    }

    // Every switch thrown on every block: glides, no blow ups.
    {
        LacquerProcessor processor;
        processor.setPlayConfigDetails (2, 2, 48000.0, 128);
        processor.prepareToPlay (48000.0, 128);

        juce::AudioBuffer<float> block (2, 128);
        juce::MidiBuffer midi;
        juce::Random random (3);
        auto finite = true;

        for (int b = 0; b < 2000; ++b)
        {
            for (auto* parameter : processor.getParameters())
                if (parameter != processor.getBypassParameter())
                    parameter->setValueNotifyingHost (random.nextFloat());

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    block.setSample (ch, i, 0.5f * (float) std::sin (0.05 * (b * 128 + i)));

            processor.processBlock (block, midi);

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 128; ++i)
                    finite = finite && std::isfinite (block.getSample (ch, i)) && std::abs (block.getSample (ch, i)) < 100.0f;
        }

        check (finite, "random automation of every control stays finite");
    }
}

void testState()
{
    section ("State survives a save");

    using namespace lacquer;

    LacquerProcessor a;
    auto set = [&a] (const juce::String& id, float value)
    {
        auto* p = a.apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    set (strip (0, pid::trebleBoostFreq), 9.0f);
    set (strip (1, pid::midCutAmount), 13.0f);
    set (strip (1, pid::shift), 1.0f);
    set (pid::link, 0.0f);
    set (pid::output, -3.5f);
    a.setFinish (1);

    juce::MemoryBlock saved;
    a.getStateInformation (saved);

    LacquerProcessor b;
    b.setStateInformation (saved.getData(), (int) saved.getSize());

    auto same = true;

    for (auto* parameter : a.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameter))
            same = same && std::abs (parameter->getValue() - b.apvts.getParameter (withId->getParameterID())->getValue()) < 1.0e-6f;

    check (same, "every parameter comes back");
    check (b.getFinish() == 1, "the panel finish comes back");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testVoicingTables();
    testBands();
    testFlatResponse();
    testBandResponse();
    testColour();
    testLinkAndBypass();
    testLatency();
    testRobustness();
    testState();

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
