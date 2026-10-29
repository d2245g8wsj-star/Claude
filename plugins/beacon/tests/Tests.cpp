/*
    Offline checks for the claims Beacon makes about itself.

    A compressor modelled on hardware is easy to break silently: a ratio that
    stops rising with level, a THD knob whose numbers drift from what the stage
    produces, an idle gain cell that is not quite a wire, a linked pair that
    lets the image wander. Each is asserted here.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/OutputStage.h"
#include "dsp/Tube.h"
#include "dsp/VariMu.h"

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
    beacon::BeaconProcessor processor;
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



/** Settings with every switchable colour stage out: GREEN output, no THD,
    tube saturation off. What is left is the input iron and the compressor. */
Settings clean (Settings extra = {})
{
    for (int s = 0; s < 2; ++s)
    {
        extra.emplace (beacon::pid::tubeIn, 0.0f);
        extra.emplace (beacon::pid::iron,   0.0f);
        extra.emplace (beacon::pid::thd,    0.0f);
    }

    return extra;
}

/** The threshold control for a threshold in dBFS. */
float th (float dbfs) { return dbfs - beacon::range::ceilingDbfs; }

const float zeroDbu = juce::Decibels::decibelsToGain (beacon::level::dbfsAtZeroDbu);

/** Harmonics 2 to 10 against the fundamental, in percent. */
double thdOf (const juce::AudioBuffer<float>& buffer, int channel, double hz, int start, int length)
{
    const auto fundamental = amplitudeAt (buffer, channel, hz, start, length);
    double sum = 0.0;

    for (int h = 2; h <= 10; ++h)
        if (hz * h < 20000.0)
            sum += std::pow (amplitudeAt (buffer, channel, hz * h, start, length), 2.0);

    return 100.0 * std::sqrt (sum) / fundamental;
}

double residualAgainst (const juce::AudioBuffer<float>& out, const std::function<std::pair<float, float> (int)>& in,
                        int from = 4800)
{
    double error = 0.0, signal = 0.0;

    for (int i = from; i < out.getNumSamples(); ++i)
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto x = ch == 0 ? in (i).first : in (i).second;
            error  += std::pow (out.getSample (ch, i) - x, 2.0);
            signal += x * x;
        }

    return 10.0 * std::log10 (error / signal + 1.0e-30);
}

/** Steady state gain reduction for a 1 kHz tone at a level, in dB. */
double steadyReduction (Settings settings, float levelDbfs)
{
    const auto amplitude = juce::Decibels::decibelsToGain (levelDbfs);
    auto out = render (settings, tone (1000.0, amplitude), 96000);
    return -db (amplitudeAt (out, 0, 1000.0, 72000, 24000) / amplitude);
}

/** Gain reduction meter readings, one per millisecond, for a source. */
std::vector<float> reductionTrace (const Settings& settings, const std::function<std::pair<float, float> (int)>& source,
                                   int milliseconds)
{
    beacon::BeaconProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 48);

    for (const auto& [id, value] : settings)
        processor.apvts.getParameter (id)->setValueNotifyingHost (processor.apvts.getParameter (id)->convertTo0to1 (value));

    processor.prepareToPlay (48000.0, 48);

    juce::AudioBuffer<float> block (2, 48);
    juce::MidiBuffer midi;
    std::vector<float> trace;

    for (int ms = 0; ms < milliseconds; ++ms)
    {
        for (int i = 0; i < 48; ++i)
        {
            const auto [l, r] = source (ms * 48 + i);
            block.setSample (0, i, l);
            block.setSample (1, i, r);
        }

        processor.processBlock (block, midi);
        trace.push_back (beacon::MeterSource::take (processor.meters.channels[0].reductionDb));
    }

    return trace;
}

/** Milliseconds from `from` until the trace first crosses half way between
    its value there and its value at `settled`. */
