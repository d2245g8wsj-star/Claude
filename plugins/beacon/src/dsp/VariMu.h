#pragma once

#include "Tube.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace beacon
{

/** The variable-mu gain law.

    A vari-mu compressor has no ratio control. Its gain element is a remote
    cutoff tube whose gain falls as the rectified control voltage pushes its
    grid negative, and it falls faster the further it goes. So the ratio is a
    property of level: barely compressing at the threshold, steepening as the
    signal goes past it.

    That is modelled directly. With e the rectified level's excess over the
    threshold, as a fraction of it,

        GR (dB) = K * (e + c * e^2)

    which in the feedback loop gives a ratio of about 1.5:1 at the threshold,
    2:1 six dB past it and 4:1 twenty dB past it, and in LIMIT about 4:1
    rising to beyond 10:1.
*/
namespace varimu
{
    struct Law
    {
        float k, c;
    };

    inline constexpr Law compress { 4.0f, 0.6f };
    inline constexpr Law limit    { 20.0f, 2.5f };

    /** Where the tube reaches cutoff: no more gain reduction is available. */
    inline constexpr float maxReductionDb = 40.0f;

    inline float reductionForExcess (const Law& law, float e) noexcept
    {
        return std::min (maxReductionDb, law.k * (e + law.c * e * e));
    }

    /** The feed-forward detector sees the input rather than the output, so it
        needs the law the other way round: the reduction for a given level
        above the threshold at the *input*. Solved once from the feedback law,
        so both topologies share one static curve and differ only in how they
        get there. */
    class ForwardTable
    {
    public:
        static constexpr float stepDb = 0.05f, rangeDb = 90.0f;
        static constexpr int   size   = (int) (rangeDb / stepDb) + 1;

        explicit ForwardTable (const Law& law)
        {
            // Walk the output level up finely and record where each input
            // level is crossed. Input level is monotonic in output level.
            float outputDb = 0.0f;

            for (int i = 0; i < size; ++i)
            {
                const auto inputDb = (float) i * stepDb;

                for (;;)
                {
                    const auto e  = std::pow (10.0f, outputDb / 20.0f) - 1.0f;
                    const auto gr = reductionForExcess (law, e);

                    if (outputDb + gr >= inputDb || gr >= maxReductionDb)
                    {
                        table[(size_t) i] = gr >= maxReductionDb ? std::max (0.0f, std::min (maxReductionDb, inputDb - outputDb))
                                                                 : inputDb - outputDb;
                        break;
                    }

                    outputDb += 0.0005f;
                }
            }
        }

        float reductionAt (float inputOverThresholdDb) const noexcept
        {
            if (inputOverThresholdDb <= 0.0f)
                return 0.0f;

            const auto position = inputOverThresholdDb / stepDb;
            const auto index    = (int) position;

            if (index >= size - 1)
                return std::min (maxReductionDb, table.back() + (inputOverThresholdDb - rangeDb));

            const auto t = position - (float) index;
            return table[(size_t) index] + t * (table[(size_t) index + 1] - table[(size_t) index]);
        }

        static const ForwardTable& forCompress() { static const ForwardTable t (compress); return t; }
        static const ForwardTable& forLimit()    { static const ForwardTable t (limit);    return t; }

    private:
        std::array<float, size> table {};
    };

    /** The rectifier and its timing network. Full wave, so a low note that
        is attacked quickly ripples the gain, as it does on the hardware.

        The release is program dependent, from a second, larger capacitor fed
        from the first through a resistor. It only charges on sustained
        compression, to a little below the first, and drains several times
        more slowly. After a short peak the first capacitor lets go at the
        set release time and the second has hardly charged; after a long
        passage the second holds part of the reduction and lets it go
        slowly, so a sustained mix is not pumped back up all at once. */
    class Envelope
    {
    public:
        static constexpr float holdFraction = 0.8f;   ///< how far the second capacitor charges, of the first
        static constexpr float holdCharge   = 0.5f;   ///< its charging time, seconds
        static constexpr float holdRelease  = 5.0f;   ///< its release, as a multiple of the set release

        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            setTimes (attackSeconds, releaseSeconds);
            holdChargeCoeff = 1.0f - std::exp (-1.0f / (holdCharge * (float) sampleRate));
            reset();
        }

        void reset() noexcept { value = held = 0.0f; }

        void setTimes (float attack, float release) noexcept
        {
            attackSeconds  = attack;
            releaseSeconds = release;
            attackCoeff      = 1.0f - std::exp (-1.0f / (attack  * (float) sampleRate));
            releaseCoeff     = 1.0f - std::exp (-1.0f / (release * (float) sampleRate));
            holdReleaseCoeff = 1.0f - std::exp (-1.0f / (holdRelease * release * (float) sampleRate));
        }

        float process (float rectified) noexcept
        {
            value += (rectified > value ? attackCoeff : releaseCoeff) * (rectified - value);

            const auto target = holdFraction * value;
            held += (target > held ? holdChargeCoeff : holdReleaseCoeff) * (target - held);

            return std::max (value, held);
        }

    private:
        double sampleRate { 48000.0 };
        float attackSeconds { 0.01f }, releaseSeconds { 0.5f };
        float attackCoeff { 0.01f }, releaseCoeff { 0.001f };
        float holdChargeCoeff { 0.0001f }, holdReleaseCoeff { 0.0001f };
        float value { 0.0f }, held { 0.0f };
    };

    /** Gain reduction for an envelope value, in dB. */
    inline float reduction (float envelope, float threshold, bool feedForward, bool limiting) noexcept
    {
        if (envelope <= threshold)
            return 0.0f;

        if (feedForward)
        {
            const auto overDb = 20.0f * std::log10 (envelope / threshold);
            return (limiting ? ForwardTable::forLimit() : ForwardTable::forCompress()).reductionAt (overDb);
        }

        return reductionForExcess (limiting ? limit : compress, envelope / threshold - 1.0f);
    }

    /** A remote cutoff tube: the grid is wound with a variable pitch, so the
        tube behaves like many triodes in parallel, each cutting off at a
        different grid voltage. As the bias goes more negative the close
        wound sections cut off one after another and the gain falls smoothly
        over tens of volts instead of all at once. Each section follows the
        3/2 power law; their cutoffs are spread from 1.5 to 30 volts, the
        remote ones carrying less current.

        The characteristic is tabulated once, current and slope, and read
        back with cubic Hermite interpolation, which is exact to far below
        anything audible and much cheaper than summing the sections. */
    class RemoteCutoff
    {
    public:
        static constexpr int    sections    = 24;
        static constexpr double nearCutoff  = 1.5, farCutoff = 30.0;
        static constexpr double weightPower = 2.5;
        static constexpr double knee        = 0.15;

        static constexpr float lowest = -32.0f, highest = 2.0f, step = 0.004f;
        static constexpr int   points = (int) ((highest - lowest) / step) + 2;

        static const RemoteCutoff& get()
        {
            static const RemoteCutoff tube;
            return tube;
        }

        /** Plate current at a grid voltage. In double precision: the cell
            works on the difference of two nearly equal currents, and a
            quiet signal's share of that difference is below what a float
            can resolve. */
        double current (double vg) const noexcept
        {
            const auto position = (std::clamp (vg, (double) lowest, (double) (highest - step)) - lowest) / step;
            const auto index    = (size_t) position;
            const auto t        = position - (double) index;

            const auto p0 = table[index].current, p1 = table[index + 1].current;
            const auto m0 = table[index].slope * step, m1 = table[index + 1].slope * step;

            const auto t2 = t * t, t3 = t2 * t;
            return (2.0 * t3 - 3.0 * t2 + 1.0) * p0 + (t3 - 2.0 * t2 + t) * m0
                 + (-2.0 * t3 + 3.0 * t2) * p1 + (t3 - t2) * m1;
        }

        static double exactCurrent (double vg)
        {
            double sum = 0.0;

            for (int k = 0; k < sections; ++k)
            {
                const auto c = nearCutoff * std::pow (farCutoff / nearCutoff, (double) k / (sections - 1));
                const auto v = vg + c;
                const auto s = v > 30.0 * knee ? v : knee * std::log1p (std::exp (v / knee));
                sum += std::pow (c, -weightPower) * s * std::sqrt (s);
            }

            return sum;
        }

        static double exactSlope (double vg)
        {
            constexpr double h = 1.0e-5;
            return (exactCurrent (vg + h) - exactCurrent (vg - h)) / (2.0 * h);
        }

    private:
        RemoteCutoff()
        {
            for (size_t i = 0; i < table.size(); ++i)
            {
                const auto vg = (double) lowest + (double) i * step;
                table[i] = { exactCurrent (vg), exactSlope (vg) };
            }
        }

        struct Point { double current, slope; };
        std::array<Point, (size_t) points> table {};
    };

    /** The gain cell: a pair of remote cutoff tubes in push-pull, the audio
        on their grids in opposite phase and the control voltage on both as
        bias. It is always in the signal path, compressing or not.

        Everything the cell does follows from that:
        - the gain at a given bias is the slope of the characteristic there,
          so the reduction the law asks for is turned into a bias voltage
          through a table made from the same characteristic;
        - push-pull cancels the even harmonics of a matched pair, leaving a
          mostly odd series; the pair is matched to a few percent, which lets
          a little 2nd harmonic through, more as the tubes near cutoff;
        - the curvature grows as the bias nears cutoff, so the distortion
          rises with the reduction, and with the swing on the grid, so it
          rises with input level too;
        - TUBE changes the swing on the grid, and the output is made up
          again after the cell, so the compression does not move;
        - the mismatch also leaves a little of the control voltage in the
          output when the bias moves: thump. On the unit it is trimmed out
          to almost nothing; here it is left at 1 % of the untrimmed amount.

        The grid swing is set so that the cell at rest measures 0.005 % THD
        on a 1 kHz sine at 0 dBu, inside the unit's specification of 0.01 %.
    */
    class Cell
    {
    public:
        static constexpr float restBias    = 1.0f;    ///< volts, with no reduction
        static constexpr float gainMismatch = 0.03f;  ///< second tube's current, relative
        static constexpr float biasMismatch = 0.03f;  ///< second tube's bias offset, volts
        static constexpr float thumpTrim    = 0.01f;  ///< control voltage left in the output
        static constexpr float tubeOutSwing = 0.1f;   ///< TUBE IN off: the grid driven 20 dB softer

        void prepare (double sampleRate) noexcept
        {
            dcCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * 5.0f / (float) sampleRate);
            reset();
        }

        void reset() noexcept { dcState = 0.0f; }

        /** drive is the TUBE gain, linear. */
        float process (float x, float reductionDb, float drive, bool tubeOn) noexcept
        {
            const auto& pair  = Pair::get();
            const auto gain   = std::pow (10.0f, -reductionDb / 20.0f);
            const auto bias   = (double) pair.biasFor (reductionDb);
            const auto swing  = (double) pair.gridScale * (tubeOn ? drive : tubeOutSwing);
            const auto u      = (double) x * swing;

            const auto rest   = pair.output (bias, 0.0);
            const auto signal = (pair.output (bias, u) - rest) / (pair.restSlope * swing);
            const auto thump  = thumpTrim * (rest - pair.restOutput) / (pair.restSlope * pair.gridScale);

            auto residual = (float) (signal + thump - (double) gain * x);
            dcState += dcCoeff * (residual - dcState);
            residual -= dcState;

            return gain * x + residual;
        }

        /** The push-pull pair, its bias table and grid scale, made once. */
        struct Pair
        {
            static const Pair& get()
            {
                static const Pair pair;
                return pair;
            }

            /** Difference of the two plate currents for a bias and a grid swing. */
            double output (double bias, double u) const noexcept
            {
                const auto& tube = RemoteCutoff::get();
                return tube.current (-bias + u) - (1.0 + gainMismatch) * tube.current (-bias - u + biasMismatch);
            }

            float biasFor (float reductionDb) const noexcept
            {
                const auto position = std::clamp (reductionDb, 0.0f, maxReductionDb) / biasStep;
                const auto index    = std::min ((size_t) position, biases.size() - 2);
                const auto t        = position - (float) index;
                return biases[index] + t * (biases[index + 1] - biases[index]);
            }

            static constexpr float biasStep = 0.05f;

            double restSlope { 1.0 }, restOutput { 0.0 }, gridScale { 1.0 };
            std::array<float, (size_t) (maxReductionDb / biasStep) + 2> biases {};

        private:
            Pair()
            {
                const auto slope = [] (double bias)
                {
                    return RemoteCutoff::exactSlope (-bias) + (1.0 + gainMismatch) * RemoteCutoff::exactSlope (-bias + biasMismatch);
                };

                const auto atRest = slope (restBias);
                restSlope  = atRest;
                restOutput = output (restBias, 0.0);

                // Bias for each reduction, by bisection on the slope.
                for (size_t i = 0; i < biases.size(); ++i)
                {
                    const auto target = atRest * std::pow (10.0, -(double) i * biasStep / 20.0);
                    double lo = restBias, hi = -RemoteCutoff::lowest - 1.0;

                    for (int n = 0; n < 60; ++n)
                    {
                        const auto mid = 0.5 * (lo + hi);
                        (slope (mid) > target ? lo : hi) = mid;
                    }

                    biases[i] = (float) (0.5 * (lo + hi));
                }

                // Grid volts per unit of signal, for 0.005 % at rest on a
                // 1 kHz sine at 0 dBu.
                const auto zeroDbu = std::pow (10.0, level::dbfsAtZeroDbu / 20.0);
                double lo = 1.0e-3, hi = 10.0;

                for (int n = 0; n < 50; ++n)
                {
                    const auto mid = std::sqrt (lo * hi);
                    (thdAtRest (mid * zeroDbu) < 0.005 ? lo : hi) = mid;
                }

                gridScale = lo;
            }

            double thdAtRest (double swing) const
            {
                constexpr int points = 512;
                std::array<double, 13> re {}, im {};

                for (int i = 0; i < points; ++i)
                {
                    const auto phase = 2.0 * 3.14159265358979323846 * i / points;
                    const auto y = output (restBias, swing * std::sin (phase));

                    for (int h = 1; h < (int) re.size(); ++h)
                    {
                        re[(size_t) h] += y * std::cos (phase * h);
                        im[(size_t) h] += y * std::sin (phase * h);
                    }
                }

                double sum = 0.0;
                for (size_t h = 2; h < re.size(); ++h)
                    sum += re[h] * re[h] + im[h] * im[h];

                return 100.0 * std::sqrt (sum) / std::hypot (re[1], im[1]);
            }
        };

    private:
        float dcCoeff { 0.001f };
        float dcState { 0.0f };
    };
}

