/*
    Offline checks for the claims Tide makes about itself.

    Two kinds. The first asks whether the bands are the bands: whether the
    analogue prototypes do what the panel and the hardware's specification
    say. The second asks whether the plugin is those bands: whether the
    filters at a sample rate follow them, and whether the switches, routing,
    cue and bypass do what the panel says.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/Bands.h"
#include "dsp/ChannelEq.h"
#include "dsp/MatchedDesign.h"

#include <cmath>
#include <complex>
#include <functional>
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

constexpr double twoPi = juce::MathConstants<double>::twoPi;

double db (double gain) { return 20.0 * std::log10 (juce::jmax (gain, 1.0e-12)); }

juce::String strip (int index, const char* name) { return tide::pid::forStrip (index, name); }

using tide::bands::Analog;

double analogDb (const Analog& h, double hz)
{
    return db (std::abs (tide::MatchedDesign::analogResponse (h, twoPi * hz)));
}

enum class Shape { lowShelf, lowPeak, mid, highShelf, highPeak };

Analog prototype (Shape shape, int freq, double gainDb)
{
    using namespace tide::bands;

    switch (shape)
    {
        case Shape::lowShelf:  return lowShelf  (omega (lowHz [(size_t) freq]), gainDb);
        case Shape::lowPeak:   return peak      (omega (lowHz [(size_t) freq]), gainDb);
        case Shape::mid:       return peak      (omega (midHz [(size_t) freq]), gainDb);
        case Shape::highShelf: return highShelf (omega (highHz[(size_t) freq]), gainDb);
        case Shape::highPeak:  return peak      (omega (highHz[(size_t) freq]), gainDb);
    }

    return {};
}

double legendHz (Shape shape, int freq)
{
    using namespace tide::bands;

    switch (shape)
    {
        case Shape::lowShelf:
        case Shape::lowPeak:   return lowHz [(size_t) freq];
        case Shape::mid:       return midHz [(size_t) freq];
        case Shape::highShelf:
        case Shape::highPeak:  return highHz[(size_t) freq];
    }

    return 1000.0;
}

constexpr Shape allShapes[] = { Shape::lowShelf, Shape::lowPeak, Shape::mid, Shape::highShelf, Shape::highPeak };

// ---------------------------------------------------------------------------

void testPrototypes()
{
    section ("The bands do what the panel says");

    using namespace tide::bands;

    double worstCentre = 0.0, worstMidSlope = 0.0, worstPlateau = 0.0, worstMirror = 0.0;

    for (int step = 0; step < numGainSteps; ++step)
    {
        const auto g = gainDb[(size_t) step];

        for (int f = 0; f < numFrequencies; ++f)
        {
            // Peaks: the switch's dB, exactly, at the legend.
            for (auto shape : { Shape::lowPeak, Shape::mid, Shape::highPeak })
                worstCentre = std::max (worstCentre, std::abs (analogDb (prototype (shape, f, g), legendHz (shape, f)) - g));

            // Shelves: half the dB at the legend (the middle of the slope),
            // the whole of it two decades out.
            for (auto shape : { Shape::lowShelf, Shape::highShelf })
            {
                const auto hz = legendHz (shape, f);
                worstMidSlope = std::max (worstMidSlope, std::abs (analogDb (prototype (shape, f, g), hz) - 0.5 * g));

                const auto far = shape == Shape::lowShelf ? hz / 100.0 : hz * 100.0;
                worstPlateau = std::max (worstPlateau, std::abs (analogDb (prototype (shape, f, g), far) - g));
            }

            // A cut is the boost of the same size upside down.
            for (auto shape : allShapes)
                for (double hz = 10.0; hz < 40000.0; hz *= 1.21)
                {
                    const auto both = analogDb (prototype (shape, f, g), hz) + analogDb (prototype (shape, f, -g), hz);
                    worstMirror = std::max (worstMirror, std::abs (both));
                }
        }
    }

    check (worstCentre < 1.0e-9,   "every peak is its switch's dB at its legend, worst " + juce::String (worstCentre));
    check (worstMidSlope < 1.0e-9, "every shelf is half its dB at its legend, worst " + juce::String (worstMidSlope));
    check (worstPlateau < 0.01,    "every shelf reaches its full dB, worst " + juce::String (worstPlateau));
    check (worstMirror < 1.0e-9,   "cut is boost upside down, worst " + juce::String (worstMirror));

    // Q = 0.5 is broad: a 4 dB peak keeps more than half its dB an octave either side.
    {
        const auto h = peak (omega (1000.0), 4.0);
        const auto below = analogDb (h, 500.0), above = analogDb (h, 2000.0);
        check (std::abs (below - above) < 1.0e-9, "a peak is symmetric on a log axis");
        check (below > 2.3 && below < 2.8, "Q 0.5: +4 dB peak is still about +2.5 dB an octave away, got " + juce::String (below, 2));
    }

    // The high pass: 12 dB/octave, Q 0.5, 6 dB down at its legend, x5 moves it.
    for (int f = 0; f < numFrequencies; ++f)
        for (auto times5 : { false, true })
        {
            const auto hz = hpfHz[(size_t) f] * (times5 ? 5.0 : 1.0);
            const auto h  = highPass (hpfOmega (f, times5));

            check (std::abs (analogDb (h, hz) + 6.0206) < 1.0e-6, "HPF is 6 dB down at " + juce::String (hz) + " Hz");
            check (std::abs (analogDb (h, hz / 8.0) - analogDb (h, hz / 16.0) - 12.0) < 0.1, "HPF falls 12 dB/octave below its corner");
            check (analogDb (h, hz * 10.0) > -0.1 && analogDb (h, hz * 10.0) <= 0.0, "HPF is flat a decade up, with no bump");
        }

    check (std::abs (hpfHz[5] * hpfMultiplier - 225.0) < 1.0e-9, "x5 takes 45 Hz to 225 Hz");
}

// ---------------------------------------------------------------------------

void testDigital()
{
    section ("The digital filters follow the analogue prototypes");

    for (auto rate : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        tide::MatchedDesign design;
        design.prepare (rate);

        const auto top = std::min (20000.0, 0.45 * rate);
        double worstBand = 0.0, worstHpf = 0.0;
        juce::String where;

        for (auto shape : allShapes)
            for (int f = 0; f < tide::bands::numFrequencies; ++f)
                for (auto g : tide::bands::gainDb)
                {
                    if (std::abs (g) < 1.0e-12)
                        continue;

                    const auto analog  = prototype (shape, f, g);
                    const auto digital = design (analog);

                    for (double hz = 20.0; hz <= top; hz *= 1.05)
                    {
                        const auto d = std::abs (db (std::abs (digital.response (twoPi * hz / rate))) - analogDb (analog, hz));

                        if (d > worstBand)
                        {
                            worstBand = d;
                            where = juce::String ((int) shape) + " f" + juce::String (f) + " " + juce::String (g) + " dB at " + juce::String (hz, 0) + " Hz";
                        }
                    }
                }

        for (int f = 0; f < tide::bands::numFrequencies; ++f)
            for (auto times5 : { false, true })
            {
                const auto analog  = tide::bands::highPass (tide::bands::hpfOmega (f, times5));
                const auto digital = design (analog);

                for (double hz = 20.0; hz <= top; hz *= 1.05)
                    worstHpf = std::max (worstHpf, std::abs (db (std::abs (digital.response (twoPi * hz / rate))) - analogDb (analog, hz)));
            }

        std::cout << "  " << rate << " Hz: bands within " << worstBand << " dB (" << where << "), high pass within "
                  << worstHpf << " dB" << std::endl;

        const auto limit = rate < 50000.0 ? 0.25 : rate < 100000.0 ? 0.03 : 0.01;
        check (worstBand < limit, "every band at " + juce::String (rate) + " Hz within " + juce::String (limit) + " dB of the prototype, 20 Hz to 20 kHz");
        check (worstHpf < 0.05, "the high pass at " + juce::String (rate) + " Hz within 0.05 dB of the prototype");
    }
}

// ---------------------------------------------------------------------------

struct Rig
{
    tide::TideProcessor processor;
    double rate;
    int block;

    explicit Rig (double sampleRate = 48000.0, int blockSize = 512) : rate (sampleRate), block (blockSize)
    {
        processor.setPlayConfigDetails (2, 2, rate, block);
    }

    void set (const juce::String& id, float value)
    {
        auto* p = processor.apvts.getParameter (id);
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    void prepare() { processor.prepareToPlay (rate, block); }

    /** Runs two channel buffers through in blocks, in place. */
    void run (std::vector<float>& left, std::vector<float>& right)
    {
        juce::MidiBuffer midi;

        for (size_t start = 0; start < left.size(); start += (size_t) block)
        {
            const auto n = (int) std::min ((size_t) block, left.size() - start);
            float* channels[] = { left.data() + start, right.data() + start };
            juce::AudioBuffer<float> buffer (channels, 2, n);
            processor.processBlock (buffer, midi);
        }
    }
};

