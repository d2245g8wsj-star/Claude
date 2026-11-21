#pragma once

#include <algorithm>
#include <array>
#include <atomic>

namespace beacon
{

/** What the audio thread tells the editor, per channel. Each field is written
    with a max from the audio thread and swapped out by the editor, so a peak
    between two frames is never lost. A slot the audio thread has not written
    since the editor last looked reads below zero: when the host's blocks are
    longer than a frame of the editor, some frames get no new reading, and the
    meters keep the last one instead of falling back to rest. */
struct MeterSource
{
    struct Channel
    {
        std::atomic<float> reductionDb { 0.0f };   ///< dB, positive
        std::atomic<float> thdDrive    { 0.0f };   ///< peak into the THD curve
        std::atomic<float> inPeak      { 0.0f };   ///< peak at the input, linear
        std::atomic<float> outPeak     { 0.0f };   ///< peak at the output, linear
    };

    std::array<Channel, 2> channels;

    /** The editor's last reading of a slot. */
    struct Held
    {
        float value { 0.0f };
        int missed { 0 };
    };

    /** Frames a reading is held without a new block: a quarter of a second at
        30 frames, longer than any block a host sends, short enough that the
        meters come to rest when the host stops calling. */
    static constexpr int maxMissedFrames = 8;

    static void raise (std::atomic<float>& slot, float value) noexcept
    {
        value = std::max (0.0f, value);
        auto current = slot.load (std::memory_order_relaxed);

        while (value > current && ! slot.compare_exchange_weak (current, value, std::memory_order_relaxed))
        {
        }
    }

    static float take (std::atomic<float>& slot) noexcept
    {
        return std::max (0.0f, slot.exchange (-1.0f, std::memory_order_relaxed));
    }

    /** For the editor: the slot's new reading or, in a frame without a block,
        the last one. */
    static float take (std::atomic<float>& slot, Held& held) noexcept
    {
        const auto value = slot.exchange (-1.0f, std::memory_order_relaxed);

        if (value >= 0.0f)
        {
            held.value = value;
            held.missed = 0;
        }
        else if (++held.missed > maxMissedFrames)
        {
            held.value = 0.0f;
        }

        return held.value;
    }
};

} // namespace beacon