int halfwayTime (const std::vector<float>& trace, int from, int settled)
{
    const auto start = trace[(size_t) from], end = trace[(size_t) settled];
    const auto half  = 0.5f * (start + end);

    for (int i = from; i < settled; ++i)
        if ((end > start && trace[(size_t) i] >= half) || (end < start && trace[(size_t) i] <= half))
            return i - from;

    return settled - from;
}

// ---------------------------------------------------------------------------

void testCurves()
{
    section ("Curves and calibration");

    const auto slope = (beacon::tube::triode (1.0e-4f) - beacon::tube::triode (-1.0e-4f)) / 2.0e-4f;
    check (std::abs (slope - 1.0f) < 1.0e-3f, "triode has unity slope at zero");
    check (std::abs (beacon::tube::triode (2.0f) + beacon::tube::triode (-2.0f)) > 0.05f, "triode is asymmetric");

    for (float percent : { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f })
    {
        const auto drive = beacon::tube::ThdCalibration::get().driveFor (percent);
        const auto back  = beacon::tube::thdPercent (drive, beacon::level::nominalAmplitude());
        check (std::abs (back - percent) < 0.02 * percent,
               "THD table inverts at " + juce::String (percent) + " %, got " + juce::String (back, 3));
    }

    check (beacon::tube::ThdCalibration::get().driveFor (0.0f) <= 0.0f, "0 % is no drive at all");

    // The law: no reduction at the threshold, and the ratio rises past it.
    using namespace beacon::varimu;
    check (std::abs (reductionForExcess (compress, 0.0f)) < 1.0e-9f, "no reduction at the threshold");

    const auto& table = ForwardTable::forCompress();
    float previous = 0.0f;
    bool monotonic = true;

    for (float over = 0.0f; over < 60.0f; over += 0.1f)
    {
        const auto gr = table.reductionAt (over);
        monotonic = monotonic && gr >= previous - 1.0e-4f && over - gr >= -1.0e-3f;
        previous = gr;
    }

    check (monotonic, "feed-forward table rises, and never past the input (no overcompression)");
}

void testTransparentWhenIdle()
{
    section ("Idle: the response and distortion of the unit");

    const Settings idle = clean ({ { beacon::pid::threshold, 0.0f }, { beacon::pid::threshold, 0.0f } });

    for (double hz : { 50.0, 1000.0, 10000.0, 18000.0 })
    {
        auto o = render (idle, tone (hz, 0.1f), 96000);
        const auto g = db (amplitudeAt (o, 0, hz, 48000, 48000) / 0.1);
        check (std::abs (g) < 0.3, "flat to 0.3 dB at " + juce::String (hz) + " Hz, got " + juce::String (g, 3));
    }

    {
        // Specified: 12 Hz to 150 kHz at -3 dB.
        auto o = render (idle, tone (12.0, 0.1f), 192000);
        const auto g = db (amplitudeAt (o, 0, 12.0, 96000, 96000) / 0.1);
        std::cout << "  12 Hz: " << g << " dB" << std::endl;
        check (g < -1.0 && g > -4.5, "about -3 dB at 12 Hz, as specified");
    }

    // Specified: THD+N below 0.01 % at 1 kHz, 0 dBu, with the tube in circuit
    // as it always is on the unit.
    {
        const Settings defaults { { beacon::pid::threshold, 0.0f } };
        auto o = render (defaults, tone (1000.0, zeroDbu), 48000);
        const auto t = thdOf (o, 0, 1000.0, 24000, 24000);
        std::cout << "  THD at 1 kHz, 0 dBu, tube in: " << t << " %" << std::endl;
        check (t < 0.01, "below 0.01 % at 0 dBu, as specified");
    }

    auto o = render (idle, tone (1000.0, beacon::level::nominalAmplitude()), 48000);
    const auto t = thdOf (o, 0, 1000.0, 24000, 24000);
    check (t < 0.002, "tube out, no reduction: next to no distortion, got " + juce::String (t, 5) + " %");
}