std::vector<float> sine (double hz, double rate, int length, double amplitude = 0.25, double phase = 0.0)
{
    std::vector<float> out ((size_t) length);

    for (int i = 0; i < length; ++i)
        out[(size_t) i] = (float) (amplitude * std::sin (twoPi * hz * i / rate + phase));

    return out;
}

std::vector<float> noise (int length, int seed)
{
    juce::Random random (seed);
    std::vector<float> out ((size_t) length);

    for (auto& x : out)
        x = 0.5f * (random.nextFloat() * 2.0f - 1.0f);

    return out;
}

/** Amplitude of a tone in the last part of a buffer, by correlation. */
double toneAmplitude (const std::vector<float>& x, double hz, double rate, int from)
{
    double s = 0.0, c = 0.0;
    const auto period = rate / hz;
    const auto cycles = std::floor ((double) ((int) x.size() - from) / period);
    const auto to     = from + (int) std::floor (cycles * period);

    for (int i = from; i < to; ++i)
    {
        s += x[(size_t) i] * std::sin (twoPi * hz * i / rate);
        c += x[(size_t) i] * std::cos (twoPi * hz * i / rate);
    }

    return 2.0 * std::sqrt (s * s + c * c) / (to - from);
}

/** Gain of the plugin at one frequency on one channel, in dB. */
double measure (Rig& rig, double hz, int channel = 0)
{
    const auto length = (int) (rig.rate * 0.5);
    auto left  = sine (hz, rig.rate, length);
    auto right = sine (hz, rig.rate, length);
    rig.run (left, right);
    return db (toneAmplitude (channel == 0 ? left : right, hz, rig.rate, length / 2) / 0.25);
}

