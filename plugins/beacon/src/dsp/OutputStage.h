#pragma once

#include "Tube.h"

#include <algorithm>
#include <cmath>

namespace beacon
{

/** A Carnhill style audio transformer. The unit has four: one at each
    channel's input, always in circuit, and one at each output in RED mode.

    A transformer saturates on flux, and flux is the integral of the voltage
    across the winding, so for the same level a low note pushes the core much
    harder than a high one. The flux is followed with a one pole low pass
    whose corner sits below the audio band (at low frequencies it tracks the
    signal, above the corner it falls an octave at a time), and only the part
    of it the core cannot carry is taken off the output. Quiet or high
    material passes untouched; loud bass comes out with a rounded, mostly odd
    series, which is the sound of iron.

    The rest is linear: the coupling capacitor's high pass at the bottom and
    the leakage inductance resonating with the winding capacitance at the top.
*/
class Transformer
{
public:
    struct Voicing
    {
        float coreSoftness;   ///< 1 / the flux the core carries before bending
        float couplingHz;
        float resonanceHz;
        float resonanceQ;
    };

    /** The input side runs at line level with plenty of core; the overall
        response of the unit is 12 Hz to 150 kHz at -3 dB. */
    static constexpr Voicing input  { 0.45f, 8.0f, 150000.0f, 0.70f };

    /** The output side is driven by the tube stage and is meant to be heard. */
    static constexpr Voicing output { 0.80f, 5.0f, 48000.0f, 0.85f };

    static constexpr float fluxCornerHz = 25.0f;

    explicit Transformer (Voicing v) noexcept : voicing (v) {}

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        constexpr double pi = 3.14159265358979323846;

        const auto fluxG = std::tan (pi * fluxCornerHz / sampleRate);
        fluxCoeff = (float) (fluxG / (1.0 + fluxG));

        const auto cg = std::tan (pi * voicing.couplingHz / sampleRate);
        couplingCoeff = (float) (cg / (1.0 + cg));

        // The resonance would sit past Nyquist at low inner rates; it is only
        // there to shape the top octave, so keep it in band.
        const auto hz = std::min ((double) voicing.resonanceHz, 0.42 * sampleRate);
        g = (float) std::tan (pi * hz / sampleRate);
        r = 1.0f / (2.0f * voicing.resonanceQ);
        norm = 1.0f / (1.0f + 2.0f * r * g + g * g);

        reset();
    }

    void reset() noexcept { flux = coupling = h1 = h2 = 0.0f; }

    float process (float x) noexcept
    {
        const auto w = (x - flux) * fluxCoeff;
        const auto f = w + flux;
        flux = f + w;

        // Flux the core cannot carry, in output units.
        const auto k = voicing.coreSoftness;
        const auto lost = f - std::tanh (f * k) / k;

        auto y = x - lost;

        // Coupling capacitor.
        const auto v = (y - coupling) * couplingCoeff;
        const auto low = v + coupling;
        coupling = low + v;
        y -= low;

        // Leakage resonance, a gentle low pass with a little lift before it.
        const auto hp = (y - (2.0f * r + g) * h1 - h2) * norm;
        const auto bp = g * hp + h1;
        const auto lp = g * bp + h2;
        h1 = g * hp + bp;
        h2 = g * bp + lp;

        return lp;
    }

private:
    const Voicing voicing;

    double sampleRate { 192000.0 };
    float fluxCoeff { 0.0f }, couplingCoeff { 0.0f };
    float g { 0.0f }, r { 0.5f }, norm { 1.0f };
    float flux { 0.0f }, coupling { 0.0f }, h1 { 0.0f }, h2 { 0.0f };
};

/** The two output paths of the unit. RED: a tube line amplifier into a
    transformer, ending in a passive attenuator (the IRON PAD). GREEN: an
    electronically balanced stage that adds nothing. They are crossfaded
    rather than switched, so changing path does not click.

    The make-up gain sits in front of this stage, so it is what drives the
    iron; the pad comes after the transformer and only takes level away. Turn
    both up together and the core works harder at the same output level. */
class OutputStage
{
public:
    /** Distortion of the output tube at the operating level. Mostly 2nd
        harmonic, rising with level towards the +26 dBu ceiling. */
    static constexpr float tubeThdPercent = 0.1f;

    static constexpr float maxPadDb = 15.0f;

    void prepare (double sampleRate) noexcept
    {
        tube.prepare (sampleRate);
        iron.prepare (sampleRate);
        tubeDrive = tube::ThdCalibration::get().driveFor (tubeThdPercent);

        smoothing = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
        reset();
    }

    void reset() noexcept
    {
        tube.reset();
        iron.reset();
    }

    void setTargets (bool ironPath, float padDb) noexcept
    {
        targetIron = ironPath ? 1.0f : 0.0f;
        targetPad  = std::pow (10.0f, -std::clamp (padDb, 0.0f, maxPadDb) / 20.0f);
    }

    void snapToTargets() noexcept
    {
        ironAmount = targetIron;
        pad = targetPad;
    }

    float process (float x) noexcept
    {
        ironAmount += smoothing * (targetIron - ironAmount);
        pad        += smoothing * (targetPad  - pad);

        if (std::abs (targetIron - ironAmount) < 1.0e-5f) ironAmount = targetIron;
        if (std::abs (targetPad  - pad)        < 1.0e-6f) pad        = targetPad;

        if (ironAmount <= 0.0f)
        {
            // Keep the iron's state moving so switching back is seamless.
            iron.process (x);
            return x;
        }

        const auto coloured = pad * iron.process (tube.process (x, tubeDrive));
        return x + ironAmount * (coloured - x);
    }

private:
    tube::Stage tube;
    Transformer iron { Transformer::output };

    float tubeDrive { 0.0f };
    float smoothing { 0.001f };
    float targetIron { 1.0f }, ironAmount { 1.0f };
    float targetPad { 1.0f }, pad { 1.0f };
};

} // namespace beacon
