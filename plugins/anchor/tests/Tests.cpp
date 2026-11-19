/*
    Offline checks for the claims Anchor makes about itself.

    A bus compressor is easy to break silently: a ratio switch whose positions
    stop meaning what they say, a feedback mode that rings, an AUTO release
    that is just another fixed one, a sidechain filter that does not filter,
    an idle unit that is not quite a wire. Each is asserted here.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "indiewave/SessionCompat.h"
#include "dsp/Vca.h"

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

void apply (anchor::AnchorProcessor& processor, const Settings& settings)
{
    for (const auto& [id, value] : settings)
    {
        auto* parameter = processor.apvts.getParameter (id);
        jassert (parameter != nullptr);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }
}

/** Renders a stereo signal through a fresh instance, latency removed. With a
    key source the sidechain input is connected and fed that. */
juce::AudioBuffer<float> render (const Settings& settings, const Source& source, int numSamples,
                                 double sampleRate = 48000.0, int blockSize = 512,
                                 int* latencyOut = nullptr, const Source& keySource = {})
{
    anchor::AnchorProcessor processor;

    if (keySource)
    {
        auto layout = processor.getBusesLayout();
        layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        processor.setBusesLayout (layout);
    }

    processor.setRateAndBufferSizeDetails (sampleRate, blockSize);
    apply (processor, settings);
    processor.prepareToPlay (sampleRate, blockSize);

    const auto latency = processor.getLatencySamples();

    if (latencyOut != nullptr)
        *latencyOut = latency;

    const auto channels = keySource ? 4 : 2;
    const auto total = numSamples + latency;
    juce::AudioBuffer<float> output (2, numSamples);
    juce::AudioBuffer<float> block (channels, blockSize);
    juce::MidiBuffer midi;

    for (int start = 0; start < total; start += blockSize)
    {
        const auto count = juce::jmin (blockSize, total - start);
        block.setSize (channels, count, false, false, true);

        for (int i = 0; i < count; ++i)
        {
            const auto inRange = start + i < numSamples;
            const auto [l, r] = inRange ? source (start + i) : std::pair { 0.0f, 0.0f };
            block.setSample (0, i, l);
            block.setSample (1, i, r);

            if (keySource)
            {
                const auto [kl, kr] = inRange ? keySource (start + i) : std::pair { 0.0f, 0.0f };
                block.setSample (2, i, kl);
                block.setSample (3, i, kr);
            }
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

/** The threshold control, in dBu, for a threshold in dBFS. */
float th (float dbfs) { return dbfs - anchor::level::dbfsAtZeroDbu; }

/** Every colour stage out and the compressor parked: GREEN, no THD, the
    threshold at the top. */
Settings clean (Settings extra = {})
{
    extra.emplace (anchor::pid::iron, 0.0f);
    extra.emplace (anchor::pid::thd, 0.0f);
    extra.emplace (anchor::pid::threshold, anchor::range::thresholdMax);
    return extra;
}

double steadyReduction (Settings settings, float levelDbfs, double hz = 1000.0)
{
    const auto amplitude = juce::Decibels::decibelsToGain (levelDbfs);
    auto out = render (settings, tone (hz, amplitude), 96000);
    return -db (amplitudeAt (out, 0, hz, 72000, 24000) / amplitude);
}

/** The gain reduction meter, one reading per millisecond. */
std::vector<float> reductionTrace (const Settings& settings, const Source& source, int milliseconds)
{
    anchor::AnchorProcessor processor;
    processor.setRateAndBufferSizeDetails (48000.0, 48);
    apply (processor, settings);
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
        trace.push_back (anchor::MeterSource::take (processor.meters.channels[0].reductionDb));
    }

    return trace;
}

/** Milliseconds from `from` until the trace has gone half way from its value
    there to its value at `settled`. */
int halfwayTime (const std::vector<float>& trace, int from, int settled)
{
    const auto start = trace[(size_t) from], end = trace[(size_t) settled];
    const auto half  = 0.5f * (start + end);

    for (int i = from; i < settled; ++i)
        if ((end > start && trace[(size_t) i] >= half) || (end < start && trace[(size_t) i] <= half))
            return i - from;

    return settled - from;
}

/** A tone switched on at `onMs` and off at `offMs`. */
Source burst (float amplitude, int onMs, int offMs, double hz = 1000.0)
{
    return [=] (int n)
    {
        const auto on = n >= onMs * 48 && n < offMs * 48;
        const auto v = on ? amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * hz * n / 48000.0) : 0.0f;
        return std::pair { v, v };
    };
}

// ---------------------------------------------------------------------------

void testCurve()
{
    section ("Static curve: the ratio switch means what it says");

    // The law itself.
    for (int r = 0; r < 5; ++r)
    {
        const auto slope = anchor::vca::slopeFor (r);
        check (anchor::vca::reductionFor (-3.0f, slope) <= 0.0f, "nothing below the corner, ratio " + juce::String (r));
        check (std::abs (anchor::vca::reductionFor (12.0f, slope) - 12.0f * slope) < 1.0e-4f, "straight past the corner");

        // The corner joins both sides smoothly.
        const auto h = 0.5f * anchor::vca::kneeDb;
        check (std::abs (anchor::vca::reductionFor (h - 1.0e-3f, slope) - anchor::vca::reductionFor (h + 1.0e-3f, slope)) < 1.0e-2f,
               "the corner is continuous");
    }

    // Through the processor: 20 dB over a -30 dBFS threshold.
    const std::array<const char*, 5> names { "1.5:1", "2:1", "4:1", "10:1", "INF" };

    for (const auto feedback : { false, true })
    {
        for (int r = 0; r < 5; ++r)
        {
            auto s = clean ({ { anchor::pid::threshold, th (-30.0f) }, { anchor::pid::ratio, (float) r },
                              { anchor::pid::attack, 2.0f }, { anchor::pid::release, 2.0f },
                              { anchor::pid::feedback, feedback ? 1.0f : 0.0f } });
            const auto gr = steadyReduction (s, -10.0f);
            const auto expected = 20.0 * anchor::vca::slopeFor (r);
            const auto ratio = 20.0 / (20.0 - gr);

            std::cout << "  " << (feedback ? "FB " : "FF ") << names[(size_t) r] << ": 20 dB over gives "
                      << juce::String (gr, 2) << " dB of reduction, " << juce::String (ratio, 1) << ":1" << std::endl;
            check (std::abs (gr - expected) < (feedback ? 1.0 : 0.5),
                   juce::String (feedback ? "feedback " : "feed-forward ") + names[(size_t) r] + " reduction near "
                       + juce::String (expected, 1) + " dB");
        }
    }

    // Below the threshold the unit is a wire.
    {
        auto s = clean ({ { anchor::pid::threshold, th (-10.0f) } });
        check (steadyReduction (s, -20.0f) < 0.01, "a tone 10 dB under the threshold is left alone");
    }
}

void testIdle()
{
    section ("Idle: flat and clean");

    const auto settings = clean();

    for (double hz : { 40.0, 1000.0, 15000.0 })
    {
        auto out = render (settings, tone (hz, 0.25f), 48000);
        const auto g = db (amplitudeAt (out, 0, hz, 24000, 24000) / 0.25);
        check (std::abs (g) < 0.02, "flat at " + juce::String (hz) + " Hz, got " + juce::String (g, 4) + " dB");
    }

    auto out = render (settings, tone (1000.0, anchor::level::nominalAmplitude()), 48000);
    const auto thd = thdOf (out, 0, 1000.0, 24000, 24000);
    std::cout << "  THD at +4 dBu, GREEN, at rest: " << thd << " %" << std::endl;
    check (thd < 0.004, "THD+N at rest under 0.004 %");

    // Working, the VCA adds only a trace.
    auto working = render (clean ({ { anchor::pid::threshold, th (-24.0f) } }), tone (1000.0, anchor::level::nominalAmplitude()), 48000);
    const auto thdWorking = thdOf (working, 0, 1000.0, 24000, 24000);
    std::cout << "  THD at +4 dBu with 7.5 dB of reduction: " << thdWorking << " %" << std::endl;
    check (thdWorking < 0.1, "the VCA stays clean while it works");
}

void testTiming()
{
    section ("Attack and release");

    const auto loud = 0.5f;

    // Attack: time to half the final reduction scales with the switch, all
    // eight positions.
    {
        std::vector<int> times;
        for (int a = 0; a < (int) anchor::vca::attacks.size(); ++a)
        {
            auto s = clean ({ { anchor::pid::threshold, th (-30.0f) }, { anchor::pid::attack, (float) a },
                              { anchor::pid::release, 6.0f } });
            const auto trace = reductionTrace (s, burst (loud, 100, 2000), 800);
            times.push_back (halfwayTime (trace, 100, 700));
        }

        juce::String line ("  attack to half, ms:");
        auto rising = true;
        for (size_t a = 0; a < times.size(); ++a)
        {
            line << " " << juce::String (anchor::vca::attacks[a]) << "->" << times[a];
            if (a > 0) rising = rising && times[a] >= times[a - 1];
        }
        std::cout << line << std::endl;
        check (rising && times.back() > times[3], "attack lengthens with the switch");
        check (times.back() >= 10 && times.back() <= 40, "30 ms attack reaches half way in about 20 ms");
    }

    // Release: time to half way back, every fixed position.
    {
        std::vector<int> times;
        for (int r = 0; r < anchor::vca::autoRelease; ++r)
        {
            auto s = clean ({ { anchor::pid::threshold, th (-30.0f) }, { anchor::pid::attack, 1.0f },
                              { anchor::pid::release, (float) r } });
            const auto trace = reductionTrace (s, burst (loud, 0, 1000), 5000);
            times.push_back (halfwayTime (trace, 1000, 4999));
        }

        juce::String line ("  release to half, ms:");
        auto rising = true;
        for (size_t r = 0; r < times.size(); ++r)
        {
            line << " " << juce::String (anchor::vca::releases[r]) << " s->" << times[r];
            if (r > 0) rising = rising && times[r] > times[r - 1];
        }
        std::cout << line << std::endl;
        check (rising, "release lengthens with the switch");
        check (std::abs (times.back() - 830) < 150, "1.2 s release halves in about 0.83 s");
    }

    // AUTO: a short burst lets go quickly, a long passage slowly.
    {
        auto s = clean ({ { anchor::pid::threshold, th (-30.0f) }, { anchor::pid::attack, 1.0f },
                          { anchor::pid::release, (float) anchor::vca::autoRelease } });
        const auto shortTrace = reductionTrace (s, burst (loud, 0, 20), 3000);
        const auto longTrace  = reductionTrace (s, burst (loud, 0, 3000), 6000);
        const auto shortHalf = halfwayTime (shortTrace, 20, 2999);
        const auto longHalf  = halfwayTime (longTrace, 3000, 5999);

        std::cout << "  AUTO: after 20 ms, half way back in " << shortHalf << " ms; after 3 s, in " << longHalf << " ms" << std::endl;
        check (shortHalf < 120, "AUTO lets a transient go quickly");
        check (longHalf > 4 * shortHalf, "and sustained material slowly");
    }
}

void testSidechain()
{
    section ("Sidechain");

    // The filters' responses.
    using F = anchor::SidechainFilter;
    check (std::abs (db (F::magnitudeAt (F::hp90, 90.0)) + 3.01) < 0.05, "90 Hz high pass is -3 dB at 90 Hz");
    check (std::abs (db (F::magnitudeAt (F::hp60, 30.0)) - db (F::magnitudeAt (F::hp60, 15.0)) - 5.0) < 1.5,
           "6 dB per octave below the corner");
    check (db (F::magnitudeAt (F::tilt2, 10000.0)) > 4.5, "T2 lifts the top");
    check (db (F::magnitudeAt (F::tilt1, 10000.0)) > 2.0 && db (F::magnitudeAt (F::tilt1, 10000.0)) < 3.5, "T1 less so");

    // Through the processor, by listening.
    for (int position = 0; position < F::count; ++position)
    {
        for (double hz : { 40.0, 1000.0, 10000.0 })
        {
            auto s = clean ({ { anchor::pid::scFilter, (float) position }, { anchor::pid::listen, 1.0f } });
            auto out = render (s, tone (hz, 0.1f), 48000);
            const auto measured = db (amplitudeAt (out, 0, hz, 24000, 24000) / 0.1);
            const auto expected = db (F::magnitudeAt (position, hz));
            check (std::abs (measured - expected) < 0.3,
                   "listen: position " + juce::String (position) + " at " + juce::String (hz) + " Hz reads "
                       + juce::String (measured, 2) + " dB, the filter is " + juce::String (expected, 2) + " dB");
        }
    }

    // A loud bass note pumps the unit less with the high pass in.
    {
        auto s = clean ({ { anchor::pid::threshold, th (-30.0f) } });
        const auto off = steadyReduction (s, -10.0f, 40.0);
        s[anchor::pid::scFilter] = (float) F::hp150;
        const auto filtered = steadyReduction (s, -10.0f, 40.0);
        std::cout << "  40 Hz, 20 dB over: " << off << " dB with the filter off, " << filtered << " dB at 150 Hz" << std::endl;
        check (filtered < off - 5.0, "the 150 Hz high pass keeps the bass from pumping");
    }

    // The external key.
    {
        auto s = clean ({ { anchor::pid::threshold, th (-30.0f) } });
        const auto quiet = tone (1000.0, 0.01f);
        const auto loudKey = tone (300.0, 0.5f);

        auto internal = render (s, quiet, 48000, 48000.0, 512, nullptr, loudKey);
        s[anchor::pid::external] = 1.0f;
        auto keyed = render (s, quiet, 48000, 48000.0, 512, nullptr, loudKey);

        const auto a = db (amplitudeAt (internal, 0, 1000.0, 24000, 24000) / 0.01);
        const auto b = db (amplitudeAt (keyed, 0, 1000.0, 24000, 24000) / 0.01);
        std::cout << "  quiet programme, loud key: " << a << " dB with EXT out, " << b << " dB with it in" << std::endl;
        check (std::abs (a) < 0.05, "EXT out: the key is ignored");
        check (b < -10.0, "EXT in: the key drives the reduction");

        // With EXT in but nothing connected, the unit listens to itself.
        auto unconnected = render (s, quiet, 48000);
        check (std::abs (db (amplitudeAt (unconnected, 0, 1000.0, 24000, 24000) / 0.01)) < 0.05,
               "EXT with no key connected falls back to the programme");
    }
}

void testColour()
{
    section ("THD and the output stage");

    const auto nominal = anchor::level::nominalAmplitude();

    for (float knob : { 25.0f, 50.0f, 100.0f })
    {
        auto out = render (clean ({ { anchor::pid::thd, knob } }), tone (1000.0, nominal), 48000);
        const auto measured = thdOf (out, 0, 1000.0, 24000, 24000);
        const auto percent = anchor::range::thdPercent (knob);
        std::cout << "  THD knob " << knob << " %: measured " << measured << " %, reads " << percent << " %" << std::endl;
        check (std::abs (measured - percent) < 0.15 * percent, "THD knob is calibrated at " + juce::String (knob));
    }
    check (std::abs (anchor::range::thdPercent (100.0f) - 8.0f) < 1.0e-4f, "the knob's top is the specified 8 %");

    // THD is on the wet path only.
    {
        auto out = render (clean ({ { anchor::pid::thd, 100.0f }, { anchor::pid::mix, 0.0f } }), tone (1000.0, nominal), 48000);
        check (thdOf (out, 0, 1000.0, 24000, 24000) < 0.004, "mix fully dry has none of the THD");
    }

    // RED: the pad is a plain attenuator, and Output up with PAD down drives
    // the iron harder at the same level.
    {
        auto red = clean ({ { anchor::pid::iron, 1.0f } });
        auto plain = render (red, tone (60.0, 0.3f), 48000);
        red[anchor::pid::makeUpIron] = 10.0f;
        red[anchor::pid::ironPad] = 10.0f;
        auto driven = render (red, tone (60.0, 0.3f), 48000);

        const auto level = db (amplitudeAt (driven, 0, 60.0, 24000, 24000) / amplitudeAt (plain, 0, 60.0, 24000, 24000));
        const auto a = thdOf (plain, 0, 60.0, 24000, 24000), b = thdOf (driven, 0, 60.0, 24000, 24000);
        std::cout << "  RED, 60 Hz: " << a << " % plain, " << b << " % with Output +10 and PAD 10 (" << level << " dB)" << std::endl;
        check (std::abs (level) < 1.0, "Output +10 into PAD 10 lands at about the same level");
        check (b > 3.0 * a, "and drives the iron harder");

        auto green = clean ({ { anchor::pid::ironPad, 10.0f }, { anchor::pid::makeUpIron, 12.0f } });
        auto g = render (green, tone (1000.0, 0.1f), 24000);
        check (std::abs (db (amplitudeAt (g, 0, 1000.0, 12000, 12000) / 0.1)) < 0.02, "the PAD and the RED make up do nothing in GREEN");
    }
}

double residualAgainst (const juce::AudioBuffer<float>& out, const Source& in, int from = 4800)
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

void testSwitching()
{
    section ("Switches, modes and links");

    const Settings busy {
        { anchor::pid::threshold, th (-30.0f) }, { anchor::pid::thd, 50.0f },
        { anchor::pid::makeUp, 3.0f }, { anchor::pid::makeUpIron, 9.0f }, { anchor::pid::iron, 1.0f }
    };

    const auto source = tone (220.0, 0.4f);

    {
        auto s = busy;
        s[anchor::pid::bypass] = 1.0f;
        const auto r = residualAgainst (render (s, source, 24000), source);
        check (r < -140.0, "bypass nulls against the input, latency included, got " + juce::String (r, 1) + " dB");
    }

    {
        auto s = busy;
        s[anchor::pid::mix] = 0.0f;
        s[anchor::pid::iron] = 0.0f;
        auto out = render (s, source, 24000);
        const auto g = db (amplitudeAt (out, 0, 220.0, 12000, 12000) / 0.4);
        check (std::abs (g - 3.0) < 0.05, "mix at 0 % is the input with the GREEN make up, got " + juce::String (g, 3) + " dB");

        // Each output stage keeps its own make up.
        s[anchor::pid::iron] = 1.0f;
        auto red = render (s, source, 24000);
        const auto r = db (amplitudeAt (red, 0, 220.0, 12000, 12000) / 0.4);
        check (std::abs (r - 9.0) < 0.3, "RED takes its own make up, got " + juce::String (r, 2) + " dB");

        // Mute silences the channel, and in mid/side only its half.
        auto muted = s;
        muted[anchor::pid::mute] = 1.0f;
        auto silent = render (muted, source, 24000);
        check (silent.getMagnitude (0, 12000, 12000) < 1.0e-6f, "mute silences the channel");

        auto msMute = clean ({ { anchor::pid::mode, (float) anchor::ChannelMode::midSide },
                               { anchor::pid::side (anchor::pid::mute), 1.0f } });
        auto keptMid = render (msMute, source, 24000);
        check (std::abs (db (amplitudeAt (keptMid, 0, 220.0, 12000, 12000) / 0.4)) < 0.05,
               "mid/side: muting the side leaves a mono source whole");
    }

    {
        // Stereo links the detectors so the image holds; dual mono does not.
        auto source2 = [] (int n)
        {
            const auto t = juce::MathConstants<double>::twoPi * n / 48000.0;
            return std::pair { 0.5f * (float) std::sin (1000.0 * t), 0.01f * (float) std::sin (1500.0 * t) };
        };

        auto s = clean ({ { anchor::pid::threshold, th (-30.0f) } });
        s[anchor::pid::mode] = (float) anchor::ChannelMode::stereo;
        auto linked = render (s, source2, 96000);
        s[anchor::pid::mode] = (float) anchor::ChannelMode::dualMono;
        auto dual = render (s, source2, 96000);

        const auto leftGr    = -db (amplitudeAt (linked, 0, 1000.0, 48000, 48000) / 0.5);
        const auto rightGr   = -db (amplitudeAt (linked, 1, 1500.0, 48000, 48000) / 0.01);
        const auto rightDual = -db (amplitudeAt (dual,   1, 1500.0, 48000, 48000) / 0.01);
        check (std::abs (leftGr - rightGr) < 0.3, "stereo: both sides get the same reduction");
        check (std::abs (rightDual) < 0.05, "dual mono: the quiet side is left alone");
    }

    {
        // Mid/side: a mono source is all mid.
        const auto compressing = clean ({ { anchor::pid::threshold, th (-30.0f) } });
        auto ms = compressing;
        ms[anchor::pid::mode] = (float) anchor::ChannelMode::midSide;
        auto dm = compressing;
        dm[anchor::pid::mode] = (float) anchor::ChannelMode::dualMono;

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

        // The side follows its own controls in mid/side, and only there.
        auto separate = clean ({ { anchor::pid::mode, (float) anchor::ChannelMode::midSide },
                                 { anchor::pid::side (anchor::pid::threshold), th (-30.0f) } });
        auto loudSide = [] (int n)
        {
            const auto v = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * n / 48000.0);
            return std::pair { v, -v };
        };

        const auto monoGain = db (amplitudeAt (render (separate, source, 48000), 0, 220.0, 24000, 24000) / 0.4);
        const auto sideGain = db (amplitudeAt (render (separate, loudSide, 48000), 0, 220.0, 24000, 24000) / 0.4);
        check (std::abs (monoGain) < 0.05, "mid/side: the mid follows the main controls");
        check (sideGain < -3.0, "mid/side: the side follows its own controls");

        auto outside = separate;
        outside[anchor::pid::mode] = (float) anchor::ChannelMode::dualMono;
        check (std::abs (db (amplitudeAt (render (outside, loudSide, 48000), 0, 220.0, 24000, 24000) / 0.4)) < 0.05,
               "outside mid/side and unsplit, the second set does nothing");

        // The iron is wired left and right.
        auto iron = ms;
        iron[anchor::pid::threshold] = anchor::range::thresholdMax;
        iron[anchor::pid::iron]      = 1.0f;
        iron[anchor::pid::ironPad]   = 6.0f;
        auto c = render (iron, source, 24000);
        check (std::abs (db (amplitudeAt (c, 0, 220.0, 12000, 12000) / 0.4) + 6.0) < 0.2
                   && std::abs (db (amplitudeAt (c, 1, 220.0, 12000, 12000) / 0.4) + 6.0) < 0.2,
               "mid/side: IRON and its pad act on left and right");
    }

    {
        // Dual mono: the right follows the left unless split.
        Settings s = clean ({ { anchor::pid::mode, (float) anchor::ChannelMode::dualMono },
                              { anchor::pid::threshold, th (-30.0f) } });
        const auto src = tone (1000.0, 0.5f);
        const auto level = [] (const juce::AudioBuffer<float>& b, int ch) { return b.getRMSLevel (ch, 24000, 24000); };

        auto linked = render (s, src, 48000);
        check (std::abs (db (level (linked, 1) / level (linked, 0))) < 0.01, "unsplit: both channels take the left's controls");

        s[anchor::pid::dualSplit] = 1.0f;
        auto split = render (s, src, 48000);
        check (db (level (split, 1) / level (split, 0)) > 2.0, "split: the right takes the second set");
    }
}

