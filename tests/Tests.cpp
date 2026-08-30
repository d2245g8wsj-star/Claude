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
#include "dsp/ClipEnvelope.h"

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
        setParam (processor, crest::pid::harmonics, 0.0f);
        setParam (processor, crest::pid::bias,    0.0f);
        setParam (processor, crest::pid::mix,     100.0f);
        setParam (processor, crest::pid::word,    0.0f);

        juce::AudioBuffer<float> reference (2, settle + window + 512);
        fillSine (reference, sampleRate, frequency, level);

        juce::AudioBuffer<float> buffer;
        buffer.makeCopyOf (reference);

        runBlocks (processor, buffer, blockSize);

        const auto latency = processor.getLatencySamples();

        // With oversampling off and every control neutral there is no filter
        // anywhere in the chain, so the output has to be the input. If this
        // fails, nothing measured below it means anything.
        if (osIndex == 0)
        {
            float worst = 0.0f;
            int   worstIndex = settle;

            for (int n = settle; n < settle + window; ++n)
            {
                const auto difference = std::abs (buffer.getSample (0, n) - reference.getSample (0, n));

                if (difference > worst)
                {
                    worst = difference;
                    worstIndex = n;
                }
            }

            check (worst < 1.0e-5f,
                   "OS off is not a passthrough: worst " + juce::String (worst, 9)
                       + " at sample " + juce::String (worstIndex)
                       + " (in " + juce::String (reference.getSample (0, worstIndex), 9)
                       + ", out " + juce::String (buffer.getSample (0, worstIndex), 9) + ")");

            std::cout << "  state: bias " << processor.apvts.getRawParameterValue (crest::pid::bias)->load()
                      << ", colour " << processor.apvts.getRawParameterValue (crest::pid::colour)->load()
                      << ", input " << processor.apvts.getRawParameterValue (crest::pid::input)->load()
                      << ", ceiling " << processor.apvts.getRawParameterValue (crest::pid::ceiling)->load()
                      << ", mix " << processor.apvts.getRawParameterValue (crest::pid::mix)->load()
                      << ", dc blocker " << (processor.dcBlockerRunning.load() ? "ON" : "off")
                      << ", os gain " << juce::String (processor.appliedOversamplingGain.load(), 9)
                      << std::endl;

            std::cout << "  OS off passthrough: worst " << juce::String (worst, 9)
                      << " at " << worstIndex
                      << " (in " << juce::String (reference.getSample (0, worstIndex), 9)
                      << ", out " << juce::String (buffer.getSample (0, worstIndex), 9) << ")"
                      << std::endl;
        }
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

