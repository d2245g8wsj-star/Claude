#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace lantern
{

/** The equaliser of one channel: an active high and low pass, then four
    passive inductor bands, low to high.

    Each section is a second order analogue prototype realised as a
    trapezoidal state variable filter (after Simper), so a stepped frequency
    switch glides between positions instead of clicking, and a band at 0 dB
    is the input exactly. */
namespace eq
{
    enum class Shape { bell, lowShelf, highShelf, highPass, lowPass };

    // -----------------------------------------------------------------------
    // The bands. Twelve positions each, overlapping, 22 Hz to 28 kHz in all,
    // as the hardware's are. The ranges are the hardware's; the positions in
    // between are this project's, on the usual E-series values.

    inline constexpr int positions = 12;
    inline constexpr int numBands  = 4;

    inline constexpr std::array<std::array<float, positions>, numBands> frequencies { {
        { 22.0f,  30.0f,  40.0f,  60.0f,  80.0f,  100.0f, 150.0f,  220.0f,  330.0f,   470.0f,   680.0f,   1000.0f  },
        { 90.0f,  130.0f, 180.0f, 270.0f, 390.0f, 560.0f, 820.0f,  1200.0f, 1800.0f,  2700.0f,  3900.0f,  4800.0f  },
        { 260.0f, 380.0f, 560.0f, 820.0f, 1200.0f,1600.0f,2200.0f, 3300.0f, 4700.0f,  6800.0f,  8200.0f,  11000.0f },
        { 600.0f, 1000.0f,1500.0f,2200.0f,3300.0f,4700.0f,6800.0f, 10000.0f,12000.0f, 16000.0f, 20000.0f, 28000.0f },
    } };

    inline constexpr std::array<int, numBands> defaultPosition { 5, 4, 6, 7 };   // 100 Hz, 390 Hz, 2.2 kHz, 10 kHz

    /** The gain knob reads ±15 dB. The RANGE switch's moderate setting makes
        the same travel worth ±5 dB, as a switch on the hardware changes what
        a knob position does. */
    inline constexpr float wideRangeDb     = 15.0f;
    inline constexpr float moderateScale   = 1.0f / 3.0f;

    /** Bell widths. Normal is the broad curve of an inductor and capacitor;
        HI-Q is a tighter one. */
    inline constexpr float bellQ  = 0.7f;
    inline constexpr float highQ  = 2.0f;
    inline constexpr float shelfQ = 0.7071f;

    /** Proportional Q: the bell narrows as it is pushed further, reaching its
        nominal Q at 10 dB. At small gains it is broad, so small moves stay
        gentle; constant Q keeps one width at every gain. */
    inline float proportionalQ (float nominal, float gainDb) noexcept
    {
        const auto amount = std::min (std::abs (gainDb), wideRangeDb);
        return nominal * (0.4f + 0.06f * amount);
    }

    // -----------------------------------------------------------------------

    /** One section's coefficients, in Simper's form: H = m0 + m1 * band +
        m2 * low, with the band and low pass outputs of a filter of tuning g
        and damping k. */
    struct Coefficients
    {
        float g { 0.0f }, k { 1.0f }, m0 { 1.0f }, m1 { 0.0f }, m2 { 0.0f };

        /** Where the core filter is tuned, against the corner it was asked
            for: a shelf is tuned half its gain away from it. */
        float tuningRatio { 1.0f };
    };

    inline Coefficients design (Shape shape, float hz, float db, float q, double sampleRate) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        Coefficients c;
        const auto a = std::pow (10.0f, db / 40.0f);
        const auto sqrtA = std::sqrt (a);

        switch (shape)
        {
            case Shape::bell:      c.k = 1.0f / (q * a); c.m1 = c.k * (a * a - 1.0f); break;
            case Shape::lowShelf:  c.k = 1.0f / q; c.m1 = c.k * (a - 1.0f); c.m2 = a * a - 1.0f; c.tuningRatio = 1.0f / sqrtA; break;
            case Shape::highShelf: c.k = 1.0f / q; c.m0 = a * a; c.m1 = c.k * (1.0f - a) * a; c.m2 = 1.0f - a * a; c.tuningRatio = sqrtA; break;
            case Shape::highPass:  c.k = 1.0f / q; c.m1 = -c.k; c.m2 = -1.0f; break;
            case Shape::lowPass:   c.k = 1.0f / q; c.m0 = 0.0f; c.m2 = 1.0f; break;
        }

        const auto tuned = std::min ((double) hz * c.tuningRatio, 0.49 * sampleRate);
        c.g = (float) std::tan (pi * tuned / sampleRate);
        return c;
    }

    /** The analogue prototype the section stands for, at a frequency. */
    inline std::complex<double> analog (Shape shape, double hz, double db, double q, double atHz) noexcept
    {
        const auto c = design (shape, (float) hz, (float) db, (float) q, 1.0e12);
        const std::complex<double> s (0.0, atHz / (hz * c.tuningRatio));
        const auto d = s * s + (double) c.k * s + 1.0;
        return ((double) c.m0 * s * s + ((double) c.m0 * c.k + c.m1) * s + ((double) c.m0 + c.m2)) / d;
    }

    /** A trapezoidal state variable filter. */
    class Section
    {
    public:
        void reset() noexcept { ic1 = ic2 = 0.0f; }

        void set (const Coefficients& c) noexcept
        {
            coeffs = c;
            a1 = 1.0f / (1.0f + c.g * (c.g + c.k));
            a2 = c.g * a1;
            a3 = c.g * a2;
        }

        float process (float v0) noexcept
        {
            const auto v3 = v0 - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return coeffs.m0 * v0 + coeffs.m1 * v1 + coeffs.m2 * v2;
        }

    private:
        Coefficients coeffs;
        float a1 { 1.0f }, a2 { 0.0f }, a3 { 0.0f };
        float ic1 { 0.0f }, ic2 { 0.0f };
    };

    // -----------------------------------------------------------------------

    /** Everything the equaliser of one channel needs to know. The editor's
        curve is drawn from the same description the audio runs on. */
    struct Settings
    {
        struct Band
        {
            float hz { 1000.0f }, db { 0.0f }, q { bellQ };
            Shape shape { Shape::bell };
            bool in { true };
        };

        float highPassHz { 30.0f }, lowPassHz { 20000.0f };
        bool  highPassIn { false }, lowPassIn { false }, highPassSteep { false }, lowPassSteep { false };
        std::array<Band, numBands> bands {};
    };

    /** Butterworth: one section at 12 dB per octave, two at 24. */
    inline constexpr std::array<float, 2> steepQ { 0.5412f, 1.3066f };
    inline constexpr float gentleQ = 0.7071f;

    /** The response of a channel at a frequency, in dB, from the analogue
        prototypes. */
    inline double responseDb (const Settings& s, double hz) noexcept
    {
        std::complex<double> h (1.0, 0.0);

        if (s.highPassIn)
        {
            if (s.highPassSteep)
                for (auto q : steepQ) h *= analog (Shape::highPass, s.highPassHz, 0.0, q, hz);
            else
                h *= analog (Shape::highPass, s.highPassHz, 0.0, gentleQ, hz);
        }

        if (s.lowPassIn)
        {
            if (s.lowPassSteep)
                for (auto q : steepQ) h *= analog (Shape::lowPass, s.lowPassHz, 0.0, q, hz);
            else
                h *= analog (Shape::lowPass, s.lowPassHz, 0.0, gentleQ, hz);
        }

        for (const auto& b : s.bands)
            if (b.in)
                h *= analog (b.shape, b.hz, b.db, b.q, hz);

        return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
    }

    /** The equaliser of one channel at the oversampled rate. Frequencies
        glide on a log scale and gains in dB over about 20 ms; while anything
        moves the sections are redesigned every few samples. */
    class Channel
    {
    public:
        void prepare (double newSampleRate) noexcept
        {
            sampleRate = newSampleRate;
            glide = 1.0f - std::exp (-1.0f / (0.006f * (float) sampleRate));
            snap();
            reset();
        }

        void reset() noexcept
        {
            for (auto& s : highPass) s.reset();
            for (auto& s : lowPass)  s.reset();
            for (auto& s : bands)    s.reset();
        }

        void setTargets (const Settings& s) noexcept { target = s; toTarget(); }

        /** Jump to the targets, for the first block of a render. */
        void snap() noexcept
        {
            current = Smoothed::from (target);
            redesign();
        }

        float process (float x) noexcept
        {
            if (moving && --countdown <= 0)
            {
                countdown = redesignEvery;
                moving = current.approach (Smoothed::from (target), glide * (float) redesignEvery);
                redesign();
            }

            if (target.highPassIn)
            {
                x = highPass[0].process (x);
                if (target.highPassSteep)
                    x = highPass[1].process (x);
            }

            if (target.lowPassIn)
            {
                x = lowPass[0].process (x);
                if (target.lowPassSteep)
                    x = lowPass[1].process (x);
            }

            for (size_t b = 0; b < bands.size(); ++b)
                if (target.bands[b].in || std::abs (current.db[b]) > 1.0e-4f)
                    x = bands[b].process (x);

            return x;
        }

    private:
        static constexpr int redesignEvery = 4;

        /** What glides: log frequencies, gains (0 for a band switched out, so
            IN fades rather than clicks) and Qs. */
        struct Smoothed
        {
            float logHp { 0.0f }, logLp { 0.0f };
            std::array<float, numBands> logHz {}, db {}, q {};

            static Smoothed from (const Settings& s) noexcept
            {
                Smoothed m;
                m.logHp = std::log (s.highPassHz);
                m.logLp = std::log (s.lowPassHz);
                for (size_t b = 0; b < (size_t) numBands; ++b)
                {
                    m.logHz[b] = std::log (s.bands[b].hz);
                    m.db[b]    = s.bands[b].in ? s.bands[b].db : 0.0f;
                    m.q[b]     = s.bands[b].q;
                }
                return m;
            }

            /** Moves towards `to`; false once there. */
            bool approach (const Smoothed& to, float amount) noexcept
            {
                amount = std::min (1.0f, amount);
                bool still = false;
                const auto step = [amount, &still] (float& v, float t, float tolerance)
                {
                    if (std::abs (t - v) <= tolerance) { v = t; return; }
                    v += amount * (t - v);
                    still = true;
                };

                step (logHp, to.logHp, 1.0e-4f);
                step (logLp, to.logLp, 1.0e-4f);
                for (size_t b = 0; b < (size_t) numBands; ++b)
                {
                    step (logHz[b], to.logHz[b], 1.0e-4f);
                    step (db[b],    to.db[b],    1.0e-4f);
                    step (q[b],     to.q[b],     1.0e-4f);
                }
                return still;
            }
        };

        void toTarget() noexcept
        {
            // A probe with no step only reports whether anything differs.
            auto probe = current;
            if (probe.approach (Smoothed::from (target), 0.0f))
            {
                if (! moving)
                    countdown = 0;
                moving = true;
            }
            else if (! moving)
            {
                // Shapes and slopes change at once.
                redesign();
            }
        }

        void redesign() noexcept
        {
            const auto hp = std::exp (current.logHp), lp = std::exp (current.logLp);
            const auto steepHp = target.highPassSteep, steepLp = target.lowPassSteep;

            highPass[0].set (design (Shape::highPass, hp, 0.0f, steepHp ? steepQ[0] : gentleQ, sampleRate));
            highPass[1].set (design (Shape::highPass, hp, 0.0f, steepQ[1], sampleRate));
            lowPass[0] .set (design (Shape::lowPass,  lp, 0.0f, steepLp ? steepQ[0] : gentleQ, sampleRate));
            lowPass[1] .set (design (Shape::lowPass,  lp, 0.0f, steepQ[1], sampleRate));

            for (size_t b = 0; b < bands.size(); ++b)
                bands[b].set (design (target.bands[b].shape, std::exp (current.logHz[b]), current.db[b], current.q[b], sampleRate));
        }

        double sampleRate { 192000.0 };
        float glide { 0.001f };
        Settings target;
        Smoothed current;
        bool moving { false };
        int countdown { 0 };

        std::array<Section, 2> highPass, lowPass;
        std::array<Section, numBands> bands;
    };
}

} // namespace lantern