void testStaticCurve()
{
    section ("The ratio is a property of level");

    for (bool forward : { false, true })
    {
        std::cout << "  " << (forward ? "feed-forward" : "feedback") << ", threshold -30 dBFS:" << std::endl;

        const Settings base = clean ({ { beacon::pid::threshold, th (-30.0f) },
                                       { beacon::pid::attack, 10.0f },
                                       { beacon::pid::release, 0.3f },
                                       { beacon::pid::feedFwd, forward ? 1.0f : 0.0f } });

        double previousOut = -200.0, previousIn = 0.0;
        std::vector<double> ratios;

        for (float level = -30.0f; level <= 0.0f; level += 3.0f)
        {
            const auto gr  = steadyReduction (base, level);
            const auto out = level - gr;

            std::cout << "    in " << level << " dBFS  GR " << juce::String (gr, 2) << " dB";

            if (level > -30.0f)
            {
                const auto ratio = (level - previousIn) / juce::jmax (1.0e-3, out - previousOut);
                ratios.push_back (ratio);
                std::cout << "  local ratio " << juce::String (ratio, 2) << ":1";
                check (out > previousOut, "output still rises with input at " + juce::String (level));
            }

            std::cout << std::endl;
            previousOut = out;
            previousIn  = level;
        }

        check (ratios.front() < 2.0, "gentle at the threshold");
        check (ratios.back() > 2.5, "steeper well past it");
        check (ratios.back() > ratios.front() + 1.0, "ratio rises with level");
    }

    // Both topologies share one static curve.
    for (float level : { -24.0f, -18.0f, -9.0f })
    {
        const Settings fb = clean ({ { beacon::pid::threshold, th (-30.0f) }, { beacon::pid::release, 0.3f } });
        auto ff = fb;
        ff[beacon::pid::feedFwd] = 1.0f;

        const auto a = steadyReduction (fb, level), b = steadyReduction (ff, level);
        check (std::abs (a - b) < 1.5, "FF and FB agree within 1.5 dB at " + juce::String (level)
                                           + ": " + juce::String (a, 2) + " / " + juce::String (b, 2));
    }

    // Limit is harder at every level past the threshold.
    {
        const Settings comp = clean ({ { beacon::pid::threshold, th (-30.0f) }, { beacon::pid::release, 0.3f } });
        auto lim = comp;
        lim[beacon::pid::limit] = 1.0f;

        const auto a = steadyReduction (comp, -15.0f), b = steadyReduction (lim, -15.0f);
        const auto lo = steadyReduction (lim, -18.0f), hi = steadyReduction (lim, -6.0f);
        const auto ratio = 12.0 / juce::jmax (1.0e-3, 12.0 - (hi - lo));

        std::cout << "  15 dB over: compress " << a << " dB, limit " << b << " dB; limit ratio 12 to 24 dB over: "
                  << ratio << ":1" << std::endl;
        check (b > a + 3.0, "limit reduces more than compress");
        check (ratio > 6.0, "limit ratio past 6:1 well over the threshold");
    }
}

