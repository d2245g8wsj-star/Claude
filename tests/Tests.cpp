/*
    Offline checks for the claims Crest makes about itself.

    These are the properties that are easy to break silently in a clipper:
    the ceiling stops being a ceiling, the "transparent below threshold" promise
    turns into a fraction of a dB of gain, or a rarely used oversampling factor
    starts producing NaN. Each of those is a bug a listening test can miss for
    weeks, so they are asserted here instead.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/Shaper.h"
#include "dsp/Quantizer.h"

#include <cmath>
#include <iostream>

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

// ---------------------------------------------------------------------------

void testShaperInvariants()
{
    section ("Shaper invariants");

    for (int modeIndex = 0; modeIndex < crest::numModes; ++modeIndex)
    {
        const auto mode = static_cast<crest::Mode> (modeIndex);
        const auto name = juce::String (crest::modeName (mode));

        for (float knee : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            for (float bias : { -1.0f, -0.4f, 0.0f, 0.4f, 1.0f })
            {
                crest::Shaper shaper;
                shaper.setParameters (mode, knee, bias);

                const auto linearRegion = std::min (shaper.thresholdPos, shaper.thresholdNeg);

                float previous = -2.0f;

                for (int i = -6000; i <= 6000; ++i)
                {
                    const auto x = (float) i * 0.01f;   // -60 ... +60
                    const auto y = shaper.process (x);

                    if (std::abs (y) > 1.0f + 1.0e-6f)
                    {
                        check (false, name + ": output " + juce::String (y)
                                          + " exceeds the ceiling at x = " + juce::String (x));
                        break;
                    }

                    if (std::isnan (y) || std::isinf (y))
                    {
                        check (false, name + ": non-finite output at x = " + juce::String (x));
                        break;
                    }

                    if (y < previous - 1.0e-6f)
                    {
                        check (false, name + ": transfer curve is not monotonic at x = " + juce::String (x));
                        break;
                    }

                    previous = y;

                    // Below the knee threshold the gain must be exactly 1.0.
                    if (std::abs (x) <= linearRegion && std::abs (y - x) > 1.0e-7f)
                    {
                        check (false, name + ": not transparent below the knee at x = " + juce::String (x));
                        break;
                    }
                }
            }
        }
    }

    // With no bias the curve has to be odd, or the plugin generates a DC offset
    // and even harmonics nobody asked for.
    for (int modeIndex = 0; modeIndex < crest::numModes; ++modeIndex)
    {
        crest::Shaper shaper;
        shaper.setParameters (static_cast<crest::Mode> (modeIndex), 0.6f, 0.0f);

        bool symmetric = true;

        for (int i = 1; i <= 400; ++i)
        {
            const auto x = (float) i * 0.01f;
            symmetric = symmetric && std::abs (shaper.process (x) + shaper.process (-x)) < 1.0e-6f;
        }

        check (symmetric, juce::String (crest::modeName (static_cast<crest::Mode> (modeIndex)))
                              + ": curve is not symmetric at zero bias");
    }

    // Bias has to actually break that symmetry, otherwise the control is a lie.
    crest::Shaper biased;
    biased.setParameters (crest::Mode::silk, 0.6f, 0.8f);
    check (std::abs (biased.process (1.0f) + biased.process (-1.0f)) > 1.0e-3f,
           "bias does not produce an asymmetric curve");

    std::cout << "  " << crest::numModes << " characters checked across knee and bias ranges" << std::endl;
}

void testQuantizer()
{
    section ("Quantiser");

    for (auto word : { crest::WordLength::bits24, crest::WordLength::bits20, crest::WordLength::bits16 })
    {
        crest::Quantizer quantizer;
        quantizer.setSeed (1);
        quantizer.setParameters (word, crest::DitherType::off);

        const auto bits = crest::wordLengthBits (word);
        const auto step = 2.0f / (float) (1u << (bits - 1));

        bool onGrid   = true;
        bool inRange  = true;

        for (int i = -2000; i <= 2000; ++i)
        {
            const auto x = (float) i * 0.001f;
            const auto y = quantizer.process (x);

            onGrid  = onGrid && std::abs (y / step - std::round (y / step)) < 1.0e-3f;
            inRange = inRange && std::abs (y) <= 1.0f + 1.0e-6f;
        }

        check (onGrid,  juce::String (bits) + " bit: output is not on the quantisation grid");
        check (inRange, juce::String (bits) + " bit: output leaves full scale");
    }

    // Dither must add noise, and shaped dither must not run away.
    crest::Quantizer dithered;
    dithered.setSeed (7);
    dithered.setParameters (crest::WordLength::bits16, crest::DitherType::shaped);

    double sum = 0.0;
    bool   finite = true;

    for (int i = 0; i < 200000; ++i)
    {
        const auto y = dithered.process (0.0f);
        sum += std::abs (y);
        finite = finite && std::isfinite (y);
    }

    check (finite,     "shaped dither produced a non-finite sample");
    check (sum > 0.0,  "dither added no noise at digital black");

    std::cout << "  word lengths on grid, dither finite over 200k samples" << std::endl;
}

// ---------------------------------------------------------------------------

void setParam (crest::CrestProcessor& processor, const char* id, float value)
{
    auto* parameter = processor.apvts.getParameter (id);
    jassert (parameter != nullptr);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

void runBlocks (crest::CrestProcessor& processor, juce::AudioBuffer<float>& buffer, int blockSize)
{
    juce::MidiBuffer midi;

    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto count = std::min (blockSize, buffer.getNumSamples() - start);

        juce::AudioBuffer<float> block (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                        start, count);
        processor.processBlock (block, midi);
    }
}

void fillSine (juce::AudioBuffer<float>& buffer, double sampleRate, double frequency, float gain)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int n = 0; n < buffer.getNumSamples(); ++n)
            buffer.setSample (ch, n, gain * (float) std::sin (juce::MathConstants<double>::twoPi
                                                              * frequency * (double) n / sampleRate));
}

void testCeilingIsHonoured()
{
    section ("Ceiling");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 256;

    for (int osIndex = 0; osIndex <= crest::maxOversamplingStages; ++osIndex)
    {
        for (float ceilingDb : { 0.0f, -0.3f, -3.0f, -12.0f })
        {
            crest::CrestProcessor processor;
            processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
            processor.prepareToPlay (sampleRate, blockSize);

            setParam (processor, crest::pid::os,      (float) osIndex);
            setParam (processor, crest::pid::ceiling, ceilingDb);
            setParam (processor, crest::pid::input,   24.0f);
            setParam (processor, crest::pid::output,  0.0f);
            setParam (processor, crest::pid::mix,     100.0f);
            setParam (processor, crest::pid::colour,  100.0f);
            setParam (processor, crest::pid::knee,    0.0f);
            setParam (processor, crest::pid::word,    0.0f);

            juce::AudioBuffer<float> buffer (2, 16384);
            fillSine (buffer, sampleRate, 997.0, 0.9f);

            runBlocks (processor, buffer, blockSize);

            // Skip the smoothing ramp at the very start.
            const auto peak = buffer.getMagnitude (2048, buffer.getNumSamples() - 2048);
            const auto peakDb = juce::Decibels::gainToDecibels (peak);

            check (peakDb <= ceilingDb + 0.1f,
                   "OS " + juce::String (1 << osIndex) + "x, ceiling " + juce::String (ceilingDb)
                       + " dB: output reached " + juce::String (peakDb, 3) + " dB");

            check (std::isfinite (peak), "non-finite output at OS " + juce::String (1 << osIndex) + "x");
        }
    }

    std::cout << "  ceiling held at every oversampling factor, driven 24 dB into it" << std::endl;
}

/** Least squares fit of a single sine to a stretch of a buffer.

    A sample by sample null test cannot separate "the plugin changed the signal"
    from "the plugin is a fraction of a sample away from the latency it reports",
    and for an oversampled plugin the second one is unavoidable: the host can
    only compensate whole samples. Fitting the sine measures the three things
    that actually matter on their own terms - level, alignment, and everything
    that is not the original tone.
*/
struct SineFit
{
    double amplitude   { 0.0 };
    double phase       { 0.0 };   ///< radians, referenced to absolute sample 0
    double residualRms { 0.0 };   ///< everything that is not the fitted tone
};