bool identical (const std::vector<float>& a, const std::vector<float>& b)
{
    return std::memcmp (a.data(), b.data(), a.size() * sizeof (float)) == 0;
}

// ---------------------------------------------------------------------------

void testThroughThePlugin()
{
    section ("Measured through the plugin");

    struct Case
    {
        const char* name;
        std::function<void (Rig&)> setup;
        std::function<double (double)> expected;
    };

    using namespace tide::bands;

    const Case cases[] = {
        { "low shelf +4 dB at 72 Hz",
          [] (Rig& r) { r.set (strip (0, tide::pid::lowFreq), 2.0f); r.set (strip (0, tide::pid::lowGain), 10.0f); },
          [] (double hz) { return analogDb (lowShelf (omega (72.0), 4.0), hz); } },
        { "low peak -3 dB at 132 Hz",
          [] (Rig& r) { r.set (strip (0, tide::pid::lowShelf), 0.0f); r.set (strip (0, tide::pid::lowFreq), 3.0f); r.set (strip (0, tide::pid::lowGain), 1.0f); },
          [] (double hz) { return analogDb (peak (omega (132.0), -3.0), hz); } },
        { "mid +2 dB at 2.4 kHz",
          [] (Rig& r) { r.set (strip (0, tide::pid::midFreq), 4.0f); r.set (strip (0, tide::pid::midGain), 8.0f); },
          [] (double hz) { return analogDb (peak (omega (2400.0), 2.0), hz); } },
        { "high shelf +3 dB at 12 kHz",
          [] (Rig& r) { r.set (strip (0, tide::pid::highFreq), 2.0f); r.set (strip (0, tide::pid::highGain), 9.0f); },
          [] (double hz) { return analogDb (highShelf (omega (12000.0), 3.0), hz); } },
        { "HPF 30 Hz x5, level -1 dB",
          [] (Rig& r) { r.set (strip (0, tide::pid::hpfIn), 1.0f); r.set (strip (0, tide::pid::hpfTimes5), 1.0f);
                        r.set (strip (0, tide::pid::hpfFreq), 2.0f); r.set (strip (0, tide::pid::level), 3.0f); },
          [] (double hz) { return analogDb (highPass (omega (150.0)), hz) - 1.0; } },
    };

    for (const auto& c : cases)
    {
        double worst = 0.0;

        for (auto hz : { 40.0, 72.0, 150.0, 400.0, 1000.0, 2400.0, 6000.0, 12000.0, 16000.0 })
        {
            Rig rig;
            c.setup (rig);
            rig.prepare();
            worst = std::max (worst, std::abs (measure (rig, hz) - c.expected (hz)));
        }

        check (worst < 0.08, juce::String (c.name) + ": within 0.08 dB of the prototype with sine tones, got " + juce::String (worst, 3));
    }

    // The right half works on the right channel only.
    {
        Rig rig;
        rig.set (strip (1, tide::pid::midGain), 10.0f);
        rig.prepare();
        const auto left = measure (rig, 870.0, 0);
        Rig rig2;
        rig2.set (strip (1, tide::pid::midGain), 10.0f);
        rig2.prepare();
        const auto right = measure (rig2, 870.0, 1);
        check (std::abs (left) < 0.001, "the right half leaves the left channel alone");
        check (std::abs (right - 4.0) < 0.05, "the right half's mid band is +4 dB at its legend on the right channel");
    }
}

