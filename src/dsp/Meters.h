#pragma once

#include <atomic>
#include <algorithm>

namespace crest
{

/** One lock-free "loudest value since you last looked" slot.

    The audio thread only ever raises the value, the editor takes it and leaves
    zero behind. No locks, no allocation, and a dropped frame in the editor
    costs a meter update rather than an audio glitch.
*/
class PeakSlot
{
public:
    void push (float magnitude) noexcept
    {
        auto current = value.load (std::memory_order_relaxed);

        while (magnitude > current
               && ! value.compare_exchange_weak (current, magnitude, std::memory_order_relaxed))
        {
        }
    }

    float take() noexcept { return value.exchange (0.0f, std::memory_order_relaxed); }

    void clear() noexcept { value.store (0.0f, std::memory_order_relaxed); }

private:
    std::atomic<float> value { 0.0f };
};

/** Everything the editor needs to draw, filled in by the audio thread. */
struct MeterSource
{
    PeakSlot inputPeak  [2];
    PeakSlot outputPeak [2];

    /** Largest amount the clipper took off any single sample, as a magnitude
        ratio in 0...1 (0 = untouched). Converted to dB by the editor. */
    PeakSlot reduction;

    /** Input magnitude that drove the clipper hardest this frame, in ceiling
        normalised units, so the curve view can show where the signal sits. */
    PeakSlot driveMagnitude;

    std::atomic<bool> clipLightL { false };
    std::atomic<bool> clipLightR { false };

    void clear() noexcept
    {
        for (auto& s : inputPeak)  s.clear();
        for (auto& s : outputPeak) s.clear();

        reduction.clear();
        driveMagnitude.clear();
        clipLightL.store (false, std::memory_order_relaxed);
        clipLightR.store (false, std::memory_order_relaxed);
    }
};

} // namespace crest