SineFit fitSine (const juce::AudioBuffer<float>& buffer, int channel,
                 int start, int count, double frequency, double sampleRate)
{
    const auto omega = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    double inPhase = 0.0, quadrature = 0.0;

    for (int n = start; n < start + count; ++n)
    {
        const auto x = (double) buffer.getSample (channel, n);
        inPhase    += x * std::sin (omega * (double) n);
        quadrature += x * std::cos (omega * (double) n);
    }

    inPhase    *= 2.0 / (double) count;
    quadrature *= 2.0 / (double) count;

    SineFit fit;
    fit.amplitude = std::sqrt (inPhase * inPhase + quadrature * quadrature);
    fit.phase     = std::atan2 (quadrature, inPhase);

    double sumOfSquares = 0.0;

    for (int n = start; n < start + count; ++n)
    {
        const auto x = (double) buffer.getSample (channel, n);
        const auto fitted = fit.amplitude * std::sin (omega * (double) n + fit.phase);
        sumOfSquares += (x - fitted) * (x - fitted);
    }

    fit.residualRms = std::sqrt (sumOfSquares / (double) count);

    return fit;
}

void testTransparencyAndLatency()
{
    section ("Transparency and latency");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 128;
    constexpr int    window     = 8192;
    constexpr int    settle     = 4096;

    // An exact analysis bin, so the fit sees no spectral leakage.
    constexpr double frequency = sampleRate * 37.0 / (double) window;
    constexpr float  level     = 0.25f;                       // -12 dBFS, well under the ceiling

    const auto omega = juce::MathConstants<double>::twoPi * frequency / sampleRate;

    for (int osIndex = 0; osIndex <= crest::maxOversamplingStages; ++osIndex)
    {
        crest::CrestProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        // Everything neutral: nothing should touch a signal that never reaches
        // the ceiling.
        setParam (processor, crest::pid::os,      (float) osIndex);
        setParam (processor, crest::pid::input,   0.0f);
        setParam (processor, crest::pid::ceiling, 0.0f);
        setParam (processor, crest::pid::output,  0.0f);
        setParam (processor, crest::pid::knee,    0.0f);
        setParam (processor, crest::pid::colour,  0.0f);
        setParam (processor, crest::pid::bias,    0.0f);
        setParam (processor, crest::pid::mix,     100.0f);
        setParam (processor, crest::pid::word,    0.0f);

        juce::AudioBuffer<float> buffer (2, settle + window + 512);
        fillSine (buffer, sampleRate, frequency, level);

        runBlocks (processor, buffer, blockSize);

        const auto latency = processor.getLatencySamples();
        const auto fit     = fitSine (buffer, 0, settle, window, frequency, sampleRate);

        const auto gainErrorDb = juce::Decibels::gainToDecibels (fit.amplitude / (double) level);

        // Phase lag converted back into samples, then compared with what the
        // plugin told the host to compensate for.
        const auto period = sampleRate / frequency;

        auto alignmentError = -fit.phase / omega - (double) latency;

        // The fit only knows the phase modulo one period; fold the difference
        // into the nearest whole cycle before judging it.
        alignmentError -= period * std::round (alignmentError / period);

        // Residual is referenced to the tone: this is THD+N of the wet path.
        const auto distortionDb = juce::Decibels::gainToDecibels (fit.residualRms
                                                                  / (fit.amplitude * 0.7071), -160.0);

        const auto label = "OS " + juce::String (1 << osIndex) + "x";

        check (std::abs (gainErrorDb) < 0.01,
               label + ": gain error below the ceiling is " + juce::String (gainErrorDb, 4) + " dB");

        check (distortionDb < -100.0,
               label + ": THD+N below the ceiling is " + juce::String (distortionDb, 1) + " dB");

        check (std::abs (alignmentError) < 0.5,
               label + ": reported latency is off by " + juce::String (alignmentError, 3) + " samples");

        std::cout << "  " << label << ": latency " << latency
                  << " smp (" << juce::String (alignmentError, 3) << " residual)"
                  << ", gain " << juce::String (gainErrorDb, 4) << " dB"
                  << ", THD+N " << juce::String (distortionDb, 1) << " dB" << std::endl;
    }
}