void testTiming()
{
    section ("Attack and release");

    auto stepped = [] (int onsetMs, int offsetMs)
    {
        return [=] (int n)
        {
            const auto ms = n / 48;
            const auto a  = ms >= onsetMs && ms < offsetMs ? 0.5f : 0.01f;
            const auto v  = a * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * n / 48000.0);
            return std::pair { v, v };
        };
    };

    auto attackHalf = [&] (float attackMs, bool forward)
    {
        const auto trace = reductionTrace (clean ({ { beacon::pid::threshold, th (-30.0f) },
                                                    { beacon::pid::attack, attackMs },
                                                    { beacon::pid::feedFwd, forward ? 1.0f : 0.0f } }),
                                           stepped (100, 100000), 1100);
        return halfwayTime (trace, 100, 1000);
    };

    for (bool forward : { false, true })
    {
        const auto fast = attackHalf (2.0f, forward), slow = attackHalf (50.0f, forward);
        std::cout << "  " << (forward ? "FF" : "FB") << " attack 2 ms: half way in " << fast
                  << " ms; 50 ms: " << slow << " ms" << std::endl;
        check (fast <= 4, "2 ms attack is quick");
        check (slow >= 5 * juce::jmax (1, fast), "50 ms attack is much slower than 2 ms");
    }

    auto releaseHalf = [&] (float releaseSeconds)
    {
        const auto trace = reductionTrace (clean ({ { beacon::pid::threshold, th (-30.0f) },
                                                    { beacon::pid::release, releaseSeconds } }),
                                           stepped (0, 1000), 9000);
        return halfwayTime (trace, 1000, 8999);
    };

    const auto fast = releaseHalf (0.1f), slow = releaseHalf (3.0f);
    std::cout << "  release 0.1 s: half way back in " << fast << " ms; 3 s: " << slow << " ms" << std::endl;
    check (fast < 200, "0.1 s release recovers quickly");
    check (slow > 10 * fast, "3 s release is much slower");
}