// ---------------------------------------------------------------------------

void testNulls()
{
    section ("Nulls, bypass and latency");

    const auto length = 48000;

    {
        Rig rig;
        rig.prepare();
        auto l = noise (length, 1), r = noise (length, 2);
        const auto inL = l, inR = r;
        rig.run (l, r);
        check (identical (l, inL) && identical (r, inR), "a flat panel is the input, bit for bit");
        check (rig.processor.getLatencySamples() == 0, "zero latency");
    }

    {
        Rig rig;
        rig.set (tide::pid::midSide, 1.0f);
        rig.prepare();
        auto l = noise (length, 3), r = noise (length, 4);
        const auto inL = l, inR = r;
        rig.run (l, r);
        check (identical (l, inL) && identical (r, inR), "a flat panel in M/S is the input, bit for bit");
    }

    auto busy = [] (Rig& rig)
    {
        rig.set (strip (0, tide::pid::lowGain), 9.0f);
        rig.set (strip (0, tide::pid::highGain), 0.0f);
        rig.set (strip (1, tide::pid::midGain), 10.0f);
        rig.set (strip (1, tide::pid::hpfIn), 1.0f);
        rig.set (strip (1, tide::pid::level), 2.0f);
    };

    {
        Rig rig;
        busy (rig);
        rig.set (tide::pid::eqIn, 0.0f);
        rig.prepare();
        auto l = noise (length, 5), r = noise (length, 6);
        const auto inL = l, inR = r;
        rig.run (l, r);
        check (identical (l, inL) && identical (r, inR), "EQ out is the input, bit for bit, whatever the panel says");
    }

    {
        Rig rig;
        busy (rig);
        rig.set (tide::pid::bypass, 1.0f);
        rig.set (tide::pid::cue, 0.0f);
        rig.prepare();
        auto l = noise (length, 7), r = noise (length, 8);
        const auto inL = l, inR = r;
        rig.run (l, r);
        check (identical (l, inL) && identical (r, inR), "the host's bypass is the input, bit for bit, cue included");
    }

    // EQ in, then out under signal: after the fade it is the input again.
    {
        Rig rig;
        busy (rig);
        rig.prepare();
        auto l = noise (length, 9), r = noise (length, 10);
        std::vector<float> head (l.begin(), l.begin() + 4800), headR (r.begin(), r.begin() + 4800);
        rig.run (head, headR);
        rig.set (tide::pid::eqIn, 0.0f);
        std::vector<float> tail (l.begin() + 4800, l.end()), tailR (r.begin() + 4800, r.end());
        const auto inTail = tail;
        rig.run (tail, tailR);
        const std::vector<float> settled (tail.begin() + 2400, tail.end()), expected (inTail.begin() + 2400, inTail.end());
        check (identical (settled, expected), "switched out under signal, it is the input again once the fade is over");
    }
}

