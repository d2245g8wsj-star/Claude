/*
    Offline checks for the claims Breakwater makes about itself.

    A limiter's promises are numbers: the ceiling is never crossed, the true
    peak stays under it, a quiet signal comes out exactly as it went in, the
    reported latency is the real one, the meters read what the standard says
    they should. Each is asserted here, across every engine, style and
    oversampling factor.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/PeakLimiter.h"
#include "dsp/SoftClipper.h"
#include "dsp/TruePeak.h"

#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <random>

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

void apply (breakwater::BreakwaterProcessor& processor, const Settings& settings)
{
    for (const auto& [id, value] : settings)
    {
        auto* parameter = processor.apvts.getParameter (id);
        jassert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
}

/** Renders a stereo signal through a fresh instance with the given settings,
    and returns the output with the reported latency removed. */
float lastTruePeakReading = 0.0f;

juce::AudioBuffer<float> render (const Settings& settings, const Source& source, int numSamples,
                                 double sampleRate = 48000.0, int blockSize = 512, int* latencyOut = nullptr)
{
    breakwater::BreakwaterProcessor processor;
    processor.setPlayConfigDetails (2, 2, sampleRate, blockSize);
    apply (processor, settings);
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

    lastTruePeakReading = processor.meters.truePeakMax.load();
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

/** Something a limiter finds hard: a kick on every beat, a snare on the
    backbeat, a sustained bass, and noise, all different per channel. */
Source drums (double sampleRate = 48000.0)
{
    auto noise = std::make_shared<std::vector<float>> (1 << 20);
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);

    for (auto& v : *noise)
        v = uniform (rng);

    return [=] (int n)
    {
        const auto t     = n / sampleRate;
        const auto beat  = std::fmod (t, 0.5);
        const auto twoPi = juce::MathConstants<double>::twoPi;

        const auto kick  = 0.9 * std::exp (-beat * 18.0) * std::sin (twoPi * (50.0 + 120.0 * std::exp (-beat * 40.0)) * beat);
        const auto bar   = std::fmod (t, 1.0);
        const auto snare = bar > 0.5 ? 0.6 * std::exp (-(bar - 0.5) * 30.0) * (*noise)[(size_t) n % noise->size()] : 0.0;
        const auto bass  = 0.35 * std::sin (twoPi * 55.0 * t);
        const auto hats  = 0.08 * (*noise)[(size_t) (n * 7 + 13) % noise->size()];

        const auto l = kick + snare + bass + hats;
        const auto r = kick + 0.8 * snare + bass - hats;
        return std::pair { (float) l, (float) r };
    };
}

double db (double gain) { return 20.0 * std::log10 (juce::jmax (gain, 1.0e-12)); }

/** Amplitude of one frequency on the left channel over [start, start + length). */
double amplitudeAt (const juce::AudioBuffer<float>& buffer, double hz, int start, int length, double sampleRate = 48000.0)
{
    double re = 0.0, im = 0.0;

    for (int i = 0; i < length; ++i)
    {
        const auto phase = juce::MathConstants<double>::twoPi * hz * (start + i) / sampleRate;
        re += buffer.getSample (0, start + i) * std::cos (phase);
        im += buffer.getSample (0, start + i) * std::sin (phase);
    }

    return 2.0 * std::hypot (re, im) / length;
}

float samplePeak (const juce::AudioBuffer<float>& b, int from = 0)
{
    float peak = 0.0f;

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = from; i < b.getNumSamples(); ++i)
            peak = juce::jmax (peak, std::abs (b.getSample (ch, i)));

    return peak;
}

/** True peak to a finer standard than the plugin's own: 16x, with a long
    windowed sinc, so the check does not mark its own homework. */
float referenceTruePeak (const juce::AudioBuffer<float>& b, int from = 0)
{
    constexpr int factor = 16, halfTaps = 32;
    float peak = 0.0f;

    std::array<std::array<float, 2 * halfTaps>, factor> kernel { };

    for (int p = 0; p < factor; ++p)
    {
        for (int k = 0; k < 2 * halfTaps; ++k)
        {
            const auto t = (double) (k - halfTaps + 1) - (double) p / factor;
            const auto sinc = std::abs (t) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
            const auto w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * t / halfTaps);
            kernel[(size_t) p][(size_t) k] = (float) (sinc * w);
        }
    }

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const auto* x = b.getReadPointer (ch);

        for (int n = juce::jmax (from, halfTaps); n < b.getNumSamples() - halfTaps; ++n)
        {
            for (int p = 0; p < factor; ++p)
            {
                float y = 0.0f;

                for (int k = 0; k < 2 * halfTaps; ++k)
                    y += kernel[(size_t) p][(size_t) k] * x[n + halfTaps - 1 - k];

                peak = juce::jmax (peak, std::abs (y));
            }
        }
    }

    return peak;
}

// ---------------------------------------------------------------------------

