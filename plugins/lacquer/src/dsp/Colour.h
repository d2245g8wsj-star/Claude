#pragma once

#include <algorithm>
#include <cmath>

namespace lacquer
{

/** The three analogue stages around the filter network.

    Level reference: 1.0 (0 dBFS) is +18 dBu, the usual mastering alignment.
    The hardware's output stage swings to about +24 dBu, so the amplifier's
    curve is centred 6 dB above full scale and a hot mix reaches it only when
    the bands are boosting.

    Each stage returns the input untouched at low level: every curve has unit
    slope at zero, and the DC an asymmetric curve produces is removed from its
    own residue, never from the programme.
*/
namespace colour
{
    inline float onePoleCoefficient (double cutoffHz, double sampleRate) noexcept
    {
        return 1.0f - (float) std::exp (-2.0 * 3.14159265358979 * cutoffHz / sampleRate);
    }

    /** The balanced input buffer, a discrete single ended amplifier. On, it
        adds a faint, mostly second harmonic. Off, the passive network is fed
        straight from the source, which loses a little at the very top. */
    class InputStage
    {
    public:
        /** Second order coefficient: about -50 dBc of 2nd harmonic at 0 dBFS. */
        static constexpr float evenAmount = 0.006f;

        void prepare (double sampleRate) noexcept
        {
            dcCoeff   = onePoleCoefficient (4.0, sampleRate);
            loadCoeff = onePoleCoefficient (std::min (90000.0, 0.45 * sampleRate), sampleRate);
            reset();
        }

        void reset() noexcept { dc = 0.0f; load = 0.0f; }

        float processBuffered (float x) noexcept
        {
            auto residue = evenAmount * x * x;
            dc += dcCoeff * (residue - dc);
            return x + residue - dc;
        }

        float processDirect (float x) noexcept
        {
            load += loadCoeff * (x - load);
            return load;
        }

    private:
        float dcCoeff { 0.0f }, loadCoeff { 1.0f };
        float dc { 0.0f }, load { 0.0f };
    };

    /** The make up amplifier after the passive network: discrete, DC coupled,
        with a lot of feedback. Below its rails it is nearly straight, with a
        small, mostly second order bend from its single ended stages; at the
        rails it rounds off quickly. Always in circuit, as on the hardware. */
    class MakeupAmp
    {
    public:
        /** Rail limit: +8 dBFS, about +26 dBu from ±24 V rails. */
        static constexpr float headroom = 2.5f;

        /** About -55 dBc of 2nd and -70 dBc of 3rd at 0 dBFS. Both grow with
            level, the 2nd 6 dB per 6 dB, the 3rd 12. */
        static constexpr float even = 0.0036f;
        static constexpr float odd  = 0.0013f;

        void prepare (double sampleRate) noexcept
        {
            dcCoeff = onePoleCoefficient (4.0, sampleRate);
            reset();
        }

        void reset() noexcept { dc = 0.0f; }

        float process (float x) noexcept
        {
            auto bend = even * x * x;
            dc += dcCoeff * (bend - dc);
            const auto y = x + bend - dc + odd * x * x * x;

            // A soft corner of high order: flat to within a few thousandths
            // of a dB until the last few dB below the rail.
            const auto r  = y / headroom;
            const auto r2 = r * r, r4 = r2 * r2;
            return y / std::pow (1.0f + r4 * r4, 0.125f);
        }

    private:
        float dcCoeff { 0.0f };
        float dc { 0.0f };
    };

    /** The 1:1 repeat coil output transformer.

        Core flux is the integral of the voltage, so the core runs out of room
        at low frequencies first. Below a corner the programme is treated as
        flux and bent by an odd curve; above it the transformer is linear. Its
        bandwidth is the other half of its sound: a low corner in the single
        hertz, and a damped resonance well above the audio band whose skirt
        softens the top octave by a fraction of a dB. */
    class OutputTransformer
    {
    public:
        static constexpr double fluxCornerHz = 55.0;
        static constexpr float  coreDrive    = 0.45f; ///< about -35 dBc of 3rd at 30 Hz, 0 dBFS
        static constexpr double lowCornerHz  = 5.0;
        static constexpr double resonanceHz  = 68000.0;
        static constexpr float  resonanceQ   = 0.85f;

        void prepare (double sampleRate) noexcept
        {
            fluxCoeff = onePoleCoefficient (fluxCornerHz, sampleRate);
            hpCoeff   = onePoleCoefficient (lowCornerHz,  sampleRate);

            const auto hz = std::min (resonanceHz, 0.45 * sampleRate);
            const auto g  = (float) std::tan (3.14159265358979 * hz / sampleRate);
            const auto k  = 1.0f / resonanceQ;
            a1 = 1.0f / (1.0f + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;

            reset();
        }

        void reset() noexcept { flux = 0.0f; hp = 0.0f; ic1 = ic2 = 0.0f; }

        float process (float x) noexcept
        {
            flux += fluxCoeff * (x - flux);
            const auto bent = std::tanh (flux * coreDrive) / coreDrive;
            auto y = x + (bent - flux);

            hp += hpCoeff * (y - hp);
            y -= hp;

            const auto v3 = y - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            return v2;
        }

    private:
        float fluxCoeff { 0.0f }, hpCoeff { 0.0f };
        float a1 { 0.0f }, a2 { 0.0f }, a3 { 0.0f };
        float flux { 0.0f }, hp { 0.0f }, ic1 { 0.0f }, ic2 { 0.0f };
    };
}

} // namespace lacquer