// ---------------------------------------------------------------------------

void testMidSideCueLink()
{
    section ("M/S, cue and link");

    const auto length = 24000;
    const auto rate = 48000.0;

    // Side level sets the width: -4 dB on the side, nothing on the mid.
    {
        Rig rig;
        rig.set (tide::pid::midSide, 1.0f);
        rig.set (strip (1, tide::pid::level), 0.0f);
        rig.prepare();

        auto l = sine (1000.0, rate, length, 0.3);
        auto r = sine (1000.0, rate, length, 0.1);
        rig.run (l, r);

        std::vector<float> mid ((size_t) length), side ((size_t) length);
        for (size_t i = 0; i < (size_t) length; ++i)
        {
            mid[i]  = 0.5f * (l[i] + r[i]);
            side[i] = 0.5f * (l[i] - r[i]);
        }

        check (std::abs (db (toneAmplitude (mid,  1000.0, rate, length / 2) / 0.2)) < 0.01, "M/S: the mid is untouched by the side's level");
        check (std::abs (db (toneAmplitude (side, 1000.0, rate, length / 2) / 0.1) + 4.0) < 0.01, "M/S: side level -4 dB narrows the image by 4 dB of side");
    }

    // The high pass on the side puts everything below it in mono.
    {
        Rig rig;
        rig.set (tide::pid::midSide, 1.0f);
        rig.set (strip (1, tide::pid::hpfIn), 1.0f);
        rig.set (strip (1, tide::pid::hpfTimes5), 1.0f);
        rig.set (strip (1, tide::pid::hpfFreq), 5.0f);
        rig.prepare();

        auto l = sine (40.0, rate, length * 2, 0.25);
        auto r = l;
        for (auto& x : r) x = -x;                         // all side
        rig.run (l, r);

        const auto left = db (toneAmplitude (l, 40.0, rate, length) / 0.25);
        check (left < -28.0, "side HPF at 225 Hz takes a 40 Hz side signal down by more than 28 dB, got " + juce::String (left, 1));
    }

    // Cue in L/R: one channel on both outputs.
    {
        Rig rig;
        rig.set (tide::pid::cue, 2.0f);
        rig.prepare();
        auto l = sine (500.0, rate, length, 0.3);
        auto r = sine (700.0, rate, length, 0.2);
        const auto inR = r;
        rig.run (l, r);
        check (identical (l, inR) && identical (r, inR), "cue R/S in L/R: the right channel on both outputs");
    }

    // Cue in M/S: the mid, or the side, on both outputs.
    {
        Rig rig;
        rig.set (tide::pid::midSide, 1.0f);
        rig.set (tide::pid::cue, 2.0f);
        rig.prepare();
        auto l = sine (500.0, rate, length, 0.3);
        auto r = sine (500.0, rate, length, 0.1);
        rig.run (l, r);
        check (std::abs (db (toneAmplitude (l, 500.0, rate, length / 2) / 0.1)) < 0.01
                   && identical (l, r),
               "cue R/S in M/S: the side on both outputs");
    }

    {
        Rig rig;
        rig.set (tide::pid::midSide, 1.0f);
        rig.set (tide::pid::cue, 0.0f);
        rig.prepare();
        auto l = sine (500.0, rate, length, 0.3);
        auto r = sine (500.0, rate, length, -0.1);
        rig.run (l, r);
        check (std::abs (db (toneAmplitude (l, 500.0, rate, length / 2) / 0.1)) < 0.01 && identical (l, r),
               "cue L/M in M/S: the mid on both outputs");
    }

    // Linked, the left half drives both channels and the right is ignored.
    {
        Rig linked, reference;

        for (auto* rig : { &linked, &reference })
        {
            rig->set (strip (0, tide::pid::lowGain), 9.0f);
            rig->set (strip (0, tide::pid::highGain), 2.0f);
        }

        linked.set (tide::pid::link, 1.0f);
        linked.set (strip (1, tide::pid::midGain), 0.0f);
        linked.set (strip (1, tide::pid::hpfIn), 1.0f);
        reference.set (strip (1, tide::pid::lowGain), 9.0f);
        reference.set (strip (1, tide::pid::highGain), 2.0f);

        linked.prepare();
        reference.prepare();

        auto l1 = noise (length, 11), r1 = noise (length, 12);
        auto l2 = l1, r2 = r1;
        linked.run (l1, r1);
        reference.run (l2, r2);
        check (identical (l1, l2) && identical (r1, r2), "linked: both channels follow the left half, the right half is ignored");
    }
}

