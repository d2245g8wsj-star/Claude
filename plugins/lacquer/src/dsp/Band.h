#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace lacquer
{

/** One filter of the equaliser network.

    Every band is a second order analogue prototype - the shape a passive LC or
    RC section, or the active mid cut, draws on a sweep - mapped to the
    oversampled rate with the bilinear transform, prewarped at its own
    frequency. The structure is Simper's trapezoidal state variable filter,
    which stays stable and quiet while its frequency, gain and Q move, so a
    stepped switch can glide between positions instead of clicking.

    Frequency, gain and Q glide over about 20 ms. A band at 0 dB is the input,
    sample for sample, whatever its frequency and Q.
*/
class Band
{
public:
    enum class Type { lowShelf, highShelf, bell };

    explicit Band (Type t) noexcept : type (t) {}

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        smoothing  = 1.0f - std::exp (-1.0f / (0.020f * (float) sampleRate));
        snapToTargets();
        reset();
    }

    void reset() noexcept { ic1 = ic2 = 0.0f; }

    void setTargets (float frequencyHz, float newGainDb, float newQ) noexcept
    {
        targetLogFrequency = std::log (std::clamp (frequencyHz, 10.0f, 0.45f * (float) sampleRate));
        targetGainDb       = newGainDb;
        targetQ            = std::max (0.1f, newQ);
    }

    void snapToTargets() noexcept
    {
        logFrequency = targetLogFrequency;
        gainDb       = targetGainDb;
        q            = targetQ;
        update();
    }

    float process (float x) noexcept
    {
        if (advanceSmoothing())
            update();

        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;

        return m0 * x + m1 * v1 + m2 * v2;
    }

    /** The analogue prototype, for the checks. s is normalised to the band's
        own frequency, as the digital filter is at its prewarped point. */
    static std::complex<double> prototype (Type type, double frequencyHz, double cornerHz,
                                           double gainDb, double q)
    {
        const std::complex<double> s (0.0, frequencyHz / cornerHz);
        const auto a  = std::pow (10.0, gainDb / 40.0);
        const auto ra = std::sqrt (a);

        switch (type)
        {
            case Type::bell:
                return (s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0);

            case Type::lowShelf:
                return a * (s * s + s * (ra / q) + a) / (a * s * s + s * (ra / q) + 1.0);

            case Type::highShelf:
                return a * (a * s * s + s * (ra / q) + 1.0) / (s * s + s * (ra / q) + a);
        }

        return 1.0;
    }

private:
    bool advanceSmoothing() noexcept
    {
        auto moved = false;

        const auto glide = [this, &moved] (float& value, float target, float settle)
        {
            if (! (std::abs (target - value) > 0.0f))
                return;

            value += smoothing * (target - value);

            if (std::abs (target - value) < settle)
                value = target;

            moved = true;
        };

        glide (logFrequency, targetLogFrequency, 1.0e-5f);
        glide (gainDb,       targetGainDb,       1.0e-4f);
        glide (q,            targetQ,            1.0e-5f);

        return moved;
    }

    void update() noexcept
    {
        const auto a = std::pow (10.0f, gainDb / 40.0f);
        auto g = (float) std::tan (3.14159265358979 * std::exp ((double) logFrequency) / sampleRate);
        auto k = 1.0f / q;

        switch (type)
        {
            case Type::bell:
                k  = 1.0f / (q * a);
                m0 = 1.0f; m1 = k * (a * a - 1.0f); m2 = 0.0f;
                break;

            case Type::lowShelf:
                g /= std::sqrt (a);
                m0 = 1.0f; m1 = k * (a - 1.0f); m2 = a * a - 1.0f;
                break;

            case Type::highShelf:
                g *= std::sqrt (a);
                m0 = a * a; m1 = k * (1.0f - a) * a; m2 = 1.0f - a * a;
                break;
        }

        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    const Type type;
    double sampleRate { 48000.0 };
    float smoothing { 0.001f };

    float targetLogFrequency { std::log (1000.0f) }, logFrequency { std::log (1000.0f) };
    float targetGainDb { 0.0f }, gainDb { 0.0f };
    float targetQ { 0.707f }, q { 0.707f };

    float a1 { 0.0f }, a2 { 0.0f }, a3 { 0.0f };
    float m0 { 1.0f }, m1 { 0.0f }, m2 { 0.0f };
    float ic1 { 0.0f }, ic2 { 0.0f };
};

} // namespace lacquer