void testLatency()
{
    section ("Latency");

    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        int latency = 0;
        auto out = render (clean(), [] (int n) { return n == 1000 ? std::pair { 0.5f, 0.5f } : std::pair { 0.0f, 0.0f }; },
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

    for (const auto feedback : { false, true })
    {
        Settings extreme;
        for (const auto side : { false, true })
        {
            const auto id = [side] (const char* n) { return side ? anchor::pid::side (n) : juce::String (n); };
            extreme[id (anchor::pid::threshold)] = anchor::range::thresholdMin;
            extreme[id (anchor::pid::ratio)]     = 4.0f;
            extreme[id (anchor::pid::attack)]    = 0.0f;
            extreme[id (anchor::pid::release)]   = 0.0f;
            extreme[id (anchor::pid::feedback)]  = feedback ? 1.0f : 0.0f;
            extreme[id (anchor::pid::scFilter)]  = 5.0f;
            extreme[id (anchor::pid::thd)]        = 100.0f;
            extreme[id (anchor::pid::makeUp)]     = 20.0f;
            extreme[id (anchor::pid::makeUpIron)] = 20.0f;
        }
        extreme[anchor::pid::iron] = 1.0f;

        juce::Random random (1234);
        auto noise = [&random] (int) { return std::pair { random.nextFloat() * 2.0f - 1.0f, random.nextFloat() * 2.0f - 1.0f }; };

        for (double rate : { 44100.0, 96000.0, 192000.0 })
            for (int blockSize : { 1, 64, 1024 })
                for (auto mode : { anchor::ChannelMode::stereo, anchor::ChannelMode::midSide })
                {
                    auto s = extreme;
                    s[anchor::pid::mode] = (float) mode;
                    auto out = render (s, noise, 8192, rate, blockSize);
                    bool finite = true;
                    float peak = 0.0f;

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < out.getNumSamples(); ++i)
                        {
                            finite = finite && std::isfinite (out.getSample (ch, i));
                            peak = juce::jmax (peak, std::abs (out.getSample (ch, i)));
                        }

                    check (finite && peak < 100.0f, juce::String (feedback ? "FB" : "FF") + " finite at " + juce::String (rate)
                                                        + " Hz, block " + juce::String (blockSize));
                }
    }

    // Feedback at INF and the fastest attack settles rather than ringing.
    {
        auto s = clean ({ { anchor::pid::threshold, th (-30.0f) }, { anchor::pid::ratio, 4.0f },
                          { anchor::pid::attack, 0.0f }, { anchor::pid::feedback, 1.0f } });
        const auto trace = reductionTrace (s, burst (0.5f, 0, 2000), 1500);
        auto spread = 0.0f;
        for (size_t i = 1000; i < 1500; ++i)
            spread = juce::jmax (spread, std::abs (trace[i] - trace[1000]));
        check (spread < 1.0f, "feedback at INF holds steady, spread " + juce::String (spread, 3) + " dB");
    }
}

