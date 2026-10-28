#pragma once

#include <atomic>
#include <array>
#include <algorithm>

namespace zenith
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

/** The scrolling history behind the main display: what went in, what came
    out, and how much the limiter took, one column per few milliseconds.

    The audio thread fills one column at a time and publishes it by bumping an
    index; the editor keeps its own read position. A reader that catches a
    column mid-write sees one wrong pixel column and nothing worse, which is
    the right trade for a display.
*/
class History
{
public:
    struct Column
    {
        float input     { 0.0f };   ///< peak magnitude, after the gain control, linear
        float output    { 0.0f };   ///< peak magnitude, linear
        float reduction { 0.0f };   ///< most gain reduction, dB, positive
    };

    static constexpr int capacity = 2048;

    void prepare (int newSamplesPerColumn) noexcept
    {
        samplesPerColumn = std::max (1, newSamplesPerColumn);
        pending = { };
        counter = 0;
    }

    void push (float input, float output, float reductionDb) noexcept
    {
        pending.input     = std::max (pending.input, input);
        pending.output    = std::max (pending.output, output);
        pending.reduction = std::max (pending.reduction, reductionDb);

        if (++counter < samplesPerColumn)
            return;

        const auto index = writeIndex.load (std::memory_order_relaxed);
        columns[(size_t) (index % capacity)] = pending;
        writeIndex.store (index + 1, std::memory_order_release);

        pending = { };
        counter = 0;
    }

    int getWriteIndex() const noexcept { return writeIndex.load (std::memory_order_acquire); }

    Column getColumn (int index) const noexcept
    {
        return columns[(size_t) (((index % capacity) + capacity) % capacity)];
    }

private:
    std::array<Column, capacity> columns { };
    std::atomic<int> writeIndex { 0 };

    Column pending { };
    int    counter { 0 };
    int    samplesPerColumn { 256 };
};

/** Everything the editor reads, written by the audio thread. */
struct MeterSource
{
    PeakSlot inputPeak  [2];
    PeakSlot outputPeak [2];
    PeakSlot outputTruePeak;
    PeakSlot reduction;           ///< dB, positive

    std::atomic<float> momentary   { -100.0f };
    std::atomic<float> shortTerm   { -100.0f };
    std::atomic<float> integrated  { -100.0f };
    std::atomic<float> range       { 0.0f };
    std::atomic<float> truePeakMax { 0.0f };   ///< linear, since the last reset
    std::atomic<float> matchGainDb { 0.0f };

    /** Set by the editor; the audio thread clears the integrated measurements. */
    std::atomic<bool> resetRequested { false };

    History history;
};

} // namespace zenith
