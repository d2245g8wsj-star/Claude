#pragma once

#include "Saturation.h"

#include <algorithm>
#include <cmath>

namespace swell
{

/** One band of the harmonic equaliser.

    A conventional shelf or peak adds a filtered copy of the signal to itself:

        out = x + k * band(x)          k = gain - 1

    Here the copy is saturated before it is added:

        out = x + k * sat(band(x) * drive) / drive

    so the band is a linear filter while it is quiet, and the boost or cut turns
    progressively into harmonics of the frequencies it covers as it is driven.
    The drive follows the size of the boost or cut rather than its sign, so
    6 dB of cut saturates exactly as much as 6 dB of boost - a cut subtracts a
    saturated copy, which also leaves harmonics behind.

    The band returns only its contribution; the channel sums the three onto the
    dry signal, which is what makes neighbouring bands interact.
*/
class HarmonicBand
{
public:
    enum class Shape { lowShelf, peak, highShelf };

    /** Boost and cut range of the level control, either way. */
    static constexpr float maxGainDb = 10.0f;

    /** The peak band's fixed bandwidth. */
    static constexpr float peakQ = 0.9f;

    /** How hard the band's signal is pushed into its curve, at full boost or
        cut, for a band component at 0 dBFS. At 0 dB of band gain the band is
        silent whatever this is; at small gains it is driven more gently. */
    static constexpr float driveAtFullGain (Shape shape) noexcept
    {
        switch (shape)
        {
            case Shape::lowShelf:  return 2.0f;
            case Shape::peak:      return 2.0f;
            case Shape::highShelf: return 2.5f;
        }

        return 2.0f;
    }

    /** Fraction of the full drive left at the smallest gains. */
    static constexpr float driveFloor = 0.35f;

    explicit HarmonicBand (Shape s) noexcept
        : shape (s),
          curve (s == Shape::lowShelf ? Curve::hard
               : s == Shape::peak     ? Curve::tube
                                      : Curve::smooth)
    {
    }

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;

        // Parameter moves glide over about 20 ms; the drive meter falls over
        // about 300 ms so a short burst of saturation is still visible.
        smoothing   = 1.0f - std::exp (-1.0f / (0.020f * (float) sampleRate));
        meterFall   = std::exp (-1.0f / (0.300f * (float) sampleRate));
        dcCoeff     = 1.0f - std::exp (-2.0f * 3.14159265f * 8.0f / (float) sampleRate);

        snapToTargets();
        reset();
    }

    void reset() noexcept
    {
        s1 = s2 = 0.0f;
        dcState = 0.0f;
        driveEnvelope = 0.0f;
    }

    void setTargets (float frequencyHz, float newGainDb, bool shouldBeEnabled) noexcept
    {
        targetLogFrequency = std::log (std::clamp (frequencyHz, 10.0f, 0.45f * (float) sampleRate));
        targetGainDb       = std::clamp (newGainDb, -maxGainDb, maxGainDb);
        targetEnabled      = shouldBeEnabled ? 1.0f : 0.0f;
    }

    /** Jumps straight to the targets, for prepare time and offline renders. */
    void snapToTargets() noexcept
    {
        logFrequency = targetLogFrequency;
        gainDb       = targetGainDb;
        enabled      = targetEnabled;
        updateFilter();
        updateGain();
    }

    /** Returns what this band adds to the dry signal. */
    float process (float x) noexcept
    {
        advanceSmoothing();

        const auto band = filter (x);

        if (enabled <= 0.0f || std::abs (k) < 1.0e-6f)
        {
            driveEnvelope *= meterFall;
            return 0.0f;
        }

        const auto u = band * drive;
        auto shaped  = saturation::apply (curve, u) / drive;

        // The tube curve is asymmetric, so it shifts the band's mean. Take it
        // out here rather than leaving it to a blocker on the whole channel,
        // which would put a phase shift on the dry path too.
        if (curve == Curve::tube)
        {
            dcState += dcCoeff * (shaped - dcState);
            shaped  -= dcState;
        }

        const auto magnitude = std::abs (u);
        driveEnvelope = magnitude > driveEnvelope ? magnitude : driveEnvelope * meterFall;

        return k * enabled * shaped;
    }

    /** Peak of the signal entering the curve, in units where 1.0 is where the
        curve has bent noticeably. Zero while the band is flat or switched out. */
    float getDrive() const noexcept { return driveEnvelope; }