void testHarmonics()
{
    section ("Harmonics");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 256;
    constexpr int    window     = 8192;
    constexpr int    settle     = 8192;

    constexpr double fundamental = sampleRate * 37.0 / (double) window;   // 216.8 Hz, exact bin

    auto configure = [&] (crest::CrestProcessor& processor, float amount, float series,
                          float drive, float ceiling)
    {
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        setParam (processor, crest::pid::os,        2.0f);
        setParam (processor, crest::pid::mode,      1.0f);
        setParam (processor, crest::pid::knee,      0.0f);
        setParam (processor, crest::pid::adapt,     0.0f);
        setParam (processor, crest::pid::bias,      0.0f);
        setParam (processor, crest::pid::colour,    0.0f);
        setParam (processor, crest::pid::sidechain, 15.0f);
        setParam (processor, crest::pid::mix,       100.0f);
        setParam (processor, crest::pid::word,      0.0f);
        setParam (processor, crest::pid::input,     drive);
        setParam (processor, crest::pid::ceiling,   ceiling);
        setParam (processor, crest::pid::output,    0.0f);
        setParam (processor, crest::pid::harmonics, amount);
        setParam (processor, crest::pid::series,    series);
    };

    // ---------------------------------------------------------------------
    // The stage is scaled by the clip envelope, so with nothing reaching the
    // ceiling it must produce nothing at all - at any setting. This is what
    // keeps the transparency promise true whatever the colour controls say.
    // ---------------------------------------------------------------------
    for (float series : { -100.0f, 0.0f, 100.0f })
    {
        crest::CrestProcessor processor;
        configure (processor, 100.0f, series, 0.0f, 0.0f);

        juce::AudioBuffer<float> reference (2, settle + window + 512);
        fillSine (reference, sampleRate, fundamental, 0.25f);

        juce::AudioBuffer<float> buffer;
        buffer.makeCopyOf (reference);

        runBlocks (processor, buffer, blockSize);

        const auto latency = processor.getLatencySamples();

        float worst = 0.0f;

        for (int n = settle; n < settle + window; ++n)
            worst = std::max (worst, std::abs (buffer.getSample (0, n)
                                               - reference.getSample (0, n - latency)));

        check (worst < 1.0e-4f,
               "harmonics at series " + juce::String (series, 0)
                   + " coloured a signal below the ceiling by " + juce::String (worst, 9));
    }

    // ---------------------------------------------------------------------
    // Generated before the shaper, so the ceiling is still guaranteed.
    // ---------------------------------------------------------------------
    for (float ceiling : { 0.0f, -6.0f })
    {
        crest::CrestProcessor processor;
        configure (processor, 100.0f, -100.0f, 18.0f, ceiling);

        juce::AudioBuffer<float> buffer (2, 16384);
        fillSine (buffer, sampleRate, fundamental, 0.9f);

        runBlocks (processor, buffer, blockSize);

        const auto peakDb = juce::Decibels::gainToDecibels (
            buffer.getMagnitude (2048, buffer.getNumSamples() - 2048));

        check (peakDb <= ceiling + 0.1f,
               "harmonics pushed the output past the ceiling: " + juce::String (peakDb, 3)
                   + " dB against " + juce::String (ceiling, 1));
    }

    // ---------------------------------------------------------------------
    // What the series control actually does. A symmetric clipper on a sine
    // produces odd harmonics only, so the second harmonic is a clean place to
    // watch the even series arrive.
    // ---------------------------------------------------------------------
    auto measure = [&] (float amount, float series)
    {
        crest::CrestProcessor processor;
        configure (processor, amount, series, 8.0f, -0.3f);

        juce::AudioBuffer<float> buffer (2, settle + window + 512);
        fillSine (buffer, sampleRate, fundamental, 0.5f);

        runBlocks (processor, buffer, blockSize);

        const auto first  = fitSine (buffer, 0, settle, window, fundamental,       sampleRate);
        const auto second = fitSine (buffer, 0, settle, window, fundamental * 2.0, sampleRate);
        const auto third  = fitSine (buffer, 0, settle, window, fundamental * 3.0, sampleRate);

        struct Result { double secondDb, thirdDb; };

        return Result { juce::Decibels::gainToDecibels (second.amplitude / first.amplitude, -160.0),
                        juce::Decibels::gainToDecibels (third.amplitude  / first.amplitude, -160.0) };
    };

    const auto none = measure (  0.0f,    0.0f);
    const auto even = measure (100.0f, -100.0f);
    const auto odd  = measure (100.0f,  100.0f);

    std::cout << "  2nd / 3rd harmonic relative to the fundamental:\n"
              << "    off        " << juce::String (none.secondDb, 1) << " / "
                                   << juce::String (none.thirdDb, 1) << " dBc\n"
              << "    even       " << juce::String (even.secondDb, 1) << " / "
                                   << juce::String (even.thirdDb, 1) << " dBc\n"
              << "    odd        " << juce::String (odd.secondDb, 1) << " / "
                                   << juce::String (odd.thirdDb, 1) << " dBc" << std::endl;

    check (even.secondDb > none.secondDb + 12.0,
           "the even series did not add a second harmonic ("
               + juce::String (none.secondDb, 1) + " then " + juce::String (even.secondDb, 1) + " dBc)");

    check (even.secondDb > odd.secondDb + 12.0,
           "the series control does not separate even from odd ("
               + juce::String (even.secondDb, 1) + " vs " + juce::String (odd.secondDb, 1) + " dBc)");

    check (odd.thirdDb > none.thirdDb + 1.0,
           "the odd series did not add a third harmonic ("
               + juce::String (none.thirdDb, 1) + " then " + juce::String (odd.thirdDb, 1) + " dBc)");

    check (even.secondDb < -12.0,
           "the even series is louder than a colour control should be ("
               + juce::String (even.secondDb, 1) + " dBc)");
}

