#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <cmath>

namespace transcender
{

enum class Wave { saw = 0, square, triangle, sine };

/** Number of oscillators in one voice: five round the played pitch and five
    an octave above it, the layout of the original's "Oscillator 1..5" and
    "Oscillator A..E" banks. */
inline constexpr int oscillatorsPerBank = 5;
inline constexpr int oscillatorsPerVoice = 2 * oscillatorsPerBank;

/** Settings a voice reads once per control step. */
struct VoiceSettings
{
    Wave   wave          { Wave::saw };
    double detuneCents   { 18.0 };   // spread of the outermost oscillator either side
    double octaveLevel   { 0.5 };    // level of the upper bank against the lower
    double stereoWidth   { 0.7 };    // 0 = all centre, 1 = outer oscillators hard left and right
    double cutoffHz      { 6000.0 }; // filter cutoff before envelope and LFO
    double resonance     { 0.2 };    // 0 .. 1
    double envToFilter   { 0.3 };    // -1 .. 1, scaled to +-6 octaves
    double lfoOctaves    { 0.0 };    // LFO contribution this step, already scaled by depth
    double glideSeconds  { 0.0 };
    double pitchBendSemis{ 0.0 };
};

/** PolyBLEP correction for a unit step at phase 0, t in [0, 1). */
inline double polyBlep (double t, double dt) noexcept
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }

    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }

    return 0.0;
}

/** One band limited oscillator. Phase runs 0 .. 1. */
struct Oscillator
{
    double phase { 0.0 };

    double next (Wave wave, double increment) noexcept
    {
        double out = 0.0;

        switch (wave)
        {
            case Wave::saw:
                out = 2.0 * phase - 1.0 - polyBlep (phase, increment);
                break;

            case Wave::square:
            {
                out = phase < 0.5 ? 1.0 : -1.0;
                out += polyBlep (phase, increment);
                auto shifted = phase + 0.5;
                if (shifted >= 1.0) shifted -= 1.0;
                out -= polyBlep (shifted, increment);
                break;
            }

            case Wave::triangle:
                out = phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase;
                break;

            case Wave::sine:
                out = std::sin (juce::MathConstants<double>::twoPi * phase);
                break;
        }

        phase += increment;
        if (phase >= 1.0) phase -= 1.0;
        return out;
    }
};

/** Topology preserving state variable low pass, after Zavalishin. */
struct StateVariableLowPass
{
    double ic1 { 0.0 }, ic2 { 0.0 };
    double g { 0.0 }, k { 2.0 }, a1 { 1.0 }, a2 { 0.0 }, a3 { 0.0 };

