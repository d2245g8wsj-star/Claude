#pragma once

#include <algorithm>
#include <array>
#include <cmath>

namespace anchor
{

/** The VCA bus compressor: a stepped ratio, a soft corner, a detector that
    either looks ahead of the gain cell (feed-forward, the default for a VCA
    bus compressor) or behind it (feedback), and timing set in dB rather than
    in volts, so a release takes as long to recover 3 dB as it does 12. */
namespace vca
{
    /** The ratio switch: 1.5, 2, 4 and 10 to 1, and INF, a limiter. */
    inline constexpr std::array<float, 5> ratios { 1.5f, 2.0f, 4.0f, 10.0f, 0.0f };

    /** Steepness of a ratio: the share of the level over the threshold that
        is taken off. 1 - 1/R, and 1 for INF. */
    inline float slopeFor (int ratioIndex) noexcept
    {
        const auto r = ratios[(size_t) std::clamp (ratioIndex, 0, (int) ratios.size() - 1)];
        return r <= 0.0f ? 1.0f : 1.0f - 1.0f / r;
    }

    /** Width of the corner, in dB, centred on the threshold. The circuits
        this follows bend over a few dB rather than at a point. */
    inline constexpr float kneeDb = 4.0f;

    /** Most reduction the cell is asked for. */
    inline constexpr float maxReductionDb = 40.0f;

    /** Static curve: reduction in dB for a level `overDb` from the threshold,
        with a quadratic corner of kneeDb. */
    inline float reductionFor (float overDb, float slope) noexcept
    {
        constexpr auto half = 0.5f * kneeDb;

        if (overDb <= -half)
            return 0.0f;

        if (overDb < half)
        {
            const auto t = overDb + half;
            return std::min (maxReductionDb, slope * t * t / (2.0f * kneeDb));
        }

        return std::min (maxReductionDb, slope * overDb);
    }

    /** The same law seen from the output, for the feedback detector: with y
        the output's level over the threshold, the reduction r that makes
        y = x - r with x - threshold = overDb. On the straight part this is
        r = y * slope / (1 - slope); for INF the loop gain is capped, which
        is what a real feedback limiter does, and it lands within a fraction
        of a dB of flat. */
    inline float feedbackReductionFor (float outputOverDb, float slope) noexcept
    {
        constexpr float maxLoopGain = 40.0f;
        const auto gain = slope >= 1.0f ? maxLoopGain : std::min (maxLoopGain, slope / (1.0f - slope));
        constexpr auto half = 0.5f * kneeDb;

        if (outputOverDb <= -half)
            return 0.0f;

        if (outputOverDb < half)
        {
            const auto t = outputOverDb + half;
            return std::min (maxReductionDb, gain * t * t / (2.0f * kneeDb));
        }

        return std::min (maxReductionDb, gain * outputOverDb);
    }

    /** Attack switch, ms. */
    inline constexpr std::array<float, 6> attacks { 0.1f, 0.3f, 1.0f, 3.0f, 10.0f, 30.0f };

    /** Release switch, s; the last position is AUTO. */
    inline constexpr std::array<float, 6> releases { 0.1f, 0.3f, 0.6f, 0.9f, 1.2f, 0.0f };
    inline constexpr int autoRelease = 5;

    /** The two halves of AUTO: a fast recovery for what a transient took,
        and a slow one for what sustained material took. A short burst
        charges only the fast part; a long passage charges the slow one too
        and lets go slowly. */
    inline constexpr float autoFastSeconds   = 0.08f;
    inline constexpr float autoSlowSeconds   = 1.6f;
    inline constexpr float autoChargeSeconds = 0.35f;

    /** Timing of the gain reduction, in dB. Attack and release are the time
        constants of a one pole move towards the static curve's target. */
    class Ballistics
    {
    public:
        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            setTimes (attackSeconds, releaseSeconds, automatic);
            reset();
        }

        void reset() noexcept { fast = slow = 0.0f; }

        void setTimes (float attack, float release, bool isAuto) noexcept
        {
            attackSeconds  = attack;
            releaseSeconds = release;
            automatic      = isAuto;

            const auto coeff = [this] (float seconds)
            {
                return 1.0f - std::exp (-1.0f / (std::max (1.0e-5f, seconds) * (float) sampleRate));
            };

            attackCoeff     = coeff (attack);
            releaseCoeff    = coeff (isAuto ? autoFastSeconds : release);
            slowCoeff       = coeff (autoSlowSeconds);
            slowChargeCoeff = coeff (autoChargeSeconds);
        }

        /** Caps the attack coefficient, for the feedback loop, which would
            ring if it moved most of the way in one sample. */
        void limitAttack (float maxCoeff) noexcept { attackCoeff = std::min (attackCoeff, maxCoeff); }

        float process (float target) noexcept
        {
            if (! automatic)
            {
                fast += (target > fast ? attackCoeff : releaseCoeff) * (target - fast);
                return fast;
            }

            // The fast part follows the material at the set attack and lets go
            // quickly; the slow part charges only from what is held for a
            // while, and lets go slowly. The cell takes whichever asks more.
            fast += (target > fast ? attackCoeff : releaseCoeff) * (target - fast);
            slow += (target > slow ? slowChargeCoeff : slowCoeff) * (target - slow);

            return std::max (fast, slow);
        }