void testAdaptiveClipping()
{
    section ("Adaptive clipping");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 256;
    constexpr int    window     = 8192;
    constexpr int    settle     = 8192;

    // ---------------------------------------------------------------------
    // The envelope has to tell a lone transient from clipping that keeps
    // coming back, on material where the individual overshoots are the same
    // length. That is the whole premise.
    // ---------------------------------------------------------------------
    {
        constexpr double osRate = 192000.0;

        crest::ClipEnvelope envelope;
        envelope.prepare (osRate);

        for (int n = 0; n < (int) (0.003 * osRate); ++n)
            envelope.process (true);

        const auto transient = envelope.value();

        envelope.reset();

        // A bass note: over the threshold for 2 ms of every 20 ms cycle.
        for (int cycle = 0; cycle < 30; ++cycle)
        {
            for (int n = 0; n < (int) (0.002 * osRate); ++n) envelope.process (true);
            for (int n = 0; n < (int) (0.018 * osRate); ++n) envelope.process (false);
        }

        const auto sustained = envelope.value();

        std::cout << "  envelope: lone 3 ms burst " << juce::String (transient, 3)
                  << ", bass note clipping every cycle " << juce::String (sustained, 3) << std::endl;

        check (transient < 0.06f,
               "a lone transient reads as sustained clipping (" + juce::String (transient, 3) + ")");

        check (sustained > transient * 4.0f,
               "the envelope cannot separate a transient from sustained clipping ("
                   + juce::String (transient, 3) + " vs " + juce::String (sustained, 3) + ")");
    }

    auto configure = [&] (crest::CrestProcessor& processor, float adapt, float drive, float ceiling)
    {
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        setParam (processor, crest::pid::os,        2.0f);
        setParam (processor, crest::pid::mode,      1.0f);     // EDGE
        setParam (processor, crest::pid::knee,      60.0f);    // the corner for sustained material
        setParam (processor, crest::pid::bias,      0.0f);
        setParam (processor, crest::pid::colour,    0.0f);
        setParam (processor, crest::pid::harmonics, 0.0f);
        setParam (processor, crest::pid::mix,       100.0f);
        setParam (processor, crest::pid::word,      0.0f);
        setParam (processor, crest::pid::sidechain, 15.0f);
        setParam (processor, crest::pid::input,     drive);
        setParam (processor, crest::pid::ceiling,   ceiling);
        setParam (processor, crest::pid::output,    0.0f);
        setParam (processor, crest::pid::adapt,     adapt);
    };

    // ---------------------------------------------------------------------
    // Whatever the adaptation does, the shaper is still the last word: the
    // ceiling holds at every setting.
    // ---------------------------------------------------------------------
    for (float adapt : { 0.0f, 50.0f, 100.0f })
    {
        crest::CrestProcessor processor;
        configure (processor, adapt, 24.0f, -3.0f);

        juce::AudioBuffer<float> buffer (2, 16384);
        fillSine (buffer, sampleRate, 220.0, 0.9f);

        runBlocks (processor, buffer, blockSize);

        const auto peakDb = juce::Decibels::gainToDecibels (
            buffer.getMagnitude (2048, buffer.getNumSamples() - 2048));

        check (peakDb <= -3.0f + 0.1f,
               "adapt " + juce::String (adapt, 0) + " %: ceiling exceeded at "
                   + juce::String (peakDb, 3) + " dB");
    }

    // ---------------------------------------------------------------------
    // A signal that never reaches the ceiling never triggers the envelope, so
    // the adaptation cannot colour anything below the knee.
    // ---------------------------------------------------------------------
    {
        constexpr double frequency = sampleRate * 37.0 / (double) window;

        crest::CrestProcessor processor;
        configure (processor, 100.0f, 0.0f, 0.0f);

        juce::AudioBuffer<float> reference (2, settle + window + 512);
        fillSine (reference, sampleRate, frequency, 0.25f);

        juce::AudioBuffer<float> buffer;
        buffer.makeCopyOf (reference);

        runBlocks (processor, buffer, blockSize);

        const auto latency = processor.getLatencySamples();

        float worst = 0.0f;

        for (int n = settle; n < settle + window; ++n)
            worst = std::max (worst, std::abs (buffer.getSample (0, n)
                                               - reference.getSample (0, n - latency)));

        check (worst < 1.0e-4f,
               "adaptation coloured a signal below the ceiling by " + juce::String (worst, 9));
    }

    // ---------------------------------------------------------------------
    // The claim itself, stated as the thing that can be measured: the
    // adaptation leaves sustained material with the character that was dialled
    // in, and reserves its effect for transients.
    // ---------------------------------------------------------------------
    constexpr double toneFrequency = sampleRate * 37.0 / (double) window;

    auto renderSustained = [&] (float adapt)
    {
        crest::CrestProcessor processor;
        configure (processor, adapt, 8.0f, -0.3f);

        juce::AudioBuffer<float> buffer (2, settle + window + 512);
        fillSine (buffer, sampleRate, toneFrequency, 0.5f);

        runBlocks (processor, buffer, blockSize);

        return buffer;
    };

    auto renderTransient = [&] (float adapt)
    {
        crest::CrestProcessor processor;
        configure (processor, adapt, 8.0f, -0.3f);

        juce::AudioBuffer<float> buffer (2, 8192);
        buffer.clear();

        // 3 ms of tone well above the ceiling, once, after a long silence.
        const auto length = (int) (0.003 * sampleRate);

        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < length; ++n)
                buffer.setSample (ch, 4096 + n, 0.5f * (float) std::sin (
                    juce::MathConstants<double>::twoPi * 1000.0 * (double) n / sampleRate));

        runBlocks (processor, buffer, blockSize);

        return buffer;
    };

    auto largestDifference = [] (const juce::AudioBuffer<float>& a,
                                 const juce::AudioBuffer<float>& b, int from, int to)
    {
        float worst = 0.0f, peak = 1.0e-9f;

        for (int n = from; n < to; ++n)
        {
            worst = std::max (worst, std::abs (a.getSample (0, n) - b.getSample (0, n)));
            peak  = std::max (peak,  std::abs (a.getSample (0, n)));
        }

        return juce::Decibels::gainToDecibels (worst / peak, -160.0f);
    };

    const auto sustainedStatic   = renderSustained (0.0f);
    const auto sustainedAdaptive = renderSustained (100.0f);
    const auto transientStatic   = renderTransient (0.0f);
    const auto transientAdaptive = renderTransient (100.0f);

    const auto sustainedChange = largestDifference (sustainedStatic, sustainedAdaptive,
                                                    settle, settle + window);
    const auto transientChange = largestDifference (transientStatic, transientAdaptive,
                                                    4000, 4600);

    std::cout << "  adapt 0 vs 100: sustained material changes by "
              << juce::String (sustainedChange, 1) << " dB, a lone transient by "
              << juce::String (transientChange, 1) << " dB" << std::endl;

    check (transientChange > sustainedChange + 12.0f,
           "the adaptation does not separate transients from sustained material ("
               + juce::String (transientChange, 1) + " vs "
               + juce::String (sustainedChange, 1) + " dB)");

    check (sustainedChange < -20.0f,
           "the adaptation changed sustained material by " + juce::String (sustainedChange, 1)
               + " dB, which is the character the user dialled in");

    check (transientChange > -30.0f,
           "the adaptation barely touched a transient (" + juce::String (transientChange, 1) + " dB)");
}