void testBypassAndDelta()
{
    section ("Bypass, delta and mix");

    constexpr double sampleRate = 44100.0;
    constexpr int    blockSize  = 512;

    {
        crest::CrestProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        setParam (processor, crest::pid::bypass, 1.0f);
        setParam (processor, crest::pid::input,  24.0f);

        juce::AudioBuffer<float> reference (2, 8192);
        fillSine (reference, sampleRate, 440.0, 0.5f);

        juce::AudioBuffer<float> buffer;
        buffer.makeCopyOf (reference);

        runBlocks (processor, buffer, blockSize);

        const auto latency = processor.getLatencySamples();

        float worst = 0.0f;

        for (int n = 1024; n < reference.getNumSamples() - latency; ++n)
            worst = std::max (worst, std::abs (buffer.getSample (0, n + latency)
                                               - reference.getSample (0, n)));

        check (worst < 1.0e-5f, "bypass is not transparent (worst error "
                                    + juce::String (worst, 8) + ")");
    }

    {
        // Delta with a signal that never reaches the ceiling must be silence:
        // nothing was removed, so there is nothing to hear.
        crest::CrestProcessor processor;
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        setParam (processor, crest::pid::delta,   1.0f);
        setParam (processor, crest::pid::input,   0.0f);
        setParam (processor, crest::pid::ceiling, 0.0f);
        setParam (processor, crest::pid::knee,    0.0f);
        setParam (processor, crest::pid::colour,  0.0f);
        setParam (processor, crest::pid::word,    0.0f);

        juce::AudioBuffer<float> buffer (2, 8192);
        fillSine (buffer, sampleRate, 440.0, 0.1f);       // -20 dBFS

        runBlocks (processor, buffer, blockSize);

        const auto residual = buffer.getMagnitude (2048, buffer.getNumSamples() - 2048);

        check (residual < 3.0e-3f, "delta is not silent below the ceiling (residual "
                                       + juce::String (juce::Decibels::gainToDecibels (residual), 1) + " dBFS)");
    }

    std::cout << "  bypass sample aligned, delta silent below the ceiling" << std::endl;
}