    private:
        double sampleRate { 48000.0 };
        float attackSeconds { 0.01f }, releaseSeconds { 0.3f };
        bool automatic { false };
        float attackCoeff { 0.01f }, releaseCoeff { 0.001f }, slowCoeff { 0.0001f }, slowChargeCoeff { 0.0001f };
        float fast { 0.0f }, slow { 0.0f };
    };

    /** The gain cell. A VCA is close to a wire, but not quite: a trace of
        second harmonic that grows as it works, from the mismatch of its
        transistors. At rest it is exact. */
    class Cell
    {
    public:
        /** Asymmetry per dB of reduction. 10 dB of reduction gives about
            0.02 % of second harmonic at the operating level. */
        static constexpr float evenPerDb = 0.0012f;

        void prepare (double sampleRate) noexcept
        {
            dcCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * 5.0f / (float) sampleRate);
            reset();
        }

        void reset() noexcept { dcState = 0.0f; }

        float process (float x, float reductionDb) noexcept
        {
            const auto v = x * std::pow (10.0f, -reductionDb / 20.0f);
            const auto h = evenPerDb * reductionDb;

            if (h <= 1.0e-7f)
            {
                dcState *= 1.0f - dcCoeff;
                return v;
            }

            auto residual = h * v * v;
            dcState += dcCoeff * (residual - dcState);
            return v + residual - dcState;
        }

    private:
        float dcCoeff { 0.001f };
        float dcState { 0.0f };
    };
}

/** What the detector listens to. OFF, a 6 dB per octave high pass at 60, 90
    or 150 Hz so the low end does not pump the mix, or one of two tilts (T1,
    T2) that also make the detector listen harder to the top, so a bright
    mix is held down where a dull one is let through.

    The ranges and the slope of the high passes are the hardware's; what the
    two tilts are exactly is not published, so these are this project's:

        T1  high pass 90 Hz  + first order high shelf +3 dB at 3 kHz
        T2  high pass 150 Hz + first order high shelf +6 dB at 5 kHz */
class SidechainFilter
{
public:
    enum Position { off = 0, hp60, hp90, hp150, tilt1, tilt2, count };

    struct Shape
    {
        float highPassHz;     ///< 0 for none
        float shelfHz;
        float shelfDb;        ///< 0 for none
    };

    static constexpr std::array<Shape, count> shapes { {
        { 0.0f,   1000.0f, 0.0f },
        { 60.0f,  1000.0f, 0.0f },
        { 90.0f,  1000.0f, 0.0f },
        { 150.0f, 1000.0f, 0.0f },
        { 90.0f,  3000.0f, 3.0f },
        { 150.0f, 5000.0f, 6.0f },
    } };

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        position = -1;
        setPosition (off);
        reset();
    }

    void reset() noexcept { hpState = shelfState = 0.0f; }

    void setPosition (int newPosition) noexcept
    {
        newPosition = std::clamp (newPosition, 0, (int) count - 1);

        if (newPosition == position)
            return;

        position = newPosition;
        const auto& s = shapes[(size_t) position];
        constexpr double pi = 3.14159265358979323846;
        const auto safe = [this] (float hz) { return std::min ((double) hz, 0.45 * sampleRate); };

        highPass = s.highPassHz > 0.0f;
        const auto g = std::tan (pi * safe (std::max (1.0f, s.highPassHz)) / sampleRate);
        hpCoeff = (float) (g / (1.0 + g));

        const auto gs = std::tan (pi * safe (s.shelfHz) / sampleRate);
        shelfCoeff = (float) (gs / (1.0 + gs));
        shelfGain  = std::pow (10.0f, s.shelfDb / 20.0f) - 1.0f;
    }

    float process (float x) noexcept
    {
        // First order high pass: the input less its low passed copy.
        const auto w  = (x - hpState) * hpCoeff;
        const auto lp = w + hpState;
        hpState = lp + w;
        const auto hp = highPass ? x - lp : x;

        // First order high shelf: add a share of the high passed part.
        const auto ws  = (hp - shelfState) * shelfCoeff;
        const auto lps = ws + shelfState;
        shelfState = lps + ws;

        return hp + shelfGain * (hp - lps);
    }

    /** The filter's response at a frequency, for the checks. */
    static double magnitudeAt (int positionIndex, double hz) noexcept
    {
        const auto& s = shapes[(size_t) std::clamp (positionIndex, 0, (int) count - 1)];
        double m = 1.0;

        if (s.highPassHz > 0.0f)
        {
            const auto r = hz / s.highPassHz;
            m *= r / std::sqrt (1.0 + r * r);
        }

        if (s.shelfDb > 1.0e-6f)
        {
            // 1 + G * s / (s + w): real and imaginary parts at s = jw.
            const auto g = std::pow (10.0, s.shelfDb / 20.0) - 1.0;
            const auto r = hz / s.shelfHz;
            const auto re = 1.0 + g * r * r / (1.0 + r * r);
            const auto im = g * r / (1.0 + r * r);
            m *= std::hypot (re, im);
        }

        return m;
    }

private:
    double sampleRate { 48000.0 };
    int position { -1 };
    bool highPass { false };
    float hpCoeff { 0.0f }, shelfCoeff { 0.0f }, shelfGain { 0.0f };
    float hpState { 0.0f }, shelfState { 0.0f };
};

} // namespace anchor
