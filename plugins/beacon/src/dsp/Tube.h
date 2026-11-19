#pragma once

#include <algorithm>
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
    /** A triode, from first principles rather than a curve fitted to look
        right. In grid volts:

        - plate current follows the 3/2 power law above cutoff, so a small
          signal gets mostly 2nd harmonic, rising with level;
        - the negative half runs into cutoff, rounded, not clipped;
        - the positive half runs into grid conduction: once the grid goes
          positive it draws current and the source can no longer push it
          much further;
        - that grid current charges the coupling capacitor, which shifts the
          bias negative for a while after a loud passage. The stage then has
          a little less gain and a little more 2nd harmonic until the
          capacitor drains again: the sag and bloom of a tube driven hard.
    */
    class Triode
    {
    public:
        static constexpr float cutoff      = 4.0f;    ///< grid volts below which no current flows
        static constexpr float restBias    = 1.5f;    ///< operating point, volts below the cathode
        static constexpr float knee        = 0.15f;   ///< rounding of the cutoff
        static constexpr float gridLimit   = 0.5f;    ///< how far the source can push a conducting grid
        static constexpr float chargeTime  = 0.0005f; ///< coupling capacitor through the grid, seconds
        static constexpr float drainTime   = 0.06f;   ///< and through the grid leak resistor

        static float plate (float vg) noexcept
        {
            const auto v = vg + cutoff;
            const auto s = v > 8.0f * knee ? v : knee * std::log1p (std::exp (v / knee));
            return s * std::sqrt (s);
        }

        /** Slope at rest, so the stage is normalised to a gain of one. */
        static float restSlope() noexcept { return 1.5f * std::sqrt (cutoff - restBias); }

        void prepare (double sampleRate) noexcept
        {
            charge = 1.0f - std::exp (-1.0f / (chargeTime * (float) sampleRate));
            drain  = std::exp (-1.0f / (drainTime * (float) sampleRate));
            reset();
        }

        void reset() noexcept { shift = 0.0f; }

        /** Grid volts in, change of plate current out, normalised to unity
            slope at rest. */
        float process (float u) noexcept
        {
            auto vg = u - restBias - shift;

            if (vg > 0.0f)
            {
                shift += charge * vg;
                vg = gridLimit * vg / (gridLimit + vg);
            }

            shift *= drain;

            return (plate (vg) - restPlate) / restSlope();
        }

        float biasShift() const noexcept { return shift; }

    private:
        inline static const float restPlate = plate (-restBias);

        float charge { 0.01f }, drain { 0.999f };
        float shift { 0.0f };
    };

    /** A triode stage driven from a signal: the grid sees the signal times the
        drive, and the result is scaled back down, so drive sets how hard the
        tube works without changing the level. The DC the stage produces is
        taken out of the distortion alone: the linear part of the signal never
        passes through a filter, so a stage at zero drive is exactly a wire. */
    class Stage
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            triode.prepare (sampleRate);
            dcCoeff = 1.0f - std::exp (-2.0f * 3.14159265f * 5.0f / (float) sampleRate);
            reset();
        }

        void reset() noexcept
        {
            triode.reset();
            dcState = 0.0f;
        }

        float process (float x, float drive) noexcept
        {
            if (drive <= 1.0e-6f)
            {
                triode.process (0.0f);   // let the coupling capacitor drain
                dcState *= 1.0f - dcCoeff;
                return x;
            }

            auto residual = triode.process (x * drive) / drive - x;
            dcState += dcCoeff * (residual - dcState);
            residual -= dcState;

            return x + residual;
        }

    private:
        Triode triode;
        float dcCoeff { 0.001f };
        float dcState { 0.0f };
    };

    /** Harmonic distortion of a stage at a given drive, for a 1 kHz sine of
        the given peak amplitude, in percent: harmonics 2 to 12 against the
        fundamental. The stage is run until the coupling capacitor has
        settled, so the figure includes the bias shift a steady tone causes. */
    inline double thdPercent (double drive, double amplitude)
    {
        constexpr double rate = 192000.0, hz = 1000.0;
        constexpr int perCycle = (int) (rate / hz), settle = 120, measure = 10;

        Stage stage;
        stage.prepare (rate);

        std::array<double, 13> re {}, im {};

        for (int n = 0; n < (settle + measure) * perCycle; ++n)
        {
            const auto phase = 2.0 * 3.14159265358979323846 * (double) n / perCycle;
            const auto y = (double) stage.process ((float) (amplitude * std::sin (phase)), (float) drive);

            if (n >= settle * perCycle)
            {
                for (int h = 1; h < (int) re.size(); ++h)
                {
                    re[(size_t) h] += y * std::cos (phase * h);
                    im[(size_t) h] += y * std::sin (phase * h);
                }
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
        static constexpr int    size     = 96;
        static constexpr double minDrive = 0.02, maxDrive = 60.0;

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
            float highest = 0.0f;

            for (int i = 0; i < size; ++i)
            {
                drives[(size_t) i] = minDrive * std::pow (maxDrive / minDrive, (double) i / (size - 1));

                // Kept rising, so the knob can always be inverted.
                highest = std::max (highest * 1.0001f, (float) thdPercent (drives[(size_t) i], amplitude));
                thd[(size_t) i] = highest;
            }
        }

        std::array<double, size> drives {};
        std::array<float,  size> thd {};
    };
}

} // namespace beacon