void testColour()
{
    section ("THD, tube and the output stage");

    const auto nominal = beacon::level::nominalAmplitude();

    // The THD knob reads what the stage does at the operating level.
    for (float percent : { 0.5f, 1.0f, 4.0f, 8.0f })
    {
        Settings s = clean ({ { beacon::pid::threshold, 0.0f } });
        s[beacon::pid::thd] = percent;

        auto out = render (s, tone (1000.0, nominal), 48000);
        const auto measured = thdOf (out, 0, 1000.0, 24000, 24000);
        std::cout << "  THD " << percent << " %: measured " << measured << " %" << std::endl;
        check (std::abs (measured - percent) < 0.15 * percent, "THD knob is calibrated at " + juce::String (percent) + " %");
    }

    // And louder material distorts more.
    {
        Settings s = clean ({ { beacon::pid::threshold, 0.0f }, { beacon::pid::thd, 1.0f } });
        auto quiet = render (s, tone (1000.0, nominal * 0.25f), 48000);
        auto loud  = render (s, tone (1000.0, nominal * 2.0f), 48000);
        check (thdOf (loud, 0, 1000.0, 24000, 24000) > 3.0 * thdOf (quiet, 0, 1000.0, 24000, 24000),
               "THD follows level");
    }

    // TUBE drives the vari-mu tube harder without changing the level or the
    // amount of compression.
    {
        Settings s { { beacon::pid::threshold, 0.0f } };
        s[beacon::pid::tube] = 0.0f;
        auto plain = render (s, tone (1000.0, nominal), 48000);
        s[beacon::pid::tube] = 15.0f;
        auto driven = render (s, tone (1000.0, nominal), 48000);

        const auto a = thdOf (plain, 0, 1000.0, 24000, 24000), b = thdOf (driven, 0, 1000.0, 24000, 24000);
        const auto shift = db (amplitudeAt (driven, 0, 1000.0, 24000, 24000) / amplitudeAt (plain, 0, 1000.0, 24000, 24000));
        std::cout << "  tube 0 dB: " << a << " %, +15 dB: " << b << " % at +4 dBu" << std::endl;
        check (b > 10.0 * a && b > 0.3, "TUBE +15 dB saturates audibly");
        // Saturation at about 2 % squeezes the fundamental a little; the unit
        // promises the compression stays put, not the last half dB of level.
        check (std::abs (shift) < 0.6, "and keeps the level, got " + juce::String (shift, 3) + " dB");

        Settings c { { beacon::pid::threshold, th (-30.0f) }, { beacon::pid::release, 0.3f } };
        c[beacon::pid::tube] = -15.0f;
        const auto low = steadyReduction (c, -12.0f);
        c[beacon::pid::tube] = 15.0f;
        const auto high = steadyReduction (c, -12.0f);
        check (std::abs (low - high) < 0.5, "the amount of compression does not follow TUBE: "
                                                + juce::String (low, 2) + " / " + juce::String (high, 2) + " dB");
    }

    // RED: the iron saturates on flux, so bass and not treble.
    const Settings red { { beacon::pid::threshold, 0.0f }, { beacon::pid::iron, 1.0f },
                         { beacon::pid::tubeIn, 0.0f } };

    const auto loud = juce::Decibels::decibelsToGain (-2.0f);
    auto bass   = render (red, tone (30.0, loud), 96000);
    auto treble = render (red, tone (1000.0, loud), 96000);

    // Iron link: 10 dB more output into the iron, 10 dB more pad after it.
    auto linked = red;
    linked[beacon::pid::output]  = 10.0f;
    linked[beacon::pid::ironPad] = 10.0f;
    auto driven = render (linked, tone (30.0, loud), 96000);

    const auto bassThd   = thdOf (bass, 0, 30.0, 48000, 48000);
    const auto trebleThd = thdOf (treble, 0, 1000.0, 48000, 48000);
    const auto drivenThd = thdOf (driven, 0, 30.0, 48000, 48000);

    std::cout << "  RED at -2 dBFS: 30 Hz " << bassThd << " %, 1 kHz " << trebleThd
              << " %, 30 Hz with output +10 / pad 10 " << drivenThd << " %" << std::endl;
    check (bassThd > 2.0 * trebleThd, "the transformer saturates on bass");
    check (drivenThd > 2.0 * bassThd, "output up, pad down drives it harder");

    {
        const auto shift = db (amplitudeAt (driven, 0, 30.0, 48000, 48000) / amplitudeAt (bass, 0, 30.0, 48000, 48000));
        check (std::abs (shift) < 1.0, "at about the same level, got " + juce::String (shift, 2) + " dB");
    }

    {
        auto padded = red;
        padded[beacon::pid::ironPad] = 7.0f;
        auto a = render (red,    tone (1000.0, nominal), 48000);
        auto b = render (padded, tone (1000.0, nominal), 48000);
        const auto shift = db (amplitudeAt (b, 0, 1000.0, 24000, 24000) / amplitudeAt (a, 0, 1000.0, 24000, 24000));
        check (std::abs (shift + 7.0) < 0.05, "the pad is a plain attenuator, got " + juce::String (shift, 3) + " dB");

        auto green = padded;
        green[beacon::pid::iron] = 0.0f;
        auto c = render (green, tone (1000.0, nominal), 48000);
        const auto greenShift = db (amplitudeAt (c, 0, 1000.0, 24000, 24000) / nominal);
        check (std::abs (greenShift) < 0.05, "and only in RED mode");
    }

    auto nominalBass = render (red, tone (30.0, nominal), 96000);
    check (thdOf (nominalBass, 0, 30.0, 48000, 48000) < 0.5, "the iron stays clean-ish at the operating level");

    for (double hz : { 30.0, 1000.0, 20000.0 })
    {
        auto o = render (red, tone (hz, 0.01f), 96000);
        const auto g = db (amplitudeAt (o, 0, hz, 48000, 48000) / 0.01);
        std::cout << "  RED response at " << hz << " Hz: " << g << " dB" << std::endl;
        check (std::abs (g) < 1.0, "RED within 1 dB at " + juce::String (hz) + " Hz");
    }

    // The gain cell: working hard it distorts more.
    {
        const Settings s = clean ({ { beacon::pid::threshold, th (-30.0f) }, { beacon::pid::attack, 50.0f },
                                    { beacon::pid::release, 2.0f } });
        auto light = render (s, tone (1000.0, juce::Decibels::decibelsToGain (-25.0f)), 96000);
        auto heavy = render (s, tone (1000.0, juce::Decibels::decibelsToGain (-3.0f)), 96000);
        const auto a = thdOf (light, 0, 1000.0, 72000, 24000), b = thdOf (heavy, 0, 1000.0, 72000, 24000);
        std::cout << "  gain cell THD, light / heavy reduction: " << a << " % / " << b << " %" << std::endl;
        check (b > 3.0 * a, "the cell distorts more as it is pushed towards cutoff");
    }
}

