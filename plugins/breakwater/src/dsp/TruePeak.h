#pragma once

#include <array>
#include <algorithm>
#include <cmath>

namespace breakwater
{

/** True peak estimation for the final limiter stage and the output meter,
    after the method in ITU-R BS.1770 annex 2, but tighter than the annex.

    The annex's 48 tap, four times interpolator has two blind spots, and a
    hard worked limiter output falls into both. Its filter starts rolling off
    well below Nyquist, so energy at 15 to 22 kHz is under-weighted, and the
    four times grid can miss the top of a peak by up to 0.6 dB. Here:

      * Eight phases, each 49 taps long: a 385 tap Kaiser windowed sinc whose
        passband reaches about 0.45 of the sample rate, 20 kHz at 44.1 kHz.

      * Every local maximum on the eight times grid is refined with a
        parabola through it and its two neighbours, which brings the residual
        grid error down to a few thousandths of a dB.

    The prototype's delay is exactly 24 samples at the original rate, so phase
    0 returns the input 24 samples late. The refinement needs the point after
    the interval too, so each result describes the interval starting one
    sample before that: from the input `delay` samples back to the one after.
*/
class TruePeakInterpolator
{
public:
    static constexpr int factor       = 8;
    static constexpr int tapsPerPhase = 49;
    static constexpr int delay        = (tapsPerPhase - 1) / 2 + 1;   ///< base rate samples

    TruePeakInterpolator() noexcept
    {
        constexpr int length = factor * (tapsPerPhase - 1) + 1;
        constexpr double centre = (length - 1) / 2.0;
        constexpr double beta = 8.0;

        std::array<double, length> h { };

        for (int n = 0; n < length; ++n)
        {
            const auto t = (n - centre) / factor;
            const auto sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (3.14159265358979323846 * t) / (3.14159265358979323846 * t);
            const auto r = (n - centre) / centre;
            h[(size_t) n] = sinc * besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
        }

        for (int p = 0; p < factor; ++p)
        {
            double sum = 0.0;

            for (int j = 0; j < tapsPerPhase && factor * j + p < length; ++j)
                sum += h[(size_t) (factor * j + p)];

            // Stored by tap, oldest input first, with the eight phases side
            // by side: the loop below then updates all eight outputs at once
            // from one input sample, which is what vectorises.
            for (int j = 0; j < tapsPerPhase; ++j)
            {
                const auto index = factor * j + p;
                coefficients[(size_t) (tapsPerPhase - 1 - j)][(size_t) p]
                    = index < length ? (float) (h[(size_t) index] / sum) : 0.0f;
            }
        }

        reset();
    }

    void reset() noexcept
    {
        history.fill (0.0f);
        position = 0;
        current.fill (0.0f);
        previous.fill (0.0f);
        beforePrevious = 0.0f;
    }

    /** Pushes one sample and returns the true peak magnitude of the interval
        from the input `delay` samples back to the one after it. */
    float push (float x) noexcept
    {
        history[(size_t) position] = x;
        history[(size_t) (position + tapsPerPhase)] = x;   // mirrored, so reads never wrap

        beforePrevious = previous[factor - 1];
        previous = current;

        // history[position + 1 + k] is x[n - tapsPerPhase + 1 + k].
        const auto* oldest = history.data() + position + 1;

        std::array<float, factor> y { };

        for (int k = 0; k < tapsPerPhase; ++k)
        {
            const auto input = oldest[k];
            const auto& c = coefficients[(size_t) k];

            for (int p = 0; p < factor; ++p)
                y[(size_t) p] += c[(size_t) p] * input;
        }

        current = y;

        position = position + 1 == tapsPerPhase ? 0 : position + 1;

        // The interval being reported is `previous`; its neighbours are the
        // last point before it and the first point after it.
        float peak = 0.0f;

        for (int p = 0; p < factor; ++p)
        {
            const auto b = previous[(size_t) p];
            const auto a = p == 0 ? beforePrevious : previous[(size_t) p - 1];
            const auto c = p == factor - 1 ? current[0] : previous[(size_t) p + 1];

            peak = std::max (peak, refine (a, b, c));
        }

        return peak;
    }

private:
    /** The magnitude at a grid point, or the top of the parabola through it
        and its neighbours when it is a local maximum of the magnitude. */
    static float refine (float a, float b, float c) noexcept
    {
        const auto sign = b < 0.0f ? -1.0f : 1.0f;
        a *= sign; b *= sign; c *= sign;

        if (b < a || b < c)
            return b;

        const auto curvature = a - 2.0f * b + c;

        if (curvature >= -1.0e-12f)
            return b;

        const auto offset = 0.5f * (a - c) / curvature;
        return b - 0.25f * (a - c) * offset;
    }

    static double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;

        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }

        return sum;
    }

    std::array<std::array<float, factor>, tapsPerPhase> coefficients { };
    std::array<float, 2 * tapsPerPhase> history { };
    int position { 0 };

    std::array<float, factor> current { }, previous { };
    float beforePrevious { 0.0f };
};

} // namespace breakwater