void testPeakLimiterCore()
{
    section ("Peak limiter core");

    std::mt19937 rng (99);
    std::uniform_real_distribution<float> uniform (-1.0f, 1.0f);

    struct Case { int lookahead, attack; float release, link; };

    for (const auto& c : { Case { 0, 0, 50.0f, 1.0f }, Case { 48, 48, 100.0f, 1.0f }, Case { 96, 20, 5.0f, 0.0f },
                           Case { 240, 240, 1.0f, 0.5f }, Case { 13, 7, 300.0f, 0.75f } })
    {
        breakwater::PeakLimiter limiter;
        limiter.prepare (48000.0, 480);

        breakwater::PeakLimiter::Settings s;
        s.lookahead = c.lookahead;
        s.attack = c.attack;
        s.releaseMs = c.release;
        s.linkTransients = c.link;
        s.linkRelease = c.link;
        limiter.configure (s);

        float worst = 0.0f;
        std::array<float, 2> in { }, det { }, out { };

        for (int n = 0; n < 200000; ++n)
        {
            // Bursts up to +30 dB over the ceiling, with silent gaps.
            const auto burst = (n / 3000) % 3 == 0 ? 0.0f : 31.6f * std::abs (uniform (rng));

            for (int ch = 0; ch < 2; ++ch)
            {
                in[(size_t) ch] = burst * uniform (rng);
                det[(size_t) ch] = std::abs (in[(size_t) ch]);
            }

            limiter.process (in.data(), det.data(), out.data(), 2);
            worst = juce::jmax (worst, std::abs (out[0]), std::abs (out[1]));
        }

        check (worst <= 1.0f + 1.0e-5f, "never over the ceiling, lookahead " + juce::String (c.lookahead)
                                         + " attack " + juce::String (c.attack) + ": " + juce::String (worst, 7));
        check (limiter.getLatency() == c.lookahead, "latency is the lookahead");
    }

    // Under the ceiling, the core is a pure delay.
    {
        breakwater::PeakLimiter limiter;
        limiter.prepare (48000.0, 100);
        breakwater::PeakLimiter::Settings s;
        s.lookahead = 37;
        s.attack = 20;
        limiter.configure (s);

        std::vector<float> history;
        float worst = 0.0f;
        std::array<float, 2> in { }, det { }, out { };

        for (int n = 0; n < 5000; ++n)
        {
            in[0] = in[1] = 0.9f * uniform (rng);
            det[0] = det[1] = std::abs (in[0]);
            history.push_back (in[0]);
            limiter.process (in.data(), det.data(), out.data(), 2);

            if (n >= 37)
                worst = juce::jmax (worst, std::abs (out[0] - history[(size_t) (n - 37)]));
        }

        check (worst < 1.0e-30f, "a signal under the ceiling passes bit for bit, delayed by the lookahead");
    }
}

void testClipper()
{
    section ("Clipper");

    for (float amount : { 0.1f, 0.5f, 1.0f })
    {
        breakwater::SoftClipper clipper;
        clipper.setAmount (amount);

        float previous = clipper.process (-100.0f);
        bool monotonic = true;

        for (float x = -100.0f; x <= 100.0f; x += 0.001f)
        {
            const auto y = clipper.process (x);
            monotonic = monotonic && y >= previous - 1.0e-6f;
            previous = y;
        }

        check (monotonic, "monotonic at amount " + juce::String (amount));
        check (std::abs (clipper.process (0.3f) - 0.3f) < 1.0e-7f, "linear below the knee");
    }

    breakwater::SoftClipper full;
    full.setAmount (1.0f);
    check (full.process (1000.0f) < 1.0f, "at full amount, never reaches the ceiling");
}

void testTruePeakInterpolator()
{
    section ("True peak interpolator");

    // A sine at a quarter of the sample rate, sampled 45 degrees off its
    // crests: every sample reads 0.707 while the waveform reaches 1.0.
    breakwater::TruePeakInterpolator tp;
    float measured = 0.0f;

    for (int n = 0; n < 4000; ++n)
    {
        const auto x = (float) std::sin (juce::MathConstants<double>::halfPi * n + juce::MathConstants<double>::pi / 4.0);
        const auto v = tp.push (x);

        if (n > 100)
            measured = juce::jmax (measured, v);
    }

    std::cout << "  fs/4 at 45 degrees, samples at -3.01 dB, reads " << db (measured) << " dBTP" << std::endl;
    check (std::abs (db (measured)) < 0.2, "finds the inter-sample peak to 0.2 dB");

    // Phase 0 is the input, delayed.
    breakwater::TruePeakInterpolator delay;
    std::vector<float> history;
    bool exact = true;

    for (int n = 0; n < 200; ++n)
    {
        const auto x = (n % 37 == 0) ? 1.0f : 0.0f;
        history.push_back (x);
        const auto v = delay.push (x);

        // An impulse reads its own height when it arrives at phase 0.
        if (n >= breakwater::TruePeakInterpolator::delay && history[(size_t) (n - breakwater::TruePeakInterpolator::delay)] > 0.5f)
            exact = exact && std::abs (v - 1.0f) < 1.0e-5f;
    }

    check (exact, "phase 0 reproduces the input after " + juce::String (breakwater::TruePeakInterpolator::delay) + " samples");
}

