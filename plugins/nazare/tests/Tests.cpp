/*
    Offline checks for the claims Nazare makes about itself.

    First, that the plugin is the reference model: the shaper sample for
    sample, and the whole chain's distortion and gain at the calibration point
    (dsp/model.py, via the generated vectors.h). Then the promises of the
    panel: the drive zones, auto gain, no aliasing, silence stays silent,
    latency reported exactly, bypass nulls, the stereo spread, matched pairs,
    and that sessions open as they were saved.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "indiewave/SessionCompat.h"
#include "vectors.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace
{

int failures = 0, checks = 0;

void check (bool condition, const juce::String& what)
{
    ++checks;
    if (! condition) { ++failures; std::cout << "  FAIL  " << what << std::endl; }
}

void section (const juce::String& name) { std::cout << "\n== " << name << std::endl; }

constexpr double fs = 48000.0;
constexpr double twoPi = juce::MathConstants<double>::twoPi;
double db (double x) { return 20.0 * std::log10 (std::max (x, 1.0e-15)); }

/** Harmonic amplitudes of a steady tone over a whole number of cycles. */
std::vector<double> harmonics (const std::vector<double>& x, double f, int count = 10)
{
    std::vector<double> h;
    const auto n = x.size();
    for (int k = 1; k <= count; ++k)
    {
        if (f * k >= 0.45 * fs) { h.push_back (0); continue; }
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) { const double a = twoPi * f * k * (double) i / fs; re += x[i] * std::cos (a); im += x[i] * std::sin (a); }
        h.push_back (2.0 * std::hypot (re, im) / (double) n);
    }
    return h;
}

double thdDb (const std::vector<double>& h)
{
    double s = 0; for (size_t k = 1; k < h.size(); ++k) s += h[k] * h[k];
    return db (std::sqrt (s) / h[0]);
}

nazare::Settings plain (int model, double driveDb)
{
    nazare::Settings s;
    s.model = model; s.driveDb = driveDb; s.autoGain = false; s.spread = 0; s.age = 0; s.noise = 0;
    s.memoryOn = false; s.calDbfs = -18; s.bus = false; s.outputDb = 0;
    return s;
}

/** Runs the console engine on a stereo tone and returns the left output after it has settled. */
std::vector<double> runConsole (const nazare::Settings& s, double freq, double levelDbfs, int order = 2, bool right = false)
{
    nazare::Console c; c.prepare (fs, 64); c.setOrder (order);
    const int settle = 24000, keep = 48000, total = settle + keep;
    std::vector<double> l (64), r (64), out;
    const double a = std::pow (10.0, levelDbfs / 20.0);
    for (int start = 0; start < total; start += 64)
    {
        for (int i = 0; i < 64; ++i) { l[(size_t) i] = r[(size_t) i] = a * std::sin (twoPi * freq * (start + i) / fs); }
        c.process (l.data(), r.data(), 64, s);
        if (start >= settle) for (int i = 0; i < 64; ++i) out.push_back (right ? r[(size_t) i] : l[(size_t) i]);
    }
    return out;
}

void set (nazare::NazareProcessor& p, const char* id, float plainValue)
{
    auto* par = p.apvts.getParameter (id);
    par->setValueNotifyingHost (par->convertTo0to1 (plainValue));
}

std::unique_ptr<nazare::NazareProcessor> make (std::function<void (nazare::NazareProcessor&)> setup = {})
{
    auto p = std::make_unique<nazare::NazareProcessor>();
    p->setPlayConfigDetails (2, 2, fs, 512);
    set (*p, nazare::pid::age, 0); set (*p, nazare::pid::noise, 0); set (*p, nazare::pid::spread, 0);
    if (setup) setup (*p);
    p->prepareToPlay (fs, 512);
    return p;
}

/** Plays a signal through the processor in 512-sample blocks. */
juce::AudioBuffer<float> play (nazare::NazareProcessor& p, const juce::AudioBuffer<float>& in)
{
    juce::AudioBuffer<float> out (in);
    juce::MidiBuffer midi;
    for (int start = 0; start < out.getNumSamples(); start += 512)
    {
        const int n = juce::jmin (512, out.getNumSamples() - start);
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), 2, start, n);
        p.processBlock (view, midi);
    }
    return out;
}

juce::AudioBuffer<float> tone (double freq, double levelDbfs, int samples)
{
    juce::AudioBuffer<float> b (2, samples);
    const double a = std::pow (10.0, levelDbfs / 20.0);
    for (int i = 0; i < samples; ++i) for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) (a * std::sin (twoPi * freq * i / fs)));
    return b;
}

std::vector<double> tail (const juce::AudioBuffer<float>& b, int channel, int count)
{
    std::vector<double> v;
    for (int i = b.getNumSamples() - count; i < b.getNumSamples(); ++i) v.push_back (b.getSample (channel, i));
    return v;
}

