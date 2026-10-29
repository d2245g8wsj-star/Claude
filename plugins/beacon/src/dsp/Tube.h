#pragma once

#include <array>
#include <cmath>

namespace beacon
{

/** Level reference used throughout: 0 dBu sits at -18 dBFS, so the hardware's
    +4 dBu operating level is -14 dBFS and its +26 dBu ceiling is +8 dBFS. */
namespace level
{
    inline constexpr float dbfsAtZeroDbu = -18.0f;
    inline constexpr float nominalDbfs   = -14.0f;     ///< +4 dBu

    inline float nominalAmplitude() noexcept { return std::pow (10.0f, nominalDbfs / 20.0f); }
}

namespace tube
{
    /** Grid bias of the triode curve. A biased tanh: the positive half reaches
        the flat part sooner than the negative one, which is where the 2nd
        harmonic comes from, and the odd series joins in as it is driven. */
    inline constexpr float triodeBias = 0.35f;

    /** Slope 1 at zero, so a gently driven stage is a wire. */
    inline float triode (float u) noexcept
    {
        static const float offset = std::tanh (triodeBias);
        static const float slope  = 1.0f - offset * offset;

        return (std::tanh (u + triodeBias) - offset) / slope;
    }

    /** Harmonic distortion of the triode curve at a given drive, for a sine of
        the given peak amplitude, in percent. Evaluated over one exact cycle,
        harmonics 2 to 12, with the DC the asymmetry produces left out. */
    inline double thdPercent (double drive, double amplitude)
    {
        constexpr int points = 512;
        std::array<double, 13> re {}, im {};

        for (int i = 0; i < points; ++i)
        {
            const auto phase = 2.0 * 3.14159265358979323846 * i / points;
            const auto x = amplitude * std::sin (phase);
            const auto y = (double) triode ((float) (x * drive)) / drive;

            for (int h = 1; h < (int) re.size(); ++h)
            {
                re[(size_t) h] += y * std::cos (phase * h);
                im[(size_t) h] += y * std::sin (phase * h);
            }
        }

        double fundamental = std::hypot (re[1], im[1]), sum = 0.0;

        for (size_t h = 2; h < re.size(); ++h)
            sum += re[h] * re[h] + im[h] * im[h];

        return 100.0 * std::sqrt (sum) / fundamental;
    }

    /** The THD control is calibrated rather than labelled: the knob reads the
        distortion the stage produces on a 1 kHz sine at the +4 dBu operating
        level. Louder material distorts more, quieter less, as on the unit. */
    class ThdCalibration
    {
    public:
        static constexpr int    size     = 192;
        static constexpr double minDrive = 0.005, maxDrive = 40.0;

        static const ThdCalibration& get()
        {
            static const ThdCalibration table;
            return table;
        }

        /** Drive that gives this much THD at the operating level. 0 % is 0. */
        float driveFor (float percent) const noexcept
        {
            if (percent <= 0.0f)
                return 0.0f;

            if (percent <= thd.front())
                return (float) drives.front() * percent / (float) thd.front();

            for (size_t i = 1; i < (size_t) size; ++i)
            {
                if (percent <= thd[i])
                {
                    const auto t = (percent - thd[i - 1]) / (thd[i] - thd[i - 1]);
                    return (float) std::exp (std::log (drives[i - 1]) + t * (std::log (drives[i]) - std::log (drives[i - 1])));
                }
            }

            return (float) drives.back();
        }

    private:
        ThdCalibration()
        {
            const auto amplitude = (double) level::nominalAmplitude();

            for (int i = 0; i < size; ++i)
            {
                drives[(size_t) i] = minDrive * std::pow (maxDrive / minDrive, (double) i / (size - 1));
                thd   [(size_t) i] = (float) thdPercent (drives[(size_t) i], amplitude);
            }
        }

        std::array<double, size> drives {};
        std::array<float,  size> thd {};
    };

    /** A curve applied to a signal, with the DC its asymmetry produces taken
        out of the distortion alone. The linear part of the signal never passes
        through a filter, so a stage at zero drive is exactly a wire. */
    class Stage
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            dcCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * 5.0f / (float) sampleRate);
            reset();
        }

        void reset() noexcept { dcState = 0.0f; }

        float process (float x, float drive) noexcept
        {
            if (drive <= 1.0e-6f)
            {
                dcState *= 1.0f - dcCoeff;
                return x;
            }

            auto residual = triode (x * drive) / drive - x;
            dcState += dcCoeff * (residual - dcState);
            residual -= dcState;

            return x + residual;
        }

    private:
        float dcCoeff { 0.001f };
        float dcState { 0.0f };
    };
}

} // namespace beacon
