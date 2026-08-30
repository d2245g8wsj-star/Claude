#pragma once

#include <cmath>
#include <algorithm>

namespace crest
{

/** Second order low pass, topology preserving transform.

    Used on the clipper's own difference signal rather than on the audio: see
    the note on the sidechain in PluginProcessor.cpp. A TPT structure rather
    than a direct form biquad because the cutoff is a user control, and a TPT
    filter stays stable and well behaved while its coefficients move, where a
    direct form can spit on a fast sweep.

    Butterworth response, 12 dB per octave. No allocation, no reference counted
    coefficient objects, so the cutoff can be updated on the audio thread.
*/
class LowSplit
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        updateCoefficients();
        reset();
    }

    void reset() noexcept { state1 = state2 = 0.0f; }

    void setCutoff (float frequencyHz) noexcept
    {
        // Keep the prewarped corner well below Nyquist: tan() runs away as the
        // cutoff approaches it, and at that point the filter is doing nothing
        // useful anyway.
        const auto limited = std::clamp (frequencyHz, 5.0f,
                                         static_cast<float> (sampleRate) * 0.45f);

        if (std::abs (limited - cutoff) < 1.0e-3f)
            return;

        cutoff = limited;
        updateCoefficients();
    }

    float process (float x) noexcept
    {
        const auto highpass = h * (x - state1 * (twoR + g) - state2);

        const auto bandpass = g * highpass + state1;
        state1 = g * highpass + bandpass;

        const auto lowpass = g * bandpass + state2;
        state2 = g * bandpass + lowpass;

        return lowpass;
    }

private:
    void updateCoefficients() noexcept
    {
        constexpr float pi = 3.14159265358979323846f;

        g    = std::tan (pi * cutoff / static_cast<float> (sampleRate));
        twoR = 1.41421356f;                       // Butterworth: 2R = 1/Q = sqrt(2)
        h    = 1.0f / (1.0f + twoR * g + g * g);
    }

    double sampleRate { 48000.0 };
    float  cutoff     { 120.0f };
    float  g          { 0.0f };
    float  twoR       { 1.41421356f };
    float  h          { 1.0f };
    float  state1     { 0.0f };
    float  state2     { 0.0f };
};

} // namespace crest
