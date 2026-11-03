#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace transcender
{

/** Sixteen step patterns for the trance gate; bit 0 is the first step. */
struct GatePattern
{
    const char* name;
    std::uint16_t steps;
};

inline constexpr std::array<GatePattern, 10> gatePatterns { {
    { "Straight",   0b1111111111111111 },
    { "Offbeat",    0b1010101010101010 },
    { "On beat",    0b0101010101010101 },
    { "Gallop",     0b1101101101101101 },
    { "Classic",    0b1011011010110110 },
    { "Stutter",    0b1110111011101110 },
    { "Syncopate",  0b0110110101101101 },
    { "Half",       0b0011001100110011 },
    { "Build",      0b1111111101010101 },
    { "Sparse",     0b1000100110001001 },
} };

inline bool gateStepOn (int pattern, int step) noexcept
{
    const auto& p = gatePatterns[(size_t) juce::jlimit (0, (int) gatePatterns.size() - 1, pattern)];
    return ((p.steps >> (step & 15)) & 1u) != 0;
}

/** A tempo locked volume chopper. Each "on" step opens for the set length of
    the step and closes for the rest; "off" steps stay down at 1 - depth. */
class TranceGate
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        // About 2 ms either way: fast enough to chop, slow enough not to click.
        smoothing = 1.0 - std::exp (-1.0 / (0.002 * sampleRate));
        gain = 1.0;
    }

    /** Applies the gate to a block. beatAtStart is the position of the first
        sample in quarter notes; stepsPerBeat is 2, 4 or 8. Returns the step
        that was sounding at the end of the block. */
    int process (float* left, float* right, int numSamples, double beatAtStart, double bpm,
                 int stepsPerBeat, int pattern, double depth, double length)
    {
        const auto beatsPerSample = bpm / (60.0 * sampleRate);
        int step = 0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto beat = beatAtStart + (double) i * beatsPerSample;
            const auto stepPosition = beat * (double) stepsPerBeat;
            const auto whole = std::floor (stepPosition);
            const auto within = stepPosition - whole;
            step = (int) (((long long) whole) % 16);
            if (step < 0) step += 16;

            const auto open = gateStepOn (pattern, step) && within < length;
            const auto target = open ? 1.0 : 1.0 - depth;
            gain += (target - gain) * smoothing;

            left[i]  *= (float) gain;
            right[i] *= (float) gain;
        }

        return step;
    }

private:
    double sampleRate { 48000.0 };
    double smoothing  { 0.01 };
    double gain       { 1.0 };
};

/** A stereo ping-pong echo. The right side repeats at twice the left's time,
    the 0.75 : 1.5 ratio the original's two delay lines were set to, and each
    side feeds the other through a gentle low pass so repeats darken. */
class PingPongDelay
{
public:
    void prepare (double newSampleRate, double maximumSeconds)
    {
        sampleRate = newSampleRate;
        const auto length = (size_t) std::ceil (maximumSeconds * sampleRate) + 4;

        for (auto& line : lines)
            line.assign (length, 0.0f);

        writeIndex = 0;
        damping = 1.0 - std::exp (-juce::MathConstants<double>::twoPi * 5000.0 / sampleRate);
        smoothedTime = -1.0;
        for (auto& s : dampState) s = 0.0;
    }

    void reset()
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& s : dampState) s = 0.0;
    }

    void process (float* left, float* right, int numSamples, double timeSeconds, double mix, double feedback)
    {
        if (lines[0].empty())
            return;

        const auto size = (int) lines[0].size();
        const auto maxDelay = (double) size - 3.0;
        const auto target = juce::jlimit (1.0, maxDelay / 2.0, timeSeconds * sampleRate);

        if (smoothedTime < 0.0)
            smoothedTime = target;

        // A tempo change glides the time rather than jumping it.
        const auto timeSmoothing = 1.0 - std::exp (-1.0 / (0.05 * sampleRate));

        for (int i = 0; i < numSamples; ++i)
        {
            smoothedTime += (target - smoothedTime) * timeSmoothing;

            const auto tapL = read (0, smoothedTime);
            const auto tapR = read (1, smoothedTime * 2.0);

            dampState[0] += (tapL - dampState[0]) * damping;
            dampState[1] += (tapR - dampState[1]) * damping;

            const auto inL = (double) left[i];
            const auto inR = (double) right[i];
            const auto mono = 0.5 * (inL + inR);

            lines[0][(size_t) writeIndex] = (float) (mono + dampState[1] * feedback);
            lines[1][(size_t) writeIndex] = (float) (dampState[0] * feedback);

            left[i]  = (float) (inL + tapL * mix);
            right[i] = (float) (inR + tapR * mix);

            if (++writeIndex >= size)
                writeIndex = 0;
        }
    }

private:
    double read (int line, double delaySamples) const noexcept
    {
        const auto size = (int) lines[(size_t) line].size();
        auto position = (double) writeIndex - delaySamples;
        while (position < 0.0) position += (double) size;

        const auto i0 = (int) position;
        const auto frac = position - (double) i0;
        const auto i1 = (i0 + 1) % size;
        const auto& buffer = lines[(size_t) line];
        return (double) buffer[(size_t) i0] + frac * ((double) buffer[(size_t) i1] - (double) buffer[(size_t) i0]);
    }

    double sampleRate { 48000.0 };
    std::array<std::vector<float>, 2> lines;
    std::array<double, 2> dampState {};
    int writeIndex { 0 };
    double damping { 0.5 };
    double smoothedTime { -1.0 };
};

/** A sine LFO. */
struct Lfo
{
    double phase { 0.0 };

    double advance (double hz, int numSamples, double sampleRate) noexcept
    {
        const auto value = std::sin (juce::MathConstants<double>::twoPi * phase);
        phase += hz * (double) numSamples / sampleRate;
        phase -= std::floor (phase);
        return value;
    }
};

} // namespace transcender
