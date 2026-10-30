#pragma once

#include "Network.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>

namespace hinge
{

/** Exactly zero, without tripping -Wfloat-equal: the network's terms vanish
    exactly at the ends of the attenuator, and that is what is being asked. */
inline bool isZero (double x) noexcept { return ! (x < 0.0 || x > 0.0); }

/** b0 + b1 z^-1 + b2 z^-2 over 1 + a1 z^-1 + a2 z^-2. */
struct Coefficients
{
    double b0 { 1.0 }, b1 { 0.0 }, b2 { 0.0 }, a1 { 0.0 }, a2 { 0.0 };

    /** The reciprocal filter. Only stable when this one is minimum phase,
        which isInvertible() checks. */
    Coefficients inverted() const noexcept
    {
        return { 1.0 / b0, a1 / b0, a2 / b0, b1 / b0, b2 / b0 };
    }

    /** Both zeros strictly inside the unit circle (Jury). */
    bool isInvertible() const noexcept
    {
        if (isZero (b0))
            return false;

        const auto c1 = b1 / b0, c2 = b2 / b0;
        return std::abs (c2) < 1.0 && std::abs (c1) < 1.0 + c2;
    }

    std::complex<double> response (double omega) const noexcept
    {
        const auto z1 = std::polar (1.0, -omega);
        const auto z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }
};

/** Direct form I, in double. Its state is only past inputs and outputs, so
    when the coefficients move under signal the output stays continuous;
    the transposed forms carry state scaled by the old coefficients, which
    clicks when a low frequency section is swept. */
struct Biquad
{
    Coefficients c;
    double x1 { 0.0 }, x2 { 0.0 }, y1 { 0.0 }, y2 { 0.0 };

    double process (double x) noexcept
    {
        const auto y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1;  x1 = x;
        y2 = y1;  y1 = y;
        return y;
    }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
};

/**
    Turns an analogue biquad into a digital one without the bilinear
    transform's cramping, so a shelf named at 18 kHz is still an 18 kHz shelf
    at 44.1 kHz and there is nothing to oversample.

    The poles are the analogue poles mapped exactly, z = exp (sT). The zeros
    are then chosen so the magnitude follows the analogue curve: |B|^2 is a
    cosine polynomial P + Q cos w + R cos 2w, linear in its three terms, so it
    is fitted by least squares on relative error across the audio band, with
    the DC value pinned exactly, and factored back into a minimum phase
    numerator.

    Against the analogue network that holds every curve this plugin can make
    to within a quarter of a dB at 44.1 kHz, and to a hundredth at 96 kHz;
    the offline checks measure it.
*/
class MatchedDesign
{
public:
    static constexpr int gridSize = 32;

    void prepare (double rate)
    {
        sampleRate = rate;

        const auto top = std::min (20000.0, 0.45 * rate);
        const auto bottom = 20.0;

        for (int k = 0; k < gridSize; ++k)
        {
            const auto hz = bottom * std::pow (top / bottom, (double) k / (gridSize - 1));
            const auto w  = 2.0 * pi * hz / rate;

            grid[(size_t) k] = { 2.0 * pi * hz, std::cos (w), std::cos (2.0 * w) };
        }
    }

    double getSampleRate() const noexcept { return sampleRate; }

    Coefficients operator() (const network::Analog& h) const noexcept
    {
        Coefficients out;
        mapPoles (h.a, out.a1, out.a2);
        fitZeros (h, out);
        return out;
    }

    /** The analogue response, for comparison. */
    static std::complex<double> analogResponse (const network::Analog& h, double radiansPerSecond) noexcept
    {
        const std::complex<double> s (0.0, radiansPerSecond);
        return (h.b[0] + s * (h.b[1] + s * h.b[2])) / (h.a[0] + s * (h.a[1] + s * h.a[2]));
    }

private:
    static constexpr double pi = 3.14159265358979323846;

    struct GridPoint
    {
        double omega;      // analogue, rad/s
        double cos1, cos2; // of the digital frequency, and of twice it
    };