void testSidechain()
{
    section ("Sidechain");

    const Settings base = clean ({ { beacon::pid::threshold, th (-30.0f) } });

    {
        auto filtered = base;
        filtered[beacon::pid::hpfIn] = 1.0f;
        filtered[beacon::pid::hpf]   = 500.0f;
        const auto open = steadyReduction (base, -10.0f);

        auto bassOpen = render (base, tone (50.0, 0.3f), 96000);
        auto bassCut  = render (filtered, tone (50.0, 0.3f), 96000);
        const auto a = -db (amplitudeAt (bassOpen, 0, 50.0, 48000, 48000) / 0.3);
        const auto b = -db (amplitudeAt (bassCut,  0, 50.0, 48000, 48000) / 0.3);
        std::cout << "  50 Hz: GR " << a << " dB open, " << b << " dB with HPF at 500 Hz" << std::endl;
        check (b < 0.6 * a, "the HPF eases bass out of the detector");

        auto switchedOut = filtered;
        switchedOut[beacon::pid::hpfIn] = 0.0f;
        auto c = render (switchedOut, tone (50.0, 0.3f), 96000);
        check (std::abs (-db (amplitudeAt (c, 0, 50.0, 48000, 48000) / 0.3) - a) < 0.1, "and its switch takes it out");
        check (open > 1.0, "and 1 kHz still compresses");
    }

    {
        auto shelf = base;
        shelf[beacon::pid::bellIn] = 1.0f;
        shelf[beacon::pid::bell]   = 10000.0f;

        auto plain = render (base,  tone (10000.0, 0.1f), 96000);
        auto boost = render (shelf, tone (10000.0, 0.1f), 96000);
        const auto a = -db (amplitudeAt (plain, 0, 10000.0, 48000, 48000) / 0.1);
        const auto b = -db (amplitudeAt (boost, 0, 10000.0, 48000, 48000) / 0.1);
        check (b > a + 1.0, "the high bell makes the detector more sensitive to the top");
    }

    {
        auto listen = base;
        listen[beacon::pid::listen] = 1.0f;
        listen[beacon::pid::hpfIn]  = 1.0f;
        listen[beacon::pid::hpf]    = 500.0f;

        auto out = render (listen, tone (50.0, 0.3f), 48000);
        const auto g = db (amplitudeAt (out, 0, 50.0, 24000, 24000) / 0.3);
        check (g < -15.0 && g > -25.0, "LISTEN plays the sidechain: 50 Hz through a 500 Hz first order HPF, got "
                                           + juce::String (g, 1) + " dB");
    }
}