void testCeiling()
{
    section ("The ceiling holds: every engine, style and oversampling factor");

    const auto source = drums();
    const int n = 48000 * 3;

    float worstSample = 0.0f;

    for (int engine = 0; engine < 2; ++engine)
    {
        for (int style = 0; style < (int) breakwater::styles.size(); ++style)
        {
            if (engine == 1 && style > 0)
                break;   // the analog engine has no styles

            for (int os : { 0, 1, 2, 5 })
            {
                for (float ceilingDb : { -0.3f, -6.0f })
                {
                    const Settings settings {
                        { breakwater::pid::engine, (float) engine },  { breakwater::pid::style, (float) style },
                        { breakwater::pid::oversampling, (float) os }, { breakwater::pid::gain, 18.0f },
                        { breakwater::pid::ceiling, ceilingDb },        { breakwater::pid::truePeak, 0.0f },
                        { breakwater::pid::clip, style == 3 ? 60.0f : 0.0f },
                        { breakwater::pid::transients, style == 1 ? 80.0f : 0.0f },
                        { breakwater::pid::lowPunch, style == 2 ? 80.0f : 0.0f },
                        { breakwater::pid::balance, style == 5 ? -100.0f : (style == 6 ? 100.0f : 0.0f) },
                        { breakwater::pid::window, (float) (style % 6) },
                        { breakwater::pid::compOn, style == 7 ? 1.0f : 0.0f },
                        { breakwater::pid::adaptiveLink, style == 0 ? 1.0f : 0.0f }
                    };

                    auto out = render (settings, source, n);
                    const auto peak = samplePeak (out);
                    const auto overDb = db (peak) - ceilingDb;
                    worstSample = juce::jmax (worstSample, (float) overDb);

                    check (overDb <= 1.0e-4,
                           "sample peak under " + juce::String (ceilingDb) + " dB, engine " + juce::String (engine)
                               + " style " + juce::String (style) + " os " + juce::String (os)
                               + ": over by " + juce::String (overDb, 5) + " dB");
                }
            }
        }
    }

    std::cout << "  worst sample peak relative to the ceiling: " << worstSample << " dB" << std::endl;
}

void testTruePeakCeiling()
{
    section ("True peak mode keeps inter-sample peaks under the ceiling");

    const auto source = drums();
    const int n = 48000 * 3;

    for (int engine = 0; engine < 2; ++engine)
    {
        for (int os : { 0, 2 })
        {
            const Settings settings {
                { breakwater::pid::engine, (float) engine }, { breakwater::pid::oversampling, (float) os },
                { breakwater::pid::gain, 15.0f }, { breakwater::pid::ceiling, -1.0f }, { breakwater::pid::truePeak, 1.0f }
            };

            auto out = render (settings, source, n);
            const auto tp = db (referenceTruePeak (out)) + 1.0;

            std::cout << "  engine " << engine << " os " << os << ": true peak " << tp << " dB re ceiling, own meter "
                      << db (lastTruePeakReading) + 1.0 << ", sample peak " << db (samplePeak (out)) + 1.0 << std::endl;
            check (tp <= 0.1, "true peak within 0.1 dB of the ceiling, engine " + juce::String (engine)
                                  + " os " + juce::String (os) + ": " + juce::String (tp, 3));
        }

        // Without true peak mode the same material does cross it between samples,
        // which is what makes the mode worth having.
        const Settings samplesOnly {
            { breakwater::pid::engine, (float) engine }, { breakwater::pid::oversampling, 0.0f },
            { breakwater::pid::gain, 15.0f }, { breakwater::pid::ceiling, -1.0f }, { breakwater::pid::truePeak, 0.0f }
        };

        auto out = render (samplesOnly, source, n);
        check (db (referenceTruePeak (out)) + 1.0 > 0.05, "sample peak mode alone lets inter-sample peaks through");
    }
}

void testTransparency()
{
    section ("Under the ceiling, the signal is untouched");

    const int n = 48000;
    const auto in = drums();
    const auto quiet = [in] (int i) { auto [l, r] = in (i); return std::pair { l * 0.1f, r * 0.1f }; };

    for (int style = 0; style < (int) breakwater::styles.size(); ++style)
    {
        const Settings settings { { breakwater::pid::oversampling, 0.0f }, { breakwater::pid::ceiling, 0.0f },
                                  { breakwater::pid::style, (float) style } };
        auto out = render (settings, quiet, n);

        float worst = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                const auto [l, r] = quiet (i);
                worst = juce::jmax (worst, std::abs (out.getSample (ch, i) - (ch == 0 ? l : r)));
            }

        check (worst < 1.0e-6f, "bit transparent without oversampling, style " + juce::String (style)
                                    + ": " + juce::String (worst, 9));
    }

    // With oversampling, what is left is the filters' passband ripple and
    // float rounding.
    for (int os : { 1, 2, 3, 4, 5 })
    {
        const Settings settings { { breakwater::pid::oversampling, (float) os }, { breakwater::pid::ceiling, 0.0f } };
        auto out = render (settings, tone (1000.0, 0.25f), n);
        const auto ref = tone (1000.0, 0.25f);

        double error = 0.0, signal = 0.0;

        for (int i = 4800; i < n; ++i)
        {
            const auto x = (double) ref (i).first;
            error  += std::pow (out.getSample (0, i) - x, 2.0);
            signal += x * x;
        }

        const auto residual = 10.0 * std::log10 (error / signal);
        std::cout << "  oversampling " << (1 << os) << "x: residual " << residual << " dB, gain "
                  << db (amplitudeAt (out, 1000.0, 4800, 43200) / 0.25) << " dB" << std::endl;
        check (residual < -90.0, "oversampling " + juce::String (1 << os) + "x nulls below -90 dB: " + juce::String (residual, 1));
    }

    // Gain is gain.
    {
        const Settings settings { { breakwater::pid::oversampling, 0.0f }, { breakwater::pid::ceiling, 0.0f },
                                  { breakwater::pid::gain, 6.0f } };
        auto out = render (settings, tone (440.0, 0.1f), n);
        const auto g = db (samplePeak (out, 24000) / 0.1);
        check (std::abs (g - 6.0) < 0.05, "+6 dB of gain under the ceiling is +6 dB: " + juce::String (g, 3));
    }
}