void testStabilityAcrossConfigurations()
{
    section ("Stability");

    juce::Random random (20260830);

    for (const auto sampleRate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        for (const auto channels : { 1, 2 })
        {
            for (const auto blockSize : { 32, 512, 2048 })
            {
                crest::CrestProcessor processor;
                processor.setPlayConfigDetails (channels, channels, sampleRate, blockSize);
                processor.prepareToPlay (sampleRate, blockSize);

                // Extremes on every control at once.
                setParam (processor, crest::pid::input,   30.0f);
                setParam (processor, crest::pid::ceiling, -24.0f);
                setParam (processor, crest::pid::knee,    100.0f);
                setParam (processor, crest::pid::bias,    100.0f);
                setParam (processor, crest::pid::colour,  100.0f);
                setParam (processor, crest::pid::output,  12.0f);
                setParam (processor, crest::pid::os,      4.0f);
                setParam (processor, crest::pid::word,    3.0f);
                setParam (processor, crest::pid::dither,  2.0f);

                juce::AudioBuffer<float> buffer (channels, blockSize * 8);

                for (int ch = 0; ch < channels; ++ch)
                    for (int n = 0; n < buffer.getNumSamples(); ++n)
                        buffer.setSample (ch, n, random.nextFloat() * 2.0f - 1.0f);

                runBlocks (processor, buffer, blockSize);

                bool finite = true;

                for (int ch = 0; ch < channels; ++ch)
                    for (int n = 0; n < buffer.getNumSamples(); ++n)
                        finite = finite && std::isfinite (buffer.getSample (ch, n));

                check (finite, juce::String (sampleRate) + " Hz, " + juce::String (channels)
                                   + " ch, block " + juce::String (blockSize) + ": non-finite output");
            }
        }
    }

    // Silence in, silence out - a clipper that idles at anything but zero has a
    // DC or dither bug.
    crest::CrestProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 256);
    processor.prepareToPlay (48000.0, 256);
    setParam (processor, crest::pid::word,   0.0f);
    setParam (processor, crest::pid::colour, 100.0f);

    juce::AudioBuffer<float> silence (2, 4096);
    silence.clear();
    runBlocks (processor, silence, 256);

    check (silence.getMagnitude (0, silence.getNumSamples()) < 1.0e-6f,
           "the plugin is not silent with no input");

    std::cout << "  finite and stable across rates, channel counts and block sizes" << std::endl;
}

void testStateRoundTrip()
{
    section ("State");

    crest::CrestProcessor source;
    source.setPlayConfigDetails (2, 2, 48000.0, 256);

    setParam (source, crest::pid::input,   7.5f);
    setParam (source, crest::pid::ceiling, -1.75f);
    setParam (source, crest::pid::knee,    42.0f);
    setParam (source, crest::pid::mode,    3.0f);

    juce::MemoryBlock state;
    source.getStateInformation (state);

    crest::CrestProcessor destination;
    destination.setPlayConfigDetails (2, 2, 48000.0, 256);
    destination.setStateInformation (state.getData(), (int) state.getSize());

    for (auto* id : { crest::pid::input, crest::pid::ceiling, crest::pid::knee, crest::pid::mode })
    {
        const auto a = source.apvts.getRawParameterValue (id)->load();
        const auto b = destination.apvts.getRawParameterValue (id)->load();

        check (std::abs (a - b) < 1.0e-4f, juce::String (id) + " did not survive a state round trip");
    }

    std::cout << "  parameters restored from a saved session" << std::endl;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    std::cout << "CREST - IndieWave Audio - offline checks" << std::endl;

    testShaperInvariants();
    testQuantizer();
    testCeilingIsHonoured();
    testTransparencyAndLatency();
    testBypassAndDelta();
    testStabilityAcrossConfigurations();
    testStateRoundTrip();

    std::cout << "\n" << (failures == 0 ? "PASS" : "FAIL")
              << " - " << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
