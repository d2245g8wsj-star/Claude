#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace kiln
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

    /** The gain cell. Push-pull, so mostly odd harmonics, with a small
        imbalance that lets some even through. The curvature grows with the
        reduction because the tubes are being driven towards cutoff, so an
        idle cell is exactly linear and a hard working one is audibly not.

        The curve acts on the cell's output swing rather than its input: the
        reduction is already a compression of the input, and bending the input
        as well would compress it a second time, past what the law says. */
    class Cell
    {
    public:
        static constexpr float curvaturePerDb = 0.12f;
        static constexpr float imbalance      = 0.08f;

        void prepare (double sampleRate) noexcept
        {
            dcCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * 5.0f / (float) sampleRate);
            dcState = 0.0f;
        }

        void reset() noexcept { dcState = 0.0f; }

        float process (float x, float reductionDb) noexcept
        {
            const auto gain = std::pow (10.0f, -reductionDb / 20.0f);
            const auto h    = curvaturePerDb * reductionDb;

            if (h <= 1.0e-5f)
            {
                dcState *= 1.0f - dcCoeff;
                return gain * x;
            }

            static const float offset = std::tanh (imbalance);
            static const float slope  = 1.0f - offset * offset;

            const auto v = gain * x;
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
    whole mix, and a high shelf to make it more sensitive to the top. */
class SidechainFilter
{
public:
    static constexpr float shelfGainDb = 6.0f;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
        update();
    }

    void reset() noexcept { h1 = h2 = s1 = 0.0f; }

    void setTargets (float highPassHz, float shelfHz, bool shelfOn) noexcept
    {
        const auto moved = [] (float a, float b) { return std::abs (a - b) > 1.0e-3f; };

        if (moved (highPassHz, hpHz) || moved (shelfHz, hfHz) || shelfOn != hfOn)
        {
            hpHz = highPassHz;
            hfHz = shelfHz;
            hfOn = shelfOn;
            update();
        }
    }

    float process (float x) noexcept
    {
        // Second order high pass, Butterworth, state variable.
        const auto hp = (x - (2.0f * r + g) * h1 - h2) * hpNorm;
        const auto bp = g * hp + h1;
        const auto lp = g * bp + h2;
        h1 = g * hp + bp;
        h2 = g * bp + lp;

        if (! hfOn)
            return hp;

        // First order high shelf: the input plus a boosted copy of its top.
        const auto v  = (hp - s1) * a1;
        const auto low = v + s1;
        s1 = low + v;

        return hp + shelfBoost * (hp - low);
    }

private:
    void update() noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        const auto nyquistSafe = [this] (float hz) { return std::min ((double) hz, 0.45 * sampleRate); };

        g      = (float) std::tan (pi * nyquistSafe (hpHz) / sampleRate);
        r      = 0.70710678f;
        hpNorm = 1.0f / (1.0f + 2.0f * r * g + g * g);

        const auto gs = (float) std::tan (pi * nyquistSafe (hfHz) / sampleRate);
        a1 = gs / (1.0f + gs);
        shelfBoost = std::pow (10.0f, shelfGainDb / 20.0f) - 1.0f;
    }

    double sampleRate { 48000.0 };
    float hpHz { 20.0f }, hfHz { 8000.0f };
    bool  hfOn { false };

    float g { 0.0f }, r { 0.7071f }, hpNorm { 1.0f };
    float a1 { 0.0f }, shelfBoost { 1.0f };
    float h1 { 0.0f }, h2 { 0.0f }, s1 { 0.0f };
};

} // namespace kiln
