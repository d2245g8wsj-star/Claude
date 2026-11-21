#pragma once

/*
    One stereo channel of the console: the signal path of dsp/model.py.

      cal -> pre-shelves -> [oversample] shaper [down] -> inverse shelves -> tone EQ -> DC
          -> gain (+ spread, auto gain) -> crosstalk (BUS) -> gated noise -> uncal

    The shaper and every filter are the reference model's, from the numbers
    in gen/Models.h. What the plugin adds around it (cal, the drive table,
    auto gain, AGE) is outside the calibrated path and is off in the checks
    that compare against the reference.
*/

#include <juce_dsp/juce_dsp.h>

#include "Biquad.h"
#include "../gen/Models.h"

#include <array>
#include <cmath>
#include <memory>
#include <random>
#include <vector>

namespace nazare
{

/** True for anything but an exact zero: the model's terms that are off are stored as 0. */
inline bool active (double x) noexcept { return std::abs (x) > 0.0; }

/** Per-channel component values, fixed per channel number (1-48): neighbouring
    channels land far apart on a golden-ratio sequence. Same as model.py. */
struct Spread
{
    double gainDb = 0, k1 = 1, clip = 1;

    static Spread forChannel (int channel, double amount)
    {
        auto frac = [] (double x) { return x - std::floor (x); };
        const double r0 = frac (channel * 0.6180339887 + 0.13) * 2 - 1;
        const double r1 = frac (channel * 0.7548776662 + 0.29) * 2 - 1;
        const double r2 = frac (channel * 0.5698402910 + 0.61) * 2 - 1;
        return { 0.15 * amount * r0, std::pow (10.0, 2.0 * amount * r1 / 20.0), 1.0 + 0.04 * amount * r2 };
    }
};

/** The stage's constants at one drive setting. */
struct Shaper
{
    double k1 = 0, a2 = 0, a3 = 0, xover = 0, tubeG = 0, tubeB = 0, slope = 1, clipC = 1, clipP = 8;
    double G = 1, outScale = 1, biasScale = 1;
    bool tube = false;

    static Shaper at (const gen::Model& m, double driveDb, double age = 0.0, const Spread* sp = nullptr)
    {
        Shaper s;
        const double D = std::pow (10.0, (driveDb - 6.0) / 20.0), ageScale = 1.0 + 0.6 * age;
        s.k1 = m.k1 * ageScale; s.a2 = m.a2 * ageScale; s.a3 = m.a3; s.clipC = m.clipC; s.clipP = m.clipP; s.xover = m.xover;
        if (sp != nullptr) { s.k1 *= sp->k1; s.a2 *= sp->k1; s.clipC *= sp->clip; }

        if (! m.pregain)
        {
            s.k1 *= D; s.a2 *= D; s.a3 *= D;
        }
        else
        {
            // above drive 0 the valve gets real input gain; below, its colour and bias shrink instead
            s.biasScale = std::pow (10.0, std::min (driveDb, 0.0) / 20.0);
            const double gBase = std::pow (10.0, (std::max (driveDb, 0.0) - 6.0) / 20.0) * m.preGain;
            s.G = gBase * s.biasScale;
            s.k1 *= s.biasScale; s.a2 *= s.biasScale; s.a3 *= s.biasScale;
            s.outScale = 1.0 / (std::pow (gBase, m.makeup) * s.biasScale);
        }

        if (m.tubeG > 0)
        {
            s.tube = true; s.tubeG = m.tubeG; s.tubeB = m.tubeB;
            const double t0 = std::tanh (m.tubeG * m.tubeB);
            s.slope = m.tubeG * (1 - t0 * t0);
        }
        return s;
    }