void testLatency()
{
    section ("Reported latency is the real latency");

    for (int engine = 0; engine < 2; ++engine)
    {
        for (int os : { 0, 1, 3, 5 })
        {
            for (float lookahead : { 0.0f, 1.0f, 5.0f })
            {
                // An impulse well under the ceiling, rendered with the reported
                // latency removed, must land where it was sent.
                const auto impulse = [] (int i) { return i == 1000 ? std::pair { 0.5f, 0.5f } : std::pair { 0.0f, 0.0f }; };

                int latency = 0;
                auto out = render ({ { breakwater::pid::engine, (float) engine }, { breakwater::pid::oversampling, (float) os },
                                     { breakwater::pid::lookahead, lookahead }, { breakwater::pid::ceiling, 0.0f } },
                                   impulse, 4000, 48000.0, 512, &latency);

                int at = 0;
                float best = 0.0f;
                for (int i = 0; i < out.getNumSamples(); ++i)
                    if (std::abs (out.getSample (0, i)) > best)
                    {
                        best = std::abs (out.getSample (0, i));
                        at = i;
                    }

                check (at == 1000, "engine " + juce::String (engine) + " os " + juce::String (1 << os) + "x lookahead "
                                       + juce::String (lookahead) + " ms: impulse at " + juce::String (at)
                                       + " (latency " + juce::String (latency) + ")");
            }
        }
    }

    for (int os = 0; os < breakwater::numOversamplingChoices; ++os)
    {
        int latency = 0;
        render ({ { breakwater::pid::oversampling, (float) os } }, tone (100.0, 0.1f), 10, 48000.0, 512, &latency);
        std::cout << "  " << (1 << os) << "x, default lookahead: " << latency << " samples" << std::endl;
    }

    int modernLatency = 0, analogLatency = 0;
    render ({ { breakwater::pid::engine, 0.0f }, { breakwater::pid::lookahead, 5.0f } }, tone (100.0, 0.1f), 10, 48000.0, 512, &modernLatency);
    render ({ { breakwater::pid::engine, 1.0f }, { breakwater::pid::lookahead, 5.0f } }, tone (100.0, 0.1f), 10, 48000.0, 512, &analogLatency);
    check (analogLatency + 240 == modernLatency, "the analog engine takes no lookahead");
}

void testBlockSizeIndependence()
{
    section ("Block size does not change the result");

    const Settings settings { { breakwater::pid::gain, 12.0f }, { breakwater::pid::transients, 50.0f },
                              { breakwater::pid::detail, 50.0f }, { breakwater::pid::clip, 30.0f } };

    auto a = render (settings, drums(), 48000, 48000.0, 512);
    auto b = render (settings, drums(), 48000, 48000.0, 61);

    float worst = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < a.getNumSamples(); ++i)
            worst = juce::jmax (worst, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));

    check (worst < 1.0e-5f, "512 and 61 sample blocks agree: " + juce::String (worst, 8));
}

void testDelta()
{
    section ("Delta is what the limiter removed");

    const Settings base { { breakwater::pid::gain, 10.0f }, { breakwater::pid::oversampling, 0.0f } };
    auto wet = render (base, drums(), 48000);

    auto withDelta = base;
    withDelta[breakwater::pid::delta] = 1.0f;
    auto delta = render (withDelta, drums(), 48000);

    const auto source = drums();
    const auto drive = juce::Decibels::decibelsToGain (10.0f);
    float worst = 0.0f;

    for (int i = 0; i < 48000; ++i)
    {
        const auto [l, r] = source (i);
        worst = juce::jmax (worst, std::abs (wet.getSample (0, i) + delta.getSample (0, i) - l * drive));
    }

    check (worst < 1.0e-4f, "output plus delta is the driven input: " + juce::String (worst, 7));
}

