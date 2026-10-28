#pragma once

#include "Tube.h"

#include <algorithm>
#include <cmath>

namespace kiln
{

/** The output transformer.

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
    static constexpr float fluxCornerHz  = 25.0f;
    static constexpr float coreSoftness  = 0.8f;    ///< 1 / the flux the core carries before bending
    static constexpr float couplingHz    = 7.0f;
    static constexpr float resonanceHz   = 48000.0f;
    static constexpr float resonanceQ    = 0.85f;

    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        constexpr double pi = 3.14159265358979323846;

        const auto fluxG = std::tan (pi * fluxCornerHz / sampleRate);
        fluxCoeff = (float) (fluxG / (1.0 + fluxG));

        const auto cg = std::tan (pi * couplingHz / sampleRate);
        couplingCoeff = (float) (cg / (1.0 + cg));

        // The resonance would sit past Nyquist at low inner rates; it is only
        // there to shape the top octave, so keep it in band.
        const auto hz = std::min ((double) resonanceHz, 0.42 * sampleRate);
        g = (float) std::tan (pi * hz / sampleRate);
        r = 1.0f / (2.0f * resonanceQ);
        norm = 1.0f / (1.0f + 2.0f * r * g + g * g);

        reset();
    }

    void reset() noexcept { flux = coupling = h1 = h2 = 0.0f; }

    /** drive scales how hard the core is pushed for a given output level:
        1 normally, 10 dB more with the IRON pad in. */
    float process (float x, float drive) noexcept
    {
        const auto w = (x - flux) * fluxCoeff;
        const auto f = w + flux;
        flux = f + w;

        // Flux the core cannot carry, in output units.
        const auto u = f * coreSoftness * drive;
        const auto lost = f - std::tanh (u) / (coreSoftness * drive);

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
    double sampleRate { 192000.0 };
    float fluxCoeff { 0.0f }, couplingCoeff { 0.0f };
    float g { 0.0f }, r { 0.5f }, norm { 1.0f };
    float flux { 0.0f }, coupling { 0.0f }, h1 { 0.0f }, h2 { 0.0f };
};

/** The two output paths of the unit: a tube line amplifier into a
    transformer, or an electronically balanced stage that adds nothing. They
    are crossfaded rather than switched, so changing path does not click. */
class OutputStage
{
public:
    /** Distortion of the output tube at the operating level. Mostly 2nd
        harmonic, rising with level towards the +26 dBu ceiling. */
    static constexpr float tubeThdPercent = 0.1f;

    /** The IRON pad: 10 dB more into the core, 10 dB less after it. */
    static constexpr float ironPadDb = 10.0f;

    void prepare (double sampleRate) noexcept
    {
        tube.prepare (sampleRate);
        iron.prepare (sampleRate);
        tubeDrive = tube::ThdCalibration::get().driveFor (tubeThdPercent);

        const auto glide = 1.0f - std::exp (-1.0f / (0.02f * (float) sampleRate));
        smoothing = glide;
        reset();
    }

    void reset() noexcept
    {
        tube.reset();
        iron.reset();
    }

    void setTargets (bool tubePath, bool ironPad) noexcept
    {
        targetTube = tubePath ? 1.0f : 0.0f;
        targetPad  = ironPad ? std::pow (10.0f, ironPadDb / 20.0f) : 1.0f;
    }

    void snapToTargets() noexcept
    {
        tubeAmount = targetTube;
        pad = targetPad;
    }

    float process (float x) noexcept
    {
        tubeAmount += smoothing * (targetTube - tubeAmount);
        pad        += smoothing * (targetPad  - pad);

        if (std::abs (targetTube - tubeAmount) < 1.0e-5f) tubeAmount = targetTube;
        if (std::abs (targetPad  - pad)        < 1.0e-5f) pad        = targetPad;

        if (tubeAmount <= 0.0f)
        {
            // Keep the iron's state moving so switching back is seamless.
            iron.process (x, pad);
            return x;
        }

        const auto coloured = iron.process (tube.process (x, tubeDrive), pad);
        return x + tubeAmount * (coloured - x);
    }

private:
    tube::Stage tube;
    Transformer iron;

    float tubeDrive { 0.0f };
    float smoothing { 0.001f };
    float targetTube { 1.0f }, tubeAmount { 1.0f };
    float targetPad { 1.0f }, pad { 1.0f };
};

} // namespace kiln