    /** m: transient memory, 1 when steady. */
    double operator() (double u, double m = 1.0) const noexcept
    {
        const double x = u * G;
        double v = x + k1 * m * std::abs (x) + a2 * m * x * x + a3 * x * x * x;
        if (active (xover)) v += xover * 1.0e-3 * std::tanh (x / 1.0e-3);
        if (tube)
        {
            const double bb = tubeB * m * biasScale;
            v = (std::tanh (tubeG * (v + bb)) - std::tanh (tubeG * bb)) / slope;
        }
        v = v / std::pow (1.0 + std::pow (std::abs (v) / clipC, clipP), 1.0 / clipP);
        return v * outScale;
    }
};

inline double modelGainDb (const gen::Model& m, double driveDb)
{
    double g = m.gainDb;
    if (! m.pregain)
        g += m.bump * (std::pow (10.0, std::min (driveDb, 24.0) / 20.0) - std::pow (10.0, 6.0 / 20.0));
    return g;
}

/** Knob position 0..1 -> {drive dB, auto gain dB}, from the generated table. */
inline std::pair<double, double> driveForKnob (int model, double knob)
{
    const double pos = juce::jlimit (0.0, 1.0, knob) * (gen::driveTableSize - 1);
    const int i = juce::jmin ((int) pos, gen::driveTableSize - 2);
    const double f = pos - i;
    const auto& t = gen::driveTable[model];
    return { t[i][0] + f * (t[i + 1][0] - t[i][0]), t[i][1] + f * (t[i + 1][1] - t[i][1]) };
}

struct Settings
{
    int    model      = 3;
    double driveDb    = 6.0;
    double autoGainDb = 0.0;
    bool   autoGain   = true;
    double spread     = 0.45;
    bool   matched    = false;
    int    channel    = 7;
    double age        = 0.2;
    double noise      = 0.3;
    double calDbfs    = -18.0;   // where 0 VU sits
    bool   bus        = false;
    double outputDb   = 0.0;
    bool   memoryOn   = true;
};

class Console
{
public:
    static constexpr int maxOrder = 3;            // 2^3 = 8x

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        maxBlockSize = juce::jmax (1, maxBlock);
        for (int o = 0; o <= maxOrder; ++o)
        {
            oversamplers[(size_t) o] = std::make_unique<juce::dsp::Oversampling<double>> (
                2, (size_t) o, juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple, true, true);
            oversamplers[(size_t) o]->initProcessing ((size_t) maxBlockSize);
        }
        memory.assign ((size_t) maxBlockSize, 1.0);
        memoryR.assign ((size_t) maxBlockSize, 1.0);
        builtModel = -1;
        reset();
    }

    void reset()
    {
        for (auto& o : oversamplers) if (o) o->reset();
        for (auto& c : ch) c.reset();
        gate = 0;
    }

    void setOrder (int order) noexcept { activeOrder = juce::jlimit (0, maxOrder, order); }
    int  latency() const noexcept { return oversamplers[(size_t) activeOrder] ? (int) std::lround (oversamplers[(size_t) activeOrder]->getLatencyInSamples()) : 0; }
    static int latencyFor (int order, double sampleRate, int block)
    {
        juce::dsp::Oversampling<double> o (2, (size_t) order, juce::dsp::Oversampling<double>::filterHalfBandFIREquiripple, true, true);
        o.initProcessing ((size_t) block); juce::ignoreUnused (sampleRate);
        return (int) std::lround (o.getLatencyInSamples());
    }

    /** Processes up to the prepared block size, in place. */
    void process (double* left, double* right, int n, const Settings& s)
    {
        jassert (n <= maxBlockSize);
        const auto& m = gen::models[s.model];
        if (s.model != builtModel) build (m);

        const double cal = std::pow (10.0, (-18.0 - s.calDbfs) / 20.0);
        double* io[2] = { left, right };
        const double factor = (double) (1 << activeOrder);

        // calibrate, pre-shelves, and the transient memory measured on what goes into the stage
        double inPeak = 0;
        for (int c = 0; c < 2; ++c)
        {
            auto& st = ch[(size_t) c];
            double* mem = c == 0 ? memory.data() : memoryR.data();
            for (int i = 0; i < n; ++i)
            {
                inPeak = std::max (inPeak, std::abs (io[c][i]));
                double y = io[c][i] * cal;
                for (auto& f : st.pre) y = f.process (y);
                io[c][i] = y;
                const double e = std::abs (y);
                st.fast += (e - st.fast) * fastCoef;
                st.slow += (e - st.slow) * slowCoef;
                mem[i] = s.memoryOn && m.memory > 0 ? 1.0 + m.memory * std::tanh (std::max (st.fast - st.slow, 0.0) / 0.25) : 1.0;
            }
        }

        // the stage, oversampled
        Spread sp[2];
        const bool spreading = ! s.matched && s.spread > 0;
        for (int c = 0; c < 2; ++c)
            sp[c] = spreading ? Spread::forChannel (s.channel + c, s.spread) : Spread{};
        const Shaper shaper[2] = { Shaper::at (m, s.driveDb, s.age, spreading ? &sp[0] : nullptr),
                                   Shaper::at (m, s.driveDb, s.age, spreading ? &sp[1] : nullptr) };

        juce::dsp::AudioBlock<double> block (io, 2, (size_t) n);
        auto& os = *oversamplers[(size_t) activeOrder];
        auto up = os.processSamplesUp (block);
        const int f = 1 << activeOrder;
        for (int c = 0; c < 2; ++c)
        {
            double* d = up.getChannelPointer ((size_t) c);
            const double* mem = c == 0 ? memory.data() : memoryR.data();
            for (int i = 0; i < (int) up.getNumSamples(); ++i)
                d[i] = shaper[c] (d[i], mem[i / f]);
        }
        os.processSamplesDown (block);
        juce::ignoreUnused (factor);

        // inverse shelves, tone EQ, DC, gain
        const double base = modelGainDb (m, s.driveDb) + (s.autoGain ? s.autoGainDb : 0.0);
        for (int c = 0; c < 2; ++c)
        {
            auto& st = ch[(size_t) c];
            const double g = std::pow (10.0, (base + sp[c].gainDb) / 20.0) / cal;
            for (int i = 0; i < n; ++i)
            {
                double y = io[c][i];
                for (auto& fl : st.post) y = fl.process (y);
                for (auto& fl : st.eq) y = fl.process (y);
                io[c][i] = y * g;
            }
        }

        // BUS: each side leaks into the other, flat plus a low and a high component
        if (s.bus)
        {
            for (int i = 0; i < n; ++i)
            {
                const double l = left[i], r = right[i];
                left[i]  += leak (ch[1], r, m);
                right[i] += leak (ch[0], l, m);
            }
        }

        // noise, only while there is signal; digital silence stays silent
        const double level = s.noise < 0.005 ? 0.0 : std::pow (10.0, (m.noiseDbfs - 12.0 + 24.0 * s.noise + 8.0 * s.age) / 20.0);
        const bool open = inPeak > 1.0e-4;
        for (int i = 0; i < n; ++i)
        {
            gate += ((open ? 1.0 : 0.0) - gate) * gateCoef;
            const double gn = std::min (gate * 4.0, 1.0) * level;
            if (gn <= 0) continue;
            for (int c = 0; c < 2; ++c)
                io[c][i] += gn * ch[(size_t) c].noiseLp.process (normal (rng));
        }

        const double out = std::pow (10.0, s.outputDb / 20.0);
        if (active (out - 1.0))
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i) io[c][i] *= out;
    }