void testLoudness()
{
    section ("Loudness to BS.1770 and EBU 3342");

    breakwater::BreakwaterProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    apply (processor, { { breakwater::pid::ceiling, 0.0f }, { breakwater::pid::oversampling, 0.0f } });
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    const auto source = tone (997.0, 0.1f);   // -20 dBFS on both channels

    for (int start = 0; start < 48000 * 10; start += 512)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto [l, r] = source (start + i);
            block.setSample (0, i, l);
            block.setSample (1, i, r);
        }

        processor.processBlock (block, midi);
    }

    const auto integrated = processor.meters.integrated.load();
    const auto shortTerm  = processor.meters.shortTerm.load();
    const auto range      = processor.meters.range.load();
    const auto tpMax      = processor.meters.truePeakMax.load();

    std::cout << "  -20 dBFS 997 Hz stereo: I " << integrated << " LUFS, S " << shortTerm
              << " LUFS, LRA " << range << " LU, TP " << db (tpMax) << " dBTP" << std::endl;

    check (std::abs (integrated + 20.0f) < 0.15f, "integrated reads -20 LUFS");
    check (std::abs (shortTerm + 20.0f) < 0.15f, "short term reads -20 LUFS");
    check (range < 0.2f, "a steady tone has no loudness range");
    check (std::abs (db (tpMax) + 20.0) < 0.1, "true peak reads -20 dBTP");
}

void testEnhancers()
{
    section ("Transients and Detail do what they say");

    const auto crest = [] (const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;
        for (int i = 0; i < b.getNumSamples(); ++i)
            sum += (double) b.getSample (0, i) * b.getSample (0, i);

        return db (samplePeak (b) / std::sqrt (sum / b.getNumSamples()));
    };

    const Settings base { { breakwater::pid::gain, 0.0f }, { breakwater::pid::ceiling, 0.0f }, { breakwater::pid::oversampling, 0.0f } };
    const auto quiet = [src = drums()] (int i) { auto [l, r] = src (i); return std::pair { l * 0.25f, r * 0.25f }; };

    auto plain = render (base, quiet, 96000);

    auto punchy = base;
    punchy[breakwater::pid::transients] = 100.0f;
    auto withPunch = render (punchy, quiet, 96000);

    auto soft = base;
    soft[breakwater::pid::transients] = -100.0f;
    auto withSoft = render (soft, quiet, 96000);

    std::cout << "  crest factor: plain " << crest (plain) << " dB, +100 % " << crest (withPunch)
              << " dB, -100 % " << crest (withSoft) << " dB" << std::endl;

    check (crest (withPunch) > crest (plain) + 1.0, "positive transients raise the crest factor");
    check (crest (withSoft) < crest (plain) - 1.0, "negative transients lower it");

    // Detail: a loud burst every half second over a quiet tone. The quiet
    // part, measured between the bursts, must come up.
    const auto burstAndTail = [] (int i)
    {
        const auto t = i / 48000.0;
        const auto phase = std::fmod (t, 0.5);
        const auto twoPi = juce::MathConstants<double>::twoPi;
        const auto v = phase < 0.1 ? 0.4 * std::sin (twoPi * 300.0 * t) : 0.01 * std::sin (twoPi * 1000.0 * t);
        return std::pair { (float) v, (float) v };
    };

    const auto quietLevel = [] (const juce::AudioBuffer<float>& b)
    {
        double sum = 0.0;
        int count = 0;

        for (int i = 48000; i < b.getNumSamples(); ++i)
        {
            const auto phase = std::fmod (i / 48000.0, 0.5);

            if (phase > 0.25 && phase < 0.45)
            {
                sum += (double) b.getSample (0, i) * b.getSample (0, i);
                ++count;
            }
        }

        return 10.0 * std::log10 (sum / count);
    };

    auto detailed = base;
    detailed[breakwater::pid::detail] = 100.0f;

    const auto lift = quietLevel (render (detailed, burstAndTail, 96000)) - quietLevel (render (base, burstAndTail, 96000));
    std::cout << "  full detail lifts the quiet part by " << lift << " dB" << std::endl;
    check (lift > 2.0 && lift <= 6.01, "detail lifts the quiet parts, by at most 6 dB");

    // Silence stays silent.
    auto silent = render (detailed, [] (int) { return std::pair { 0.0f, 0.0f }; }, 4800);
    check (samplePeak (silent) < 1.0e-30f, "detail does not lift silence");
}

void testDither()
{
    section ("Word length");

    auto out = render ({ { breakwater::pid::wordLength, 3.0f }, { breakwater::pid::dither, 2.0f } }, tone (1000.0, 0.3f), 4800);

    bool onGrid = true;
    for (int i = 0; i < out.getNumSamples(); ++i)
    {
        const auto v = out.getSample (0, i) * 32768.0f;
        onGrid = onGrid && std::abs (v - std::round (v)) < 1.0e-3f;
    }

    check (onGrid, "16 bit output lands on the 16 bit grid");
}