void testState()
{
    section ("State survives a save");

    anchor::AnchorProcessor a;
    a.apvts.getParameter (anchor::pid::threshold)->setValueNotifyingHost (0.73f);
    a.apvts.getParameter (anchor::pid::ratio)->setValueNotifyingHost (1.0f);
    a.apvts.getParameter (anchor::pid::mode)->setValueNotifyingHost (1.0f);

    juce::MemoryBlock state;
    a.getStateInformation (state);

    anchor::AnchorProcessor b;
    b.setStateInformation (state.getData(), (int) state.getSize());

    check (std::abs (b.apvts.getParameter (anchor::pid::threshold)->getValue() - 0.73f) < 1.0e-3f, "threshold restored");
    check (b.apvts.getParameter (anchor::pid::ratio)->getValue() > 0.99f, "ratio restored");
    check (b.apvts.getParameter (anchor::pid::mode)->getValue() > 0.99f, "mode restored");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testCurve();
    testIdle();
    testTiming();
    testSidechain();
    testColour();
    testSwitching();
    testLatency();
    testRobustness();
    testState();

    section ("Sessions saved by released versions open unchanged");
    indiewave::compat::run ([] { return std::make_unique<anchor::AnchorProcessor>(); },
                            juce::File (INDIEWAVE_COMPAT_DIR), INDIEWAVE_VERSION, check);

    std::cout << "\n" << (checks - failures) << " / " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