// ---------------------------------------------------------------------------
void testShaper()
{
    section ("The shaper is the reference model's, sample for sample");
    double worst = 0;
    for (const auto& c : nazare::vectors::cases)
    {
        const auto y = nazare::Shaper::at (nazare::gen::models[c.model], c.drive) (c.in);
        worst = std::max (worst, std::abs (y - c.out) / std::max (1.0e-9, std::abs (c.out)));
    }
    std::cout << "  worst relative error " << worst << std::endl;
    check (worst < 1.0e-9, "shaper matches dsp/model.py within 1e-9");
}

void testCalibration()
{
    section ("Distortion and gain at the calibration point match the reference model");
    for (const auto& lv : nazare::vectors::levels)
    {
        const auto y = runConsole (plain (lv.model, 6.0), 1000.0, lv.level);
        const auto h = harmonics (y, 1000.0);
        const double thd = thdDb (h), gain = db (h[0]) - lv.level;
        const auto name = juce::String (nazare::gen::models[lv.model].name) + " at " + juce::String (lv.level) + " dBFS";
        std::cout << "  " << name.paddedRight (' ', 26) << " THD " << juce::String (thd, 1) << " (model " << juce::String (lv.thdDb, 1)
                  << ")  gain " << juce::String (gain, 3) << " (model " << juce::String (lv.gainDb, 3) << ")" << std::endl;
        // below -110 dB the oversampling filters' own floor shows; the model's number is only an upper bound there
        if (lv.thdDb > -110) check (std::abs (thd - lv.thdDb) < 1.0, name + ": THD within 1 dB of the model");
        else                 check (thd < -100, name + ": THD below -100 dB");
        check (std::abs (gain - lv.gainDb) < 0.05, name + ": gain within 0.05 dB of the model");
    }
}

void testDriveZones()
{
    section ("The drive knob lands in its zones on every console");
    for (int m = 0; m < nazare::gen::numModels; ++m)
        for (double knob : { 0.2, 0.5, 0.65 })
        {
            auto p = make ([m, knob] (auto& pr) { set (pr, nazare::pid::console, (float) m); set (pr, nazare::pid::drive, (float) knob); });
            const auto out = play (*p, tone (1000.0, -18.0, 72000));
            const auto h = harmonics (tail (out, 0, 48000), 1000.0);
            const double target = 20.0 * std::log10 (std::pow (10.0, -3.3 + knob * 4.7) / 100.0);
            const double thd = thdDb (h), level = db (h[0]) + 18.0;
            const auto t = nazare::gen::driveTable[m][juce::roundToInt (knob * (nazare::gen::driveTableSize - 1))][0];
            const auto name = juce::String (nazare::gen::models[m].name) + " knob " + juce::String (knob, 2) + " (" + nazare::zoneName (knob) + ")";
            if (t > -139 && t < 59.9)   // inside the table's reach
                check (std::abs (thd - target) < 3.0, name + ": THD " + juce::String (thd, 1) + " dB, zone wants " + juce::String (target, 1));
            check (std::abs (level) < 1.0, name + ": auto gain holds the level (" + juce::String (level, 2) + " dB)");
        }
}

void testAliasing()
{
    section ("No aliasing: a 9973 Hz tone at 0 dBFS, drive 18");
    for (int m : { 2, 5 })
    {
        nazare::Console c; c.prepare (fs, 64); c.setOrder (2);
        auto s = plain (m, 18.0);
        std::vector<double> l (64), r (64), out;
        for (int start = 0; start < 96000; start += 64)
        {
            for (int i = 0; i < 64; ++i) l[(size_t) i] = r[(size_t) i] = std::sin (twoPi * 9973.0 * (start + i) / fs);
            c.process (l.data(), r.data(), 64, s);
            if (start >= 48000) out.insert (out.end(), l.begin(), l.end());
        }
        auto level = [&out] (double f)
        {
            double re = 0, im = 0, wsum = 0;
            for (size_t i = 0; i < out.size(); ++i)
            {
                const double w = 0.35875 - 0.48829 * std::cos (twoPi * i / out.size()) + 0.14128 * std::cos (2 * twoPi * i / out.size()) - 0.01168 * std::cos (3 * twoPi * i / out.size());
                re += w * out[i] * std::cos (twoPi * f * i / fs); im += w * out[i] * std::sin (twoPi * f * i / fs); wsum += w;
            }
            return std::hypot (re, im) / wsum;
        };
        const double alias = db (level (48000.0 - 3 * 9973.0) / level (9973.0));
        std::cout << "  " << nazare::gen::models[m].name << ": folded third harmonic " << juce::String (alias, 1) << " dB" << std::endl;
        check (alias < -85.0, juce::String (nazare::gen::models[m].name) + ": alias below -85 dB at 4x");
    }
}