void testRobustness()
{
    section ("Robustness");

    // Changing the structure mid-stream: no NaN, nothing over the ceiling.
    breakwater::BreakwaterProcessor processor;
    processor.setPlayConfigDetails (2, 2, 44100.0, 256);
    apply (processor, { { breakwater::pid::gain, 20.0f }, { breakwater::pid::ceiling, -0.1f }, { breakwater::pid::truePeak, 0.0f } });
    processor.prepareToPlay (44100.0, 256);

    const auto source = drums (44100.0);
    juce::AudioBuffer<float> block (2, 256);
    juce::MidiBuffer midi;
    float worst = 0.0f;
    bool finite = true;

    for (int b = 0; b < 600; ++b)
    {
        if (b % 50 == 0)
            apply (processor, { { breakwater::pid::oversampling, (float) ((b / 50) % 5) },
                                { breakwater::pid::engine, (float) ((b / 100) % 2) },
                                { breakwater::pid::lookahead, (float) ((b / 50) % 7) },
                                { breakwater::pid::attack, 0.05f + (float) ((b / 25) % 4) },
                                { breakwater::pid::style, (float) ((b / 30) % 8) } });

        for (int i = 0; i < 256; ++i)
        {
            const auto [l, r] = source (b * 256 + i);
            block.setSample (0, i, l);
            block.setSample (1, i, r);
        }

        processor.processBlock (block, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 256; ++i)
            {
                const auto v = block.getSample (ch, i);
                finite = finite && std::isfinite (v);
                worst = juce::jmax (worst, std::abs (v));
            }
    }

    check (finite, "no NaN or infinity through structure changes");
    check (db (worst) <= -0.1 + 1.0e-4, "ceiling holds through structure changes: " + juce::String (db (worst), 4));

    // Mono.
    {
        breakwater::BreakwaterProcessor mono;
        mono.setPlayConfigDetails (1, 1, 48000.0, 128);
        apply (mono, { { breakwater::pid::gain, 12.0f }, { breakwater::pid::ceiling, -1.0f } });
        mono.prepareToPlay (48000.0, 128);

        juce::AudioBuffer<float> m (1, 128);
        float peak = 0.0f;

        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < 128; ++i)
                m.setSample (0, i, source (b * 128 + i).first);

            mono.processBlock (m, midi);
            peak = juce::jmax (peak, m.getMagnitude (0, 0, 128));
        }

        check (db (peak) <= -1.0 + 1.0e-4, "mono holds the ceiling");
    }

    // Other sample rates.
    for (double rate : { 44100.0, 96000.0, 192000.0 })
    {
        auto out = render ({ { breakwater::pid::gain, 15.0f }, { breakwater::pid::ceiling, -0.5f }, { breakwater::pid::truePeak, 0.0f } },
                           drums (rate), (int) rate, rate);
        check (db (samplePeak (out)) <= -0.5 + 1.0e-4, "ceiling holds at " + juce::String (rate) + " Hz");
    }
}

void testCompressor()
{
    section ("Compressor");

    // Stereo: a -6 dBFS tone against a -20 dB threshold at 4:1 loses about
    // three quarters of its 14 dB over.
    {
        const Settings settings { { breakwater::pid::ceiling, 0.0f }, { breakwater::pid::compOn, 1.0f },
                                  { breakwater::pid::compThreshold, -20.0f }, { breakwater::pid::compRatio, 4.0f },
                                  { breakwater::pid::oversampling, 0.0f } };
        auto out = render (settings, tone (1000.0, 0.5f), 48000);
        const auto change = db (amplitudeAt (out, 1000.0, 24000, 24000) / 0.5);

        std::cout << "  -6 dBFS into -20 dB at 4:1: " << change << " dB" << std::endl;
        check (change < -9.0 && change > -12.0, "4:1 takes about three quarters of the over");

        auto off = settings;
        off[breakwater::pid::compOn] = 0.0f;
        auto untouched = render (off, tone (1000.0, 0.5f), 48000);
        check (std::abs (db (amplitudeAt (untouched, 1000.0, 24000, 24000) / 0.5)) < 0.01, "switched off, it does nothing");
    }

    // Mid/side: a loud side and a quiet mid. Only the side is compressed.
    {
        const auto source = [] (int n)
        {
            const auto t = n / 48000.0;
            const auto mid  = 0.03 * std::sin (juce::MathConstants<double>::twoPi * 500.0 * t);
            const auto side = 0.4  * std::sin (juce::MathConstants<double>::twoPi * 3000.0 * t);
            return std::pair { (float) (mid + side), (float) (mid - side) };
        };

        const Settings settings { { breakwater::pid::ceiling, 0.0f }, { breakwater::pid::compOn, 1.0f },
                                  { breakwater::pid::compThreshold, -20.0f }, { breakwater::pid::compRatio, 4.0f },
                                  { breakwater::pid::compMode, 1.0f }, { breakwater::pid::oversampling, 0.0f } };
        auto out = render (settings, source, 48000);

        juce::AudioBuffer<float> mid (1, 48000), sideOnly (1, 48000);
        for (int i = 0; i < 48000; ++i)
        {
            mid.setSample (0, i, 0.5f * (out.getSample (0, i) + out.getSample (1, i)));
            sideOnly.setSample (0, i, 0.5f * (out.getSample (0, i) - out.getSample (1, i)));
        }

        const auto midChange  = db (amplitudeAt (mid, 500.0, 24000, 24000) / 0.03);
        const auto sideChange = db (amplitudeAt (sideOnly, 3000.0, 24000, 24000) / 0.4);

        std::cout << "  mid/side: mid " << midChange << " dB, side " << sideChange << " dB" << std::endl;
        check (std::abs (midChange) < 0.1, "the quiet mid is left alone");
        check (sideChange < -6.0, "the loud side is compressed");
    }
}