    void mapPoles (const std::array<double, 3>& a, double& a1, double& a2) const noexcept
    {
        const auto t = 1.0 / sampleRate;
        a1 = a2 = 0.0;

        if (isZero (a[2]))
        {
            if (! isZero (a[1]))
                a1 = -std::exp (-a[0] / a[1] * t);

            return;
        }

        const auto disc = a[1] * a[1] - 4.0 * a[2] * a[0];

        if (disc >= 0.0)
        {
            // Stable form: the far root as q / a2, the near one as a0 / q, so
            // a nearly first order network does not lose its near pole.
            const auto q  = -0.5 * (a[1] + std::copysign (std::sqrt (disc), a[1]));
            const auto r1 = q / a[2];
            const auto r2 = isZero (q) ? r1 : a[0] / q;

            a1 = -(std::exp (r1 * t) + std::exp (r2 * t));
            a2 = std::exp ((r1 + r2) * t);
        }
        else
        {
            const auto sigma = -a[1] / (2.0 * a[2]);
            const auto omega = std::sqrt (-disc) / (2.0 * a[2]);

            a1 = -2.0 * std::exp (sigma * t) * std::cos (omega * t);
            a2 = std::exp (2.0 * sigma * t);
        }
    }

    static double analogPower (const network::Analog& h, double w) noexcept
    {
        const auto w2   = w * w;
        const auto nRe  = h.b[0] - h.b[2] * w2, nIm = h.b[1] * w;
        const auto dRe  = h.a[0] - h.a[2] * w2, dIm = h.a[1] * w;
        return (nRe * nRe + nIm * nIm) / (dRe * dRe + dIm * dIm);
    }

    static double denominatorPower (double a1, double a2, double cos1, double cos2) noexcept
    {
        return 1.0 + a1 * a1 + a2 * a2 + 2.0 * a1 * (1.0 + a2) * cos1 + 2.0 * a2 * cos2;
    }

    void fitZeros (const network::Analog& h, Coefficients& out) const noexcept
    {
        // Target |B|^2 on the grid and at DC.
        const auto dc  = isZero (h.a[0]) ? 0.0 : h.b[0] / h.a[0];
        const auto sum = 1.0 + out.a1 + out.a2;
        const auto t0  = dc * dc * sum * sum;

        // P = t0 - Q - R pins DC; minimise sum ((t0 + Q (c1 - 1) + R (c2 - 1)) / T - 1)^2.
        double m11 = 0.0, m12 = 0.0, m22 = 0.0, v1 = 0.0, v2 = 0.0;

        for (const auto& g : grid)
        {
            const auto target = std::max (analogPower (h, g.omega) * denominatorPower (out.a1, out.a2, g.cos1, g.cos2),
                                          1.0e-300);
            const auto x1 = (g.cos1 - 1.0) / target;
            const auto x2 = (g.cos2 - 1.0) / target;
            const auto y  = 1.0 - t0 / target;

            m11 += x1 * x1;  m12 += x1 * x2;  m22 += x2 * x2;
            v1  += x1 * y;   v2  += x2 * y;
        }

        const auto det = m11 * m22 - m12 * m12;
        double q = 0.0, r = 0.0;

        if (std::abs (det) > 1.0e-300 * (m11 * m22 + 1.0e-300))
        {
            q = (v1 * m22 - v2 * m12) / det;
            r = (m11 * v2 - m12 * v1) / det;
        }
        else if (m11 > 0.0)
        {
            q = v1 / m11;
        }

        const auto p = t0 - q - r;

        // Back to coefficients: |B(1)| and |B(-1)| give b0 + b2 and b1, and
        // R = 2 b0 b2 splits the first; the larger root goes to b0, which
        // keeps the zeros inside the circle.
        const auto atDc      = std::sqrt (std::max (p + q + r, 0.0));
        const auto atNyquist = std::sqrt (std::max (p - q + r, 0.0));

        const auto u  = 0.5 * (atDc + atNyquist);
        const auto b1 = 0.5 * (atDc - atNyquist);
        const auto d  = std::sqrt (std::max (u * u - 2.0 * r, 0.0));

        out.b0 = 0.5 * (u + d);
        out.b1 = b1;
        out.b2 = u - out.b0;

        if (dc < 0.0)
        {
            out.b0 = -out.b0;
            out.b1 = -out.b1;
            out.b2 = -out.b2;
        }
    }

    double sampleRate { 48000.0 };
    std::array<GridPoint, gridSize> grid {};
};

} // namespace hinge