    Shape getShape() const noexcept { return shape; }

    // ---------------------------------------------------------------------
    // Linear response, for the display and the checks. Evaluated from the
    // same coefficients the audio path uses.

    /** Magnitude of 1 + k * H(f) with the curve treated as linear. */
    static double linearMagnitude (Shape shape, double frequencyHz, double cornerHz, double gainDb)
    {
        const auto k  = std::pow (10.0, gainDb / 20.0) - 1.0;
        const auto w  = frequencyHz / cornerHz;     // analogue prototype, s = jw

        double re = 0.0, im = 0.0;

        switch (shape)
        {
            case Shape::lowShelf:  { const auto d = 1.0 + w * w; re = 1.0 / d;   im = -w / d; break; }
            case Shape::highShelf: { const auto d = 1.0 + w * w; re = w * w / d; im =  w / d; break; }
            case Shape::peak:
            {
                // (s/Q) / (s^2 + s/Q + 1), normalised to 1 at the centre.
                const auto a = 1.0 - w * w;
                const auto b = w / peakQ;
                const auto d = a * a + b * b;
                re = b * b / d;
                im = b * a / d;
                break;
            }
        }

        return std::hypot (1.0 + k * re, k * im);
    }

private:
    void advanceSmoothing() noexcept
    {
        if (std::abs (targetLogFrequency - logFrequency) > 0.0f)
        {
            logFrequency += smoothing * (targetLogFrequency - logFrequency);

            if (std::abs (targetLogFrequency - logFrequency) < 1.0e-5f)
                logFrequency = targetLogFrequency;

            updateFilter();
        }

        if (std::abs (targetGainDb - gainDb) > 0.0f)
        {
            gainDb += smoothing * (targetGainDb - gainDb);

            if (std::abs (targetGainDb - gainDb) < 1.0e-4f)
                gainDb = targetGainDb;

            updateGain();
        }

        if (std::abs (targetEnabled - enabled) > 0.0f)
        {
            enabled += smoothing * (targetEnabled - enabled);

            if (std::abs (targetEnabled - enabled) < 1.0e-4f)
                enabled = targetEnabled;
        }
    }

    void updateFilter() noexcept
    {
        const auto hz = std::exp (logFrequency);
        g = std::tan (3.14159265358979 * hz / sampleRate);

        if (shape == Shape::peak)
        {
            const auto damping = 1.0f / peakQ;
            a1 = 1.0f / (1.0f + g * (g + damping));
            a2 = g * a1;
            a3 = g * a2;
        }
        else
        {
            a1 = g / (1.0f + g);
        }
    }

    void updateGain() noexcept
    {
        k = std::pow (10.0f, gainDb / 20.0f) - 1.0f;
        drive = driveAtFullGain (shape)
              * (driveFloor + (1.0f - driveFloor) * std::abs (gainDb) / maxGainDb);
    }

    /** Topology preserving one pole for the shelves, state variable for the
        peak, so a frequency sweep is free of zipper noise and blow ups. */
    float filter (float x) noexcept
    {
        if (shape == Shape::peak)
        {
            const auto v3 = x - s2;
            const auto v1 = a1 * s1 + a2 * v3;
            const auto v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0f * v1 - s1;
            s2 = 2.0f * v2 - s2;

            return v1 / peakQ;    // band pass, unity at the centre
        }

        const auto v  = (x - s1) * a1;
        const auto lp = v + s1;
        s1 = lp + v;

        return shape == Shape::lowShelf ? lp : x - lp;
    }

    const Shape shape;
    const Curve curve;

    double sampleRate { 48000.0 };

    float targetLogFrequency { std::log (1000.0f) }, logFrequency { std::log (1000.0f) };
    float targetGainDb { 0.0f }, gainDb { 0.0f };
    float targetEnabled { 1.0f }, enabled { 1.0f };

    float smoothing { 0.001f }, meterFall { 0.999f }, dcCoeff { 0.001f };

    float g { 0.0f }, a1 { 0.0f }, a2 { 0.0f }, a3 { 0.0f };
    float s1 { 0.0f }, s2 { 0.0f };

    float k { 0.0f }, drive { 1.0f };
    float dcState { 0.0f };
    float driveEnvelope { 0.0f };
};

} // namespace swell