// ---------------------------------------------------------------------------

void testSwitching()
{
    section ("Switches thrown under signal");

    // Shelf to peak: once the fade is over it is the peak alone.
    {
        Rig switched, peak;

        for (auto* rig : { &switched, &peak })
            rig->set (strip (0, tide::pid::lowGain), 10.0f);

        peak.set (strip (0, tide::pid::lowShelf), 0.0f);
        switched.prepare();
        peak.prepare();

        auto l = noise (48000, 13), r = noise (48000, 14);
        auto pl = l, pr = r;

        std::vector<float> a (l.begin(), l.begin() + 24000), ar (r.begin(), r.begin() + 24000);
        std::vector<float> b (l.begin() + 24000, l.end()),   br (r.begin() + 24000, r.end());
        switched.run (a, ar);
        switched.set (strip (0, tide::pid::lowShelf), 0.0f);
        switched.run (b, br);
        peak.run (pl, pr);

        double worst = 0.0;
        for (size_t i = 24000 + 4800; i < 48000; ++i)
            worst = std::max (worst, (double) std::abs (b[i - 24000] - pl[i]));

        check (worst < 1.0e-6, "shelf thrown to peak settles on the peak exactly, worst " + juce::String (worst));
    }

    // Everything at random under a tone: no step bigger than the tone itself
    // can make through the most gain the panel has at that frequency.
    const auto rate = 48000.0;
    const auto amplitude = 0.25;
    const auto hz = 1000.0;

    tide::TideProcessor processor;
    processor.setPlayConfigDetails (2, 2, rate, 256);
    processor.prepareToPlay (rate, 256);

    juce::Random random (42);
    juce::StringArray ids;

    for (int s = 0; s < 2; ++s)
        for (auto* id : { tide::pid::lowFreq, tide::pid::lowShelf, tide::pid::lowGain, tide::pid::midFreq, tide::pid::midGain,
                          tide::pid::highFreq, tide::pid::highShelf, tide::pid::highGain, tide::pid::hpfIn, tide::pid::hpfTimes5,
                          tide::pid::hpfFreq, tide::pid::level })
            ids.add (strip (s, id));

    ids.add (tide::pid::eqIn);
    ids.add (tide::pid::midSide);
    ids.add (tide::pid::link);

    double worstStep = 0.0, previous = 0.0;
    long long n = 0;
    juce::MidiBuffer midi;

    for (int blockIndex = 0; blockIndex < 1500; ++blockIndex)
    {
        if (blockIndex % 6 == 0)
            for (int k = 0; k < 3; ++k)
            {
                auto* p = processor.apvts.getParameter (ids[random.nextInt (ids.size())]);
                p->setValueNotifyingHost (random.nextFloat());
            }

        juce::AudioBuffer<float> buffer (2, 256);

        for (int i = 0; i < 256; ++i, ++n)
        {
            const auto x = (float) (amplitude * std::sin (twoPi * hz * (double) n / rate));
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }

        processor.processBlock (buffer, midi);

        for (int i = 0; i < 256; ++i)
        {
            const auto y = (double) buffer.getSample (0, i);
            worstStep = std::max (worstStep, std::abs (y - previous));
            previous = y;
        }
    }

    // Four bands and the level, each at most +4 dB, at 1 kHz.
    const auto ceiling = amplitude * std::pow (10.0, 16.0 / 20.0) * twoPi * hz / rate;
    std::cout << "  worst step " << worstStep << ", tone's own ceiling " << ceiling << std::endl;
    check (worstStep < ceiling, "no click: no sample to sample step beyond what the tone makes through the panel's gain");
}