private:
    struct Channel
    {
        std::vector<Biquad> pre, post, eq;
        Biquad xtLow, xtHigh, noiseLp;
        double fast = 0, slow = 0;
        void reset() { for (auto* v : { &pre, &post, &eq }) for (auto& b : *v) b.reset(); xtLow.reset(); xtHigh.reset(); noiseLp.reset(); fast = slow = 0; }
    };

    double leak (Channel& from, double x, const gen::Model& m) noexcept
    {
        return m.xtFlat * x + m.xtLf * from.xtLow.process (x) + m.xtHf * from.xtHigh.process (x);
    }

    void build (const gen::Model& m)
    {
        for (auto& c : ch)
        {
            c.pre.clear(); c.post.clear(); c.eq.clear();
            if (active (m.lfG)) c.pre.push_back ({ design (Kind::lowShelf,  m.lfF, fs, m.lfG, 0.6) });
            if (active (m.hfG)) c.pre.push_back ({ design (Kind::highShelf, m.hfF, fs, m.hfG, 0.6) });
            for (auto& b : c.pre) c.post.push_back ({ b.k.inverse() });
            if (m.hpF > 0) c.eq.push_back ({ design (m.hpOrder == 2 ? Kind::highPass2 : Kind::highPass1, m.hpF, fs) });
            if (active (m.lsG)) c.eq.push_back ({ design (Kind::lowShelf, m.lsF, fs, m.lsG, 0.6) });
            if (active (m.pkG)) c.eq.push_back ({ design (Kind::peak, m.pkF, fs, m.pkG, m.pkQ) });
            if (active (m.hsG)) c.eq.push_back ({ design (Kind::highShelf, m.hsF, fs, m.hsG, 0.6) });
            c.eq.push_back ({ design (Kind::highPass1, m.dcF, fs) });
            c.xtLow  = { design (Kind::lowPass1,  m.xtLfF, fs) };
            c.xtHigh = { design (Kind::highPass1, m.xtHfF, fs) };
            c.noiseLp = { design (Kind::lowPass1, std::min (16000.0, 0.45 * fs), fs) };
        }
        fastCoef = 1.0 - std::exp (-1.0 / (0.004 * fs));
        slowCoef = 1.0 - std::exp (-1.0 / (0.15 * fs));
        gateCoef = 1.0 - std::exp (-1.0 / (0.05 * fs));
        builtModel = (int) (&m - gen::models);
    }

    double fs = 48000;
    int maxBlockSize = 512, activeOrder = 2, builtModel = -1;
    std::array<std::unique_ptr<juce::dsp::Oversampling<double>>, maxOrder + 1> oversamplers;
    std::array<Channel, 2> ch;
    std::vector<double> memory, memoryR;
    double fastCoef = 0, slowCoef = 0, gateCoef = 0, gate = 0;
    std::mt19937 rng { 0x4e5a };
    std::normal_distribution<double> normal { 0.0, 1.0 };
};

} // namespace nazare