    void set (double cutoffHz, double resonance, double sampleRate) noexcept
    {
        const auto limited = juce::jlimit (16.0, sampleRate * 0.45, cutoffHz);
        g  = std::tan (juce::MathConstants<double>::pi * limited / sampleRate);
        // Resonance 0 .. 1 maps Q from 0.5 to about 25.
        k  = 2.0 - 1.96 * juce::jlimit (0.0, 1.0, resonance);
        a1 = 1.0 / (1.0 + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }

    double process (double x) noexcept
    {
        const auto v3 = x - ic2;
        const auto v1 = a1 * ic1 + a2 * v3;
        const auto v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0 * v1 - ic1;
        ic2 = 2.0 * v2 - ic2;
        return v2;
    }

    void reset() noexcept { ic1 = ic2 = 0.0; }
};

/** One note: ten oscillators, a stereo filter and an envelope that drives
    both the amplitude and the filter. */
class Voice
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        envelope.setSampleRate (sampleRate);
        reset();
    }

    void reset()
    {
        envelope.reset();
        for (auto& f : filters) f.reset();
        note = -1;
        held = false;
    }

    void setEnvelope (const juce::ADSR::Parameters& p) { envelope.setParameters (p); }

    /** Starts a note. With retrigger false (mono legato) only the pitch
        moves; the envelope carries on. */
    void start (int midiNote, float velocity, bool retrigger, juce::Random& random)
    {
        const auto target = (double) midiNote;

        if (note < 0 || ! envelope.isActive())
            currentPitch = target;

        targetPitch = target;
        note  = midiNote;
        held  = true;
        level = 0.45 + 0.55 * (double) velocity;

        if (retrigger || ! envelope.isActive())
        {
            if (! envelope.isActive())
            {
                // Free running phases are what makes a stack of detuned saws
                // sound wide rather than like one loud saw at note on.
                for (auto& o : oscillators)
                    o.phase = random.nextDouble();

                for (auto& f : filters) f.reset();
            }

            envelope.noteOn();
        }
    }

    void glideTo (int midiNote)
    {
        targetPitch = (double) midiNote;
        note = midiNote;
    }

    void stop (bool allowTail)
    {
        held = false;

        if (allowTail)
            envelope.noteOff();
        else
            reset();
    }

    bool isActive() const noexcept   { return envelope.isActive(); }
    bool isHeld() const noexcept     { return held; }
    int  getNote() const noexcept    { return note; }
    double getPitch() const noexcept { return currentPitch; }

    /** Adds numSamples of this voice into left and right. */
    void render (const VoiceSettings& s, float* left, float* right, int numSamples)
    {
        if (! envelope.isActive())
            return;

        // Glide: a constant time per glide, in log frequency.
        if (s.glideSeconds > 0.0005)
        {
            const auto coefficient = std::exp (-1.0 / (s.glideSeconds * sampleRate / 4.0));
            glideCoefficient = coefficient;
        }
        else
        {
            glideCoefficient = 0.0;
        }

        // Per oscillator detune in cents and pan position, fixed shapes
        // scaled by the panel. The upper bank sits between the lower bank's
        // positions so the two never beat in step.
        static constexpr std::array<double, oscillatorsPerVoice> detuneShape {
            0.0, -0.5, 0.5, -1.0, 1.0,     // lower bank
            0.12, -0.25, 0.25, -0.75, 0.75 // upper bank, an octave up
        };
        static constexpr std::array<double, oscillatorsPerVoice> panShape {
            0.0, -0.55, 0.55, -1.0, 1.0,
            0.0, 0.8, -0.8, 0.35, -0.35
        };

        const auto lowerGain = 1.0 / std::sqrt ((double) oscillatorsPerBank);
        const auto upperGain = lowerGain * s.octaveLevel;

        std::array<double, oscillatorsPerVoice> gainL {}, gainR {};

        for (int i = 0; i < oscillatorsPerVoice; ++i)
        {
            const auto pan   = panShape[(size_t) i] * s.stereoWidth;           // -1 .. 1
            const auto angle = (pan + 1.0) * juce::MathConstants<double>::pi * 0.25;
            const auto bank  = i < oscillatorsPerBank ? lowerGain : upperGain;
            gainL[(size_t) i] = bank * std::cos (angle) * juce::MathConstants<double>::sqrt2;
            gainR[(size_t) i] = bank * std::sin (angle) * juce::MathConstants<double>::sqrt2;
        }

        constexpr int controlStep = 16;

        for (int start = 0; start < numSamples; start += controlStep)
        {
            const auto count = juce::jmin (controlStep, numSamples - start);

            for (int n = 0; n < count; ++n)
                currentPitch = targetPitch + (currentPitch - targetPitch) * glideCoefficient;

            const auto basePitch = currentPitch + s.pitchBendSemis;
            std::array<double, oscillatorsPerVoice> increments {};

            for (int i = 0; i < oscillatorsPerVoice; ++i)
            {
                const auto semis = basePitch + (i < oscillatorsPerBank ? 0.0 : 12.0)
                                 + detuneShape[(size_t) i] * s.detuneCents / 100.0;
                const auto hz = 440.0 * std::pow (2.0, (semis - 69.0) / 12.0);
                increments[(size_t) i] = juce::jmin (hz / sampleRate, 0.49);
            }

            // Filter: the envelope's own level is the modulation source, as
            // on the original, which shares one ADSR between VCA and filter.
            const auto envNow = (double) lastEnvelope;
            const auto octaves = s.envToFilter * 6.0 * envNow + s.lfoOctaves;
            const auto keyTrack = (basePitch - 60.0) / 12.0 * 0.5; // half key tracking
            const auto cutoff = s.cutoffHz * std::pow (2.0, octaves + keyTrack);

            for (auto& f : filters)
                f.set (cutoff, s.resonance, sampleRate);

            for (int n = 0; n < count; ++n)
            {
                double l = 0.0, r = 0.0;

                for (int i = 0; i < oscillatorsPerVoice; ++i)
                {
                    const auto x = oscillators[(size_t) i].next (s.wave, increments[(size_t) i]);
                    l += x * gainL[(size_t) i];
                    r += x * gainR[(size_t) i];
                }

                const auto env = envelope.getNextSample();
                lastEnvelope = env;
                const auto amp = (double) env * level * 0.25;

                left[start + n]  += (float) (filters[0].process (l) * amp);
                right[start + n] += (float) (filters[1].process (r) * amp);
            }

            if (! envelope.isActive())
            {
                note = -1;
                break;
            }
        }
    }

private:
    double sampleRate { 48000.0 };
    std::array<Oscillator, oscillatorsPerVoice> oscillators;
    std::array<StateVariableLowPass, 2> filters;
    juce::ADSR envelope;

    int    note { -1 };
    bool   held { false };
    double level { 1.0 };
    double currentPitch { 60.0 }, targetPitch { 60.0 };
    double glideCoefficient { 0.0 };
    float  lastEnvelope { 0.0f };
};

} // namespace transcender