void testSwitching()
{
    section ("Switches null where they should");

    const Settings busy {
        { beacon::pid::input, 6.0f }, { beacon::pid::threshold, th (-30.0f) },
        { beacon::pid::thd, 4.0f }, { beacon::pid::tube, 9.0f },
        { beacon::pid::output, -3.0f }
    };

    const auto source = tone (220.0, 0.4f);

    {
        auto settings = busy;
        settings[beacon::pid::bypass] = 1.0f;
        const auto r = residualAgainst (render (settings, source, 24000), source);
        check (r < -140.0, "bypass nulls against the input, latency included, got " + juce::String (r, 1) + " dB");
    }

    {
        // Mix fully dry leaves the trims.
        auto settings = busy;
        settings[beacon::pid::mix] = 0.0f;
        auto out = render (settings, source, 24000);
        const auto g = db (amplitudeAt (out, 0, 220.0, 12000, 12000) / 0.4);
        check (std::abs (g - 3.0) < 0.05, "mix at 0 % is the trimmed input, got " + juce::String (g, 3) + " dB");
        check (thdOf (out, 0, 220.0, 12000, 12000) < 0.01, "and has none of the colour");
    }

    {
        // Stereo: one detector for both sides, so a quiet right channel ducks
        // with a loud left one, and the image holds.
        auto source2 = [] (int n)
        {
            const auto t = juce::MathConstants<double>::twoPi * n / 48000.0;
            return std::pair { 0.5f * (float) std::sin (1000.0 * t), 0.01f * (float) std::sin (1500.0 * t) };
        };

        auto settings = clean ({ { beacon::pid::threshold, th (-30.0f) } });

        settings[beacon::pid::mode] = (float) beacon::ChannelMode::stereo;
        auto linked = render (settings, source2, 96000);
        settings[beacon::pid::mode] = (float) beacon::ChannelMode::dualMono;
        auto dual = render (settings, source2, 96000);

        const auto leftGr  = -db (amplitudeAt (linked, 0, 1000.0, 48000, 48000) / 0.5);
        const auto rightGr = -db (amplitudeAt (linked, 1, 1500.0, 48000, 48000) / 0.01);
        const auto rightDual = -db (amplitudeAt (dual, 1, 1500.0, 48000, 48000) / 0.01);

        std::cout << "  linked: left GR " << leftGr << " dB, right GR " << rightGr << " dB; dual mono right GR "
                  << rightDual << " dB" << std::endl;
        check (std::abs (leftGr - rightGr) < 0.5, "stereo mode: both sides get the same reduction");
        check (std::abs (rightDual) < 0.05, "dual mono: the quiet side is left alone");
    }

    {
        // Mid/side: a mono source is all mid, so mid/side compresses it
        // exactly as dual mono compresses each side of it.
        const auto compressing = clean ({ { beacon::pid::threshold, th (-30.0f) } });

        auto ms = compressing;
        ms[beacon::pid::mode] = (float) beacon::ChannelMode::midSide;
        auto dm = compressing;
        dm[beacon::pid::mode] = (float) beacon::ChannelMode::dualMono;

        auto a = render (ms, source, 48000);
        auto b = render (dm, source, 48000);

        double error = 0.0, signal = 0.0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 4800; i < a.getNumSamples(); ++i)
            {
                error  += std::pow (a.getSample (ch, i) - b.getSample (ch, i), 2.0);
                signal += std::pow (b.getSample (ch, i), 2.0);
            }

        const auto r = 10.0 * std::log10 (error / signal + 1.0e-30);
        check (r < -60.0, "mid/side matrix reconstructs around the compressor, got " + juce::String (r, 1) + " dB");

        // And a side-only source is all side: the mid detector stays idle.
        auto antiphase = [] (int n)
        {
            const auto v = 0.01f * (float) std::sin (juce::MathConstants<double>::twoPi * 330.0 * n / 48000.0);
            return std::pair { v, -v };
        };

        auto quiet = render (ms, antiphase, 48000);
        const auto g = db (amplitudeAt (quiet, 0, 330.0, 24000, 24000) / 0.01);
        check (std::abs (g) < 0.1, "mid/side: a quiet side below the threshold is left alone, got " + juce::String (g, 3) + " dB");

        // The side has controls of its own in mid/side, and only there. With
        // the mid's compressor off and the side's on, a mono source passes
        // and a loud side-only one is compressed.
        auto separate = clean ({ { beacon::pid::mode, (float) beacon::ChannelMode::midSide },
                                 { beacon::pid::threshold, 0.0f },
                                 { beacon::pid::side (beacon::pid::threshold), th (-30.0f) } });

        auto loudSide = [] (int n)
        {
            const auto v = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * n / 48000.0);
            return std::pair { v, -v };
        };

        auto mono = render (separate, source, 48000);
        auto sideOnly = render (separate, loudSide, 48000);
        const auto monoGain = db (amplitudeAt (mono, 0, 220.0, 24000, 24000) / 0.4);
        const auto sideGain = db (amplitudeAt (sideOnly, 0, 220.0, 24000, 24000) / 0.4);
        std::cout << "  mid off, side on: mono " << monoGain << " dB, side only " << sideGain << " dB" << std::endl;
        check (std::abs (monoGain) < 0.1, "mid/side: the mid follows the main controls");
        check (sideGain < -3.0, "mid/side: the side follows its own controls");

        auto outside = separate;
        outside[beacon::pid::mode] = (float) beacon::ChannelMode::dualMono;
        auto ignored = render (outside, loudSide, 48000);
        check (std::abs (db (amplitudeAt (ignored, 0, 220.0, 24000, 24000) / 0.4)) < 0.1,
               "outside mid/side the side's controls do nothing");

        // The output transformers are wired left and right, outside the
        // mid/side matrix.
        auto iron = ms;
        iron[beacon::pid::threshold] = 0.0f;
        iron[beacon::pid::iron]      = 1.0f;
        iron[beacon::pid::ironPad]   = 6.0f;
        auto c = render (iron, source, 24000);
        const auto left  = db (amplitudeAt (c, 0, 220.0, 12000, 12000) / 0.4);
        const auto right = db (amplitudeAt (c, 1, 220.0, 12000, 12000) / 0.4);
        check (std::abs (left + 6.0) < 0.2 && std::abs (right + 6.0) < 0.2,
               "mid/side: IRON and its pad act on left and right");
    }
}