void testLowPunchAndBalance()
{
    section ("Low Punch and Balance");

    // Low band attack against body: on every beat, the peak of the low band
    // in the first 15 ms against its RMS from 60 to 200 ms.
    const auto punchOfLows = [] (const juce::AudioBuffer<float>& b)
    {
        std::vector<double> low ((size_t) b.getNumSamples());
        double a = 0.0, c = 0.0;
        const auto k = std::exp (-juce::MathConstants<double>::twoPi * 150.0 / 48000.0);

        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            a = b.getSample (0, i) + k * (a - b.getSample (0, i));
            c = a + k * (c - a);
            low[(size_t) i] = c;
        }

        double total = 0.0;
        int beats = 0;

        for (int beat = 24000; beat + 9600 < b.getNumSamples(); beat += 24000, ++beats)
        {
            double peak = 0.0, sum = 0.0;

            for (int i = 0; i < 720; ++i)
                peak = juce::jmax (peak, std::abs (low[(size_t) (beat + i)]));

            for (int i = 2880; i < 9600; ++i)
                sum += low[(size_t) (beat + i)] * low[(size_t) (beat + i)];

            total += db (peak / std::sqrt (sum / 6720.0));
        }

        return total / beats;
    };

    const Settings base { { breakwater::pid::ceiling, 0.0f }, { breakwater::pid::oversampling, 0.0f } };

    // A kick on every beat with a short hat over it: the low band is the kick alone.
    const auto quiet = [] (int n)
    {
        const auto t     = n / 48000.0;
        const auto beat  = std::fmod (t, 0.5);
        const auto twoPi = juce::MathConstants<double>::twoPi;
        const auto kick  = 0.25 * std::exp (-beat * 12.0) * std::sin (twoPi * (50.0 + 120.0 * std::exp (-beat * 40.0)) * beat);
        const auto hat   = 0.02 * std::sin (twoPi * 9000.0 * t) * std::exp (-std::fmod (t, 0.125) * 60.0);
        return std::pair { (float) (kick + hat), (float) (kick - hat) };
    };

    auto plain = render (base, quiet, 144000);
    auto up = base;   up[breakwater::pid::lowPunch] = 100.0f;
    auto down = base; down[breakwater::pid::lowPunch] = -100.0f;

    const auto c0 = punchOfLows (plain), cUp = punchOfLows (render (up, quiet, 144000)), cDown = punchOfLows (render (down, quiet, 144000));
    std::cout << "  low band attack over body: plain " << c0 << " dB, +100 % " << cUp << " dB, -100 % " << cDown << " dB" << std::endl;
    check (cUp > c0 + 1.0, "positive Low Punch brings the kick's attack forward");
    check (cDown < c0 - 1.0, "negative Low Punch softens it");

    // Balance under heavy limiting: towards Lows, the top end keeps more level.
    const auto topEnergy = [] (const juce::AudioBuffer<float>& b)
    {
        double a = 0.0, sum = 0.0;
        const auto k = std::exp (-juce::MathConstants<double>::twoPi * 2000.0 / 48000.0);

        for (int i = 24000; i < b.getNumSamples(); ++i)
        {
            a = b.getSample (0, i) + k * (a - b.getSample (0, i));
            const auto high = b.getSample (0, i) - a;
            sum += high * high;
        }

        return 10.0 * std::log10 (sum);
    };

    const Settings loud { { breakwater::pid::gain, 12.0f }, { breakwater::pid::ceiling, -1.0f } };
    auto lows = loud;  lows[breakwater::pid::balance] = -100.0f;
    auto highs = loud; highs[breakwater::pid::balance] = 100.0f;

    const auto eCentre = topEnergy (render (loud, drums(), 96000));
    const auto eLows   = topEnergy (render (lows, drums(), 96000));
    const auto eHighs  = topEnergy (render (highs, drums(), 96000));

    std::cout << "  energy above 2 kHz: centre " << eCentre << ", lows take it " << eLows << ", highs take it " << eHighs << " dB" << std::endl;
    check (eLows > eCentre + 1.0, "towards Lows, the top keeps more level");
    check (eHighs < eCentre - 1.0, "towards Highs, the top gives more up");
}

void testWindow()
{
    section ("Window");

    // A 50 Hz sine driven 9 dB into the analog engine with a short release:
    // without a window the detector rides the waveform and distorts it.
    const auto third = [] (int window)
    {
        const Settings settings { { breakwater::pid::engine, 1.0f }, { breakwater::pid::gain, 9.0f },
                                  { breakwater::pid::ceiling, 0.0f }, { breakwater::pid::release, 5.0f },
                                  { breakwater::pid::window, (float) window }, { breakwater::pid::oversampling, 0.0f },
                                  { breakwater::pid::truePeak, 0.0f } };
        auto out = render (settings, tone (50.0, 0.5f), 96000);
        return db (amplitudeAt (out, 150.0, 48000, 48000) / amplitudeAt (out, 50.0, 48000, 48000));
    };

    const auto off = third (0), c0 = third (5);
    std::cout << "  3rd harmonic of 50 Hz: window off " << off << " dBc, C0 " << c0 << " dBc" << std::endl;
    check (c0 < off - 6.0, "a C0 window cuts the distortion on a bass note");
}

