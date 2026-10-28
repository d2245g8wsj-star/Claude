#pragma once

#include <array>
#include <atomic>

namespace swell
{

/** What the audio thread tells the editor, per channel. Each field is written
    with a max from the audio thread and swapped out by the editor, so a peak
    between two frames is never lost. */
struct MeterSource
{
    static constexpr int numBands = 3;

    struct Channel
    {
        std::atomic<float> outputPeak { 0.0f };
        std::array<std::atomic<float>, numBands> bandDrive { };
    };

    std::array<Channel, 2> channels;

    static void raise (std::atomic<float>& slot, float value) noexcept
    {
        auto current = slot.load (std::memory_order_relaxed);

        while (value > current && ! slot.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {
        }
    }

    static float take (std::atomic<float>& slot) noexcept
    {
        return slot.exchange (0.0f, std::memory_order_relaxed);
    }
};

} // namespace swell