void testSidechain()
{
    section ("Sidechain low pass");

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 256;
    constexpr int    window     = 8192;
    constexpr int    settle     = 4096;

    constexpr double frequency = sampleRate * 10.0 / (double) window;   // 58.6 Hz, an exact bin

    auto configure = [&] (crest::CrestProcessor& processor, float cutoff, float drive, float ceiling)
    {
        processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
        processor.prepareToPlay (sampleRate, blockSize);

        setParam (processor, crest::pid::os,        2.0f);
        setParam (processor, crest::pid::mode,      0.0f);
        setParam (processor, crest::pid::knee,      0.0f);    // square corner: the worst case
        setParam (processor, crest::pid::adapt,     0.0f);
        setParam (processor, crest::pid::bias,      0.0f);
        setParam (processor, crest::pid::colour,    0.0f);
        setParam (processor, crest::pid::harmonics, 0.0f);
        setParam (processor, crest::pid::mix,       100.0f);
        setParam (processor, crest::pid::word,      0.0f);
        setParam (processor, crest::pid::input,     drive);
        setParam (processor, crest::pid::ceiling,   ceiling);
        setParam (processor, crest::pid::output,    0.0f);
        setParam (processor, crest::pid::sidechain, cutoff);
    };

    // ---------------------------------------------------------------------
    // The filter runs on the clipper's difference signal, which is zero when
    // nothing is being clipped. So no cutoff, however high, may colour a
    // signal that stays below the ceiling.
    // ---------------------------------------------------------------------
    for (float cutoff : { 15.0f, 60.0f, 200.0f, 800.0f })
    {
        crest::CrestProcessor processor;
        configure (processor, cutoff, 0.0f, 0.0f);

        juce::AudioBuffer<float> reference (2, settle + window + 512);
        fillSine (reference, sampleRate, frequency, 0.25f);     // -12 dBFS, never clips

        juce::AudioBuffer<float> buffer;
        buffer.makeCopyOf (reference);

        runBlocks (processor, buffer, blockSize);

        const auto latency = processor.getLatencySamples();

        float worst = 0.0f;

        for (int n = settle; n < settle + window; ++n)
            worst = std::max (worst, std::abs (buffer.getSample (0, n)
                                               - reference.getSample (0, n - latency)));

        check (worst < 1.0e-4f,
               "SC at " + juce::String (cutoff, 0) + " Hz colours a signal below the ceiling by "
                   + juce::String (worst, 9));
    }

    // ---------------------------------------------------------------------
    // What the control is actually for: a bass note sharing the signal with
    // brighter material. The clipper flattens the peaks, which the low end
    // dominates, and the low end loses level and shape as a result. Opening
    // the sidechain should hand that level back while the rest still clips.
    // ---------------------------------------------------------------------
    constexpr double highFrequency = sampleRate * 341.0 / (double) window;   // 1998 Hz, an exact bin
    constexpr float  lowLevel  = 0.5f;
    constexpr float  highLevel = 0.25f;
    constexpr float  driveDb   = 12.0f;

    auto measureLowBandLoss = [&] (float cutoff, float drive, bool tpSafe)
    {
        crest::CrestProcessor processor;
        configure (processor, cutoff, drive, -0.3f);
        setParam (processor, crest::pid::tpSafe, tpSafe ? 1.0f : 0.0f);

        juce::AudioBuffer<float> buffer (2, settle + window + 512);

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int n = 0; n < buffer.getNumSamples(); ++n)
            {
                const auto t = (double) n / sampleRate;
                buffer.setSample (ch, n,
                    lowLevel  * (float) std::sin (juce::MathConstants<double>::twoPi * frequency * t)
                  + highLevel * (float) std::sin (juce::MathConstants<double>::twoPi * highFrequency * t));
            }
        }

        runBlocks (processor, buffer, blockSize);

        const auto fit = fitSine (buffer, 0, settle, window, frequency, sampleRate);

        // Where the bass would have come out had nothing touched it.
        const auto intended = (double) lowLevel * juce::Decibels::decibelsToGain (drive);

        return juce::Decibels::gainToDecibels (fit.amplitude / intended);
    };

    // The operating envelope, printed because it is the thing a user needs to
    // know about this control and no single number captures it.
    for (float drive : { 3.0f, 6.0f, 12.0f })
        for (bool tp : { true, false })
            std::cout << "  drive " << juce::String (drive, 0) << " dB, TP " << (tp ? "on " : "off")
                      << ": off " << juce::String (measureLowBandLoss (15.0f, drive, tp), 2)
                      << " / 120 Hz " << juce::String (measureLowBandLoss (120.0f, drive, tp), 2)
                      << " / 400 Hz " << juce::String (measureLowBandLoss (400.0f, drive, tp), 2)
                      << " dB" << std::endl;

    // With the ceiling clamp out of the way the control does its whole job:
    // the bass comes out at the level it went in, while everything above the
    // cutoff is still being clipped.
    const auto openOff = measureLowBandLoss ( 15.0f, driveDb, false);
    const auto open120 = measureLowBandLoss (120.0f, driveDb, false);
    const auto open400 = measureLowBandLoss (400.0f, driveDb, false);

    check (openOff < -4.0,
           "the clipper is not costing the bass any level to hand back ("
               + juce::String (openOff, 2) + " dB)");

    check (open400 > -0.5,
           "with TP Safe off the sidechain did not restore the bass: "
               + juce::String (open400, 2) + " dB still missing");

    check (open400 > open120 && open120 > openOff,
           "opening the cutoff further did not preserve more of the low end ("
               + juce::String (openOff, 2) + ", " + juce::String (open120, 2)
               + ", " + juce::String (open400, 2) + " dB)");

    check (open400 < 0.5,
           "the sidechain handed back more level than the clipper removed ("
               + juce::String (open400, 2) + " dB)");

    // With TP Safe on the ceiling wins, by design: what the sidechain hands
    // back is only what fits underneath it. This is a trade the user makes,
    // so it is specified here rather than left to be discovered.
    const auto safe400 = measureLowBandLoss (400.0f, driveDb, true);

    check (safe400 <= open400 + 0.01,
           "TP Safe did not limit the sidechain to the headroom under the ceiling ("
               + juce::String (safe400, 2) + " vs " + juce::String (open400, 2) + " dB)");

    // ---------------------------------------------------------------------
    // Handing low frequency material back means the sum can climb past the
    // shaper's ceiling. TP Safe is the last line and has to hold it anyway.
    // ---------------------------------------------------------------------
    for (float ceiling : { 0.0f, -3.0f, -12.0f })
    {
        crest::CrestProcessor processor;
        configure (processor, 800.0f, 24.0f, ceiling);
        setParam (processor, crest::pid::tpSafe, 1.0f);

        juce::AudioBuffer<float> buffer (2, 16384);
        fillSine (buffer, sampleRate, frequency, 0.9f);

        runBlocks (processor, buffer, blockSize);

        const auto peakDb = juce::Decibels::gainToDecibels (
            buffer.getMagnitude (2048, buffer.getNumSamples() - 2048));

        check (peakDb <= ceiling + 0.1f,
               "with the sidechain wide open and TP Safe on, ceiling " + juce::String (ceiling, 1)
                   + " dB was exceeded: " + juce::String (peakDb, 3) + " dB");
    }

    std::cout << "  transparent below the ceiling at every cutoff, and TP Safe still holds the ceiling"
              << std::endl;
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
    testAdaptiveClipping();
    testHarmonics();
    testSidechain();
    testBypassAndDelta();
    testStabilityAcrossConfigurations();
    testStateRoundTrip();

    std::cout << "\n" << (failures == 0 ? "PASS" : "FAIL")
              << " - " << (checks - failures) << "/" << checks << " checks" << std::endl;

    return failures == 0 ? 0 : 1;
}