void testAdaptiveLink()
{
    section ("Adaptive link");

    breakwater::BreakwaterProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    apply (processor, { { breakwater::pid::linkTransients, 0.0f }, { breakwater::pid::adaptiveLink, 1.0f } });
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    const auto source = tone (300.0, 0.3f);

    for (int start = 0; start < 48000; start += 512)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto [l, r] = source (start + i);
            block.setSample (0, i, l);
            block.setSample (1, i, r);
        }

        processor.processBlock (block, midi);
    }

    std::cout << "  identical channels: correlation " << processor.meters.correlation.load()
              << ", link in use " << processor.meters.link.load() << std::endl;
    check (processor.meters.link.load() > 0.95f, "identical channels are fully linked whatever the control says");
}

void testSidechain()
{
    section ("External sidechain");

    breakwater::BreakwaterProcessor processor;

    auto layout = processor.getBusesLayout();
    layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    check (processor.setBusesLayout (layout), "accepts a stereo sidechain");

    processor.setRateAndBufferSizeDetails (48000.0, 512);
    apply (processor, { { breakwater::pid::sidechain, 1.0f }, { breakwater::pid::ceiling, 0.0f },
                        { breakwater::pid::oversampling, 0.0f } });
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> block (4, 512);
    juce::MidiBuffer midi;
    const auto quiet = tone (1000.0, 0.1f);
    const auto key   = tone (80.0, 4.0f);   // 12 dB over the ceiling

    float quietPeak = 0.0f, keyedPeak = 0.0f;

    for (int b = 0; b < 200; ++b)
    {
        const auto keyed = b >= 100;

        for (int i = 0; i < 512; ++i)
        {
            const auto n = b * 512 + i;
            block.setSample (0, i, quiet (n).first);
            block.setSample (1, i, quiet (n).first);
            block.setSample (2, i, keyed ? key (n).first : 0.0f);
            block.setSample (3, i, keyed ? key (n).first : 0.0f);
        }

        processor.processBlock (block, midi);

        if (b >= 50 && b < 100)  quietPeak = juce::jmax (quietPeak, block.getMagnitude (0, 0, 512));
        if (b >= 150)            keyedPeak = juce::jmax (keyedPeak, block.getMagnitude (0, 0, 512));
    }

    std::cout << "  programme at -20 dBFS: " << db (quietPeak) << " dB alone, " << db (keyedPeak) << " dB keyed" << std::endl;
    check (std::abs (db (quietPeak) + 20.0) < 0.1, "without a key, the programme is untouched");
    check (db (keyedPeak) < db (quietPeak) - 6.0, "a loud key turns the programme down");
}

void testVu()
{
    section ("VU");

    breakwater::BreakwaterProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    apply (processor, { { breakwater::pid::ceiling, 0.0f } });
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    const auto source = tone (1000.0, juce::Decibels::decibelsToGain (-18.0f));

    for (int start = 0; start < 48000 * 2; start += 512)
    {
        for (int i = 0; i < 512; ++i)
        {
            const auto [l, r] = source (start + i);
            block.setSample (0, i, l);
            block.setSample (1, i, r);
        }

        processor.processBlock (block, midi);
    }

    const auto vu = processor.meters.vu.load();
    std::cout << "  -18 dBFS sine reads " << vu << " VU" << std::endl;
    check (std::abs (vu) < 0.2f, "a -18 dBFS sine reads 0 VU");
}

void testPerformance()
{
    section ("Speed");

    for (int os : { 0, 2, 4, 5 })
    {
        const auto seconds = 10;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        render ({ { breakwater::pid::oversampling, (float) os }, { breakwater::pid::gain, 12.0f },
                  { breakwater::pid::transients, 40.0f }, { breakwater::pid::clip, 30.0f } },
                drums(), 48000 * seconds);
        const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;

        std::cout << "  " << (1 << os) << "x: " << juce::String (seconds / elapsed, 1) << " times real time, stereo 48 kHz" << std::endl;
        check (seconds / elapsed > (os == 5 ? 1.0 : 2.0), "fast enough at " + juce::String (1 << os) + "x");
    }
}

void testState()
{
    section ("State");

    breakwater::BreakwaterProcessor a;
    apply (a, { { breakwater::pid::gain, 7.5f }, { breakwater::pid::style, 6.0f }, { breakwater::pid::release, 333.0f } });

    juce::MemoryBlock state;
    a.getStateInformation (state);

    breakwater::BreakwaterProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    for (auto* id : { breakwater::pid::gain, breakwater::pid::style, breakwater::pid::release })
        check (std::abs (a.apvts.getParameter (id)->getValue() - b.apvts.getParameter (id)->getValue()) < 1.0e-6f,
               juce::String ("restores ") + id);
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testPeakLimiterCore();
    testClipper();
    testTruePeakInterpolator();
    testCeiling();
    testTruePeakCeiling();
    testTransparency();
    testLatency();
    testBlockSizeIndependence();
    testDelta();
    testLoudness();
    testEnhancers();
    testDither();
    testRobustness();
    testCompressor();
    testLowPunchAndBalance();
    testWindow();
    testAdaptiveLink();
    testSidechain();
    testVu();
    testPerformance();
    testState();

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