void testLatency()
{
    section ("Latency");

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        int latency = 0;
        auto out = render (clean ({ { beacon::pid::threshold, 0.0f } }),
                           [] (int n) { return n == 1000 ? std::pair { 0.5f, 0.5f } : std::pair { 0.0f, 0.0f }; },
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

    for (bool forward : { false, true })
    {
        Settings extreme;

        for (int s = 0; s < 2; ++s)
        {
            extreme[beacon::pid::input]     = 15.0f;
            extreme[beacon::pid::threshold] = beacon::range::thresholdMin;
            extreme[beacon::pid::attack]    = 0.5f;
            extreme[beacon::pid::release]   = 0.1f;
            extreme[beacon::pid::limit]     = 1.0f;
            extreme[beacon::pid::feedFwd]   = forward ? 1.0f : 0.0f;
            extreme[beacon::pid::hpfIn]     = 1.0f;
            extreme[beacon::pid::hpf]       = 500.0f;
            extreme[beacon::pid::bellIn]    = 1.0f;
            extreme[beacon::pid::bell]      = 20000.0f;
            extreme[beacon::pid::thd]       = 8.0f;
            extreme[beacon::pid::tube]      = 15.0f;
            extreme[beacon::pid::iron]      = 1.0f;
            extreme[beacon::pid::output]    = 15.0f;
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

                check (finite, juce::String (forward ? "FF" : "FB") + " finite at " + juce::String (rate)
                                   + " Hz, block " + juce::String (blockSize));
            }
        }
    }
}

void testState()
{
    section ("State survives a save");

    beacon::BeaconProcessor a;
    a.apvts.getParameter (beacon::pid::release)->setValueNotifyingHost (0.73f);
    a.apvts.getParameter (beacon::pid::ironPad)->setValueNotifyingHost (0.6f);
    a.apvts.getParameter (beacon::pid::mode)->setValueNotifyingHost (1.0f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    beacon::BeaconProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    check (std::abs (b.apvts.getParameter (beacon::pid::release)->getValue() - 0.73f) < 1.0e-4f, "release restored");
    check (std::abs (b.apvts.getParameter (beacon::pid::ironPad)->getValue() - 0.6f) < 1.0e-4f, "iron pad restored");
    check (b.apvts.getParameter (beacon::pid::mode)->getValue() > 0.99f, "mode restored");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testCurves();
    testTransparentWhenIdle();
    testStaticCurve();
    testTiming();
    testColour();
    testSidechain();
    testSwitching();
    testLatency();
    testRobustness();
    testState();

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