void testSilenceLatencyBypass()
{
    section ("Silence stays silent, latency is reported exactly, bypass nulls");
    {
        auto p = make ([] (auto& pr) { set (pr, nazare::pid::noise, 1.0f); });
        juce::AudioBuffer<float> zero (2, 48000); zero.clear();
        const auto out = play (*p, zero);
        check (! (out.getMagnitude (0, out.getNumSamples()) > 0.0f), "digital silence in, digital silence out, noise at maximum");
    }
    for (int order = 0; order < 4; ++order)
    {
        auto p = make ([order] (auto& pr) { set (pr, nazare::pid::oversampling, (float) order); set (pr, nazare::pid::console, 0.0f); set (pr, nazare::pid::drive, 0.0f); });
        juce::AudioBuffer<float> imp (2, 4096); imp.clear(); imp.setSample (0, 100, 0.5f); imp.setSample (1, 100, 0.5f);
        const auto out = play (*p, imp);
        int at = 0; float best = 0;
        for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > best) { best = std::abs (out.getSample (0, i)); at = i; }
        check (at - 100 == p->getLatencySamples(), juce::String (1 << order) + "x: impulse arrives " + juce::String (at - 100) + " samples late, reported " + juce::String (p->getLatencySamples()));
    }
    {
        auto p = make ([] (auto& pr) { set (pr, nazare::pid::bypass, 1.0f); set (pr, nazare::pid::drive, 1.0f); });
        auto in = tone (440.0, -6.0, 24000);
        const auto out = play (*p, in);
        const int lat = p->getLatencySamples();
        float worst = 0;
        for (int i = lat; i < out.getNumSamples(); ++i) worst = std::max (worst, std::abs (out.getSample (0, i) - in.getSample (0, i - lat)));
        check (! (worst > 0.0f), "bypass is the input, delayed by the reported latency, bit for bit");
    }
}

void testStereo()
{
    section ("Spread makes the channels differ; a matched pair does not");
    for (bool matched : { false, true })
    {
        auto p = make ([matched] (auto& pr) { set (pr, nazare::pid::spread, 1.0f); set (pr, nazare::pid::matched, matched ? 1.0f : 0.0f); set (pr, nazare::pid::console, 1.0f); });
        const auto out = play (*p, tone (1000.0, -6.0, 72000));
        const auto hl = harmonics (tail (out, 0, 48000), 1000.0), hr = harmonics (tail (out, 1, 48000), 1000.0);
        const double dGain = db (hl[0]) - db (hr[0]), dH2 = (db (hl[1]) - db (hl[0])) - (db (hr[1]) - db (hr[0]));
        if (matched)
        {
            float worst = 0; for (int i = 0; i < out.getNumSamples(); ++i) worst = std::max (worst, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            check (! (worst > 0.0f), "matched pair: left and right identical");
        }
        else
        {
            std::cout << "  channels 7/8 at full spread: gain " << juce::String (dGain, 3) << " dB, H2 " << juce::String (dH2, 2) << " dB apart" << std::endl;
            check (std::abs (dGain) > 0.01 && std::abs (dH2) > 0.3, "spread: left and right differ in gain and H2");
        }
    }
}

void testSwitchingAndRobustness()
{
    section ("Changing console mid-stream, and every console at every drive, stays finite");
    auto p = make();
    auto in = tone (220.0, -3.0, 96000);
    juce::MidiBuffer midi;
    bool finite = true;
    for (int start = 0, step = 0; start < in.getNumSamples(); start += 512, ++step)
    {
        if (step % 10 == 0) set (*p, nazare::pid::console, (float) ((step / 10) % 6));
        if (step % 7 == 0)  set (*p, nazare::pid::drive, (float) ((step % 5) / 4.0));
        juce::AudioBuffer<float> view (in.getArrayOfWritePointers(), 2, start, juce::jmin (512, in.getNumSamples() - start));
        p->processBlock (view, midi);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < view.getNumSamples(); ++i) finite &= std::isfinite (view.getSample (c, i)) && std::abs (view.getSample (c, i)) < 8.0f;
    }
    check (finite, "no NaN, no infinity, nothing runaway while switching");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    testShaper();
    testCalibration();
    testDriveZones();
    testAliasing();
    testSilenceLatencyBypass();
    testStereo();
    testSwitchingAndRobustness();

    section ("Sessions saved by released versions open unchanged");
    indiewave::compat::run ([] { return std::make_unique<nazare::NazareProcessor>(); },
                            juce::File (INDIEWAVE_COMPAT_DIR), INDIEWAVE_VERSION, check);

    std::cout << "\n" << (checks - failures) << " of " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