// ---------------------------------------------------------------------------

void testRobustness()
{
    section ("Extremes, rates, block sizes, state");

    for (auto rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto block : { 1, 17, 512, 4097 })
        {
            Rig rig (rate, block);

            for (int s = 0; s < 2; ++s)
            {
                rig.set (strip (s, tide::pid::lowGain), 10.0f);
                rig.set (strip (s, tide::pid::midGain), 10.0f);
                rig.set (strip (s, tide::pid::highGain), 10.0f);
                rig.set (strip (s, tide::pid::level), 10.0f);
                rig.set (strip (s, tide::pid::highFreq), 5.0f);
                rig.set (strip (s, tide::pid::lowFreq), 0.0f);
                rig.set (strip (s, tide::pid::hpfIn), 1.0f);
            }

            rig.prepare();

            auto l = noise ((int) rate / 4, 15), r = noise ((int) rate / 4, 16);
            rig.run (l, r);

            bool finite = true;
            for (size_t i = 0; i < l.size(); ++i)
                finite = finite && std::isfinite (l[i]) && std::isfinite (r[i]) && std::abs (l[i]) < 20.0f;

            check (finite, "finite at full boost everywhere, " + juce::String (rate) + " Hz, block " + juce::String (block));
        }

    // Mono.
    {
        tide::TideProcessor processor;
        processor.setPlayConfigDetails (1, 1, 48000.0, 512);
        processor.apvts.getParameter (tide::pid::midSide)->setValueNotifyingHost (1.0f);
        processor.prepareToPlay (48000.0, 512);

        juce::AudioBuffer<float> buffer (1, 512);
        juce::MidiBuffer midi;
        auto x = noise (512, 17);
        buffer.copyFrom (0, 0, x.data(), 512);
        processor.processBlock (buffer, midi);
        check (std::memcmp (buffer.getReadPointer (0), x.data(), 512 * sizeof (float)) == 0, "mono, flat, M/S switch on: the input");
    }

    // State.
    {
        Rig a;
        a.set (strip (0, tide::pid::lowGain), 2.0f);
        a.set (strip (1, tide::pid::highShelf), 0.0f);
        a.set (strip (1, tide::pid::hpfFreq), 4.0f);
        a.set (tide::pid::cue, 2.0f);
        a.set (tide::pid::midSide, 1.0f);

        juce::MemoryBlock state;
        a.processor.getStateInformation (state);

        Rig b;
        b.processor.setStateInformation (state.getData(), (int) state.getSize());

        bool same = true;
        for (auto* p : a.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
                same = same && std::abs (ranged->getValue() - b.processor.apvts.getParameter (ranged->getParameterID())->getValue()) < 1.0e-6f;

        check (same, "every parameter survives a save and load");
    }
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    testPrototypes();
    testDigital();
    testThroughThePlugin();
    testNulls();
    testMidSideCueLink();
    testSwitching();
    testRobustness();

    std::cout << "\n" << (checks - failures) << " of " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
