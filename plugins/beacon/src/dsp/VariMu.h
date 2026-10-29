#pragma once

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

    /** The rectifier and its timing capacitor. Full wave, so a low note that
        is attacked quickly ripples the gain, as it does on the hardware. */
    class Envelope
    {
    public:
        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            setTimes (attackSeconds, releaseSeconds);
            reset();
        }

        void reset() noexcept { value = 0.0f; }

        void setTimes (float attack, float release) noexcept
        {
            attackSeconds  = attack;
            releaseSeconds = release;
            attackCoeff  = 1.0f - std::exp (-1.0f / (attack  * (float) sampleRate));
            releaseCoeff = 1.0f - std::exp (-1.0f / (release * (float) sampleRate));
        }

        float process (float rectified) noexcept
        {
            value += (rectified > value ? attackCoeff : releaseCoeff) * (rectified - value);
            return value;
        }

        float get() const noexcept { return value; }

    private:
        double sampleRate { 48000.0 };
        float attackSeconds { 0.01f }, releaseSeconds { 0.5f };
        float attackCoeff { 0.01f }, releaseCoeff { 0.001f };
        float value { 0.0f };
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

    /** The gain cell: the vari-mu tube itself. It is always in the signal
        path, compressing or not, so it always colours a little.

        Push-pull, so mostly odd harmonics, with a small imbalance that lets
        some even through. The curvature has two parts: the tube's own, which
        the TUBE control drives into (up to 15 dB harder, made up again after
        the cell, the way the unit's adaptive circuit keeps the compression
        unchanged), and a part that grows with the reduction because the grid
        is being pushed towards cutoff.

        The curve acts on the cell's output swing rather than its input: the
        reduction is already a compression of the input, and bending the input
        as well would compress it a second time, past what the law says. */
    class Cell
    {
    public:
        /** Calibrated to the unit's specification of below 0.01 % THD+N at
            0 dBu with the tube at rest; driven 15 dB harder it reaches a
            couple of percent at the operating level. */
        static constexpr float tubeCurvature  = 0.18f;
        static constexpr float curvaturePerDb = 0.12f;
        static constexpr float imbalance      = 0.003f;

        void prepare (double sampleRate) noexcept
        {
            dcCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * 5.0f / (float) sampleRate);
            dcState = 0.0f;
        }

        void reset() noexcept { dcState = 0.0f; }

        /** drive is the TUBE gain, linear; tubeOn false leaves only the
            curvature the reduction itself brings. */
        float process (float x, float reductionDb, float drive, bool tubeOn) noexcept
        {
            const auto gain = std::pow (10.0f, -reductionDb / 20.0f);
            const auto v    = gain * x;
            // A tube driven harder bends faster than in proportion.
            const auto h = (tubeOn ? tubeCurvature * drive * std::sqrt (drive) : 0.0f) + curvaturePerDb * reductionDb;

            if (h <= 1.0e-5f)
            {
                dcState *= 1.0f - dcCoeff;
                return v;
            }

            static const float offset = std::tanh (imbalance);
            static const float slope  = 1.0f - offset * offset;

            auto residual = (std::tanh (v * h + imbalance) - offset) / (slope * h) - v;
            dcState += dcCoeff * (residual - dcState);
            residual -= dcState;

            return v + residual;
        }

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
