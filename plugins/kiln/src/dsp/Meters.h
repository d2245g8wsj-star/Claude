#pragma once

#include <array>
#include <atomic>

namespace kiln
{

/** What the audio thread tells the editor, per channel. Each field is written
    with a max from the audio thread and swapped out by the editor, so a peak
    between two frames is never lost. */
struct MeterSource
{
    struct Channel
    {
        std::atomic<float> reductionDb { 0.0f };   ///< dB, positive
        std::atomic<float> thdDrive    { 0.0f };   ///< peak into the THD curve
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

} // namespace kiln