/** What the detector listens to: a high pass to stop the low end pumping the
    whole mix, and a high bell to make it more sensitive to the top, each
    with its own switch.

    The unit's manual gives the ranges but not the slope or the boost. The
    high pass is first order, 6 dB per octave, as the sidechain filters of
    the same maker's bus compressor are; the bell is +6 dB at a Q of 0.7. */
class SidechainFilter
{
public:
    static constexpr float bellGainDb = 6.0f;
    static constexpr float bellQ      = 0.7f;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
        update();
    }

    void reset() noexcept { h1 = b1 = b2 = 0.0f; }

    void setTargets (bool highPassOn, float highPassHz, bool bellOn, float bellHz) noexcept
    {
        const auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-3f; };

        hpOn = highPassOn;
        hfOn = bellOn;

        if (moved (highPassHz, hpHz) || moved (bellHz, hfHz))
        {
            hpHz = highPassHz;
            hfHz = bellHz;
            update();
        }
    }

    float process (float x) noexcept
    {
        // First order high pass: the input less its low passed copy. The
        // filter keeps running while switched out, so switching is seamless.
        const auto w  = (x - h1) * a0;
        const auto lp = w + h1;
        h1 = lp + w;

        const auto hp = hpOn ? x - lp : x;

        // Bell: the input plus a boosted band pass copy, unity at its centre.
        const auto v3 = hp - b2;
        const auto v1 = c1 * b1 + c2 * v3;
        const auto v2 = b2 + c2 * b1 + c3 * v3;
        b1 = 2.0f * v1 - b1;
        b2 = 2.0f * v2 - b2;

        return hfOn ? hp + bellBoost * v1 / bellQ : hp;
    }

private:
    void update() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        const auto nyquistSafe = [this] (float hz) { return std::min ((double) hz, 0.45 * sampleRate); };

        const auto g = (float) std::tan (pi * nyquistSafe (hpHz) / sampleRate);
        a0 = g / (1.0f + g);

        const auto gb = (float) std::tan (pi * nyquistSafe (hfHz) / sampleRate);
        c1 = 1.0f / (1.0f + gb * (gb + 1.0f / bellQ));
        c2 = gb * c1;
        c3 = gb * c2;
        bellBoost = std::pow (10.0f, bellGainDb / 20.0f) - 1.0f;
    }

    double sampleRate { 48000.0 };
    float hpHz { 20.0f }, hfHz { 8000.0f };
    bool  hpOn { true }, hfOn { false };

    float a0 { 0.0f }, c1 { 0.0f }, c2 { 0.0f }, c3 { 0.0f }, bellBoost { 1.0f };
    float h1 { 0.0f }, b1 { 0.0f }, b2 { 0.0f };
};

} // namespace beacon
