#pragma once

#include <atomic>
#include <array>
#include <algorithm>
#include <cmath>

namespace crest
{

/** A scrolling picture of what the clipper is doing.

    A level meter tells you how loud the signal is; it cannot tell you whether
    the clipper took the top two samples off a snare or flattened a whole bass
    note. This carries both envelopes - the signal going into the shaper and the
    signal coming out - so the difference between them is drawn rather than
    inferred. The material the clipper removed is literally the gap.

    Both are captured in the normalised domain where 1.0 is the ceiling, so the
    two are directly comparable however the gain staging is set.

    The audio thread fills one column at a time and publishes it by bumping an
    index; the editor keeps its own read position. A column is four floats
    written without a lock, so a reader that catches one mid-write sees one
    wrong pixel column and nothing worse - which is the right trade for a
    display, and cheaper than making the audio thread wait.
*/
class WaveformTap
{
public:
    struct Column
    {
        float inputMin  { 0.0f };
        float inputMax  { 0.0f };
        float outputMin { 0.0f };
        float outputMax { 0.0f };
    };

    static constexpr int capacity = 2048;

    /** @param samplesPerColumn  how much audio one column of pixels covers */
    void prepare (int newSamplesPerColumn) noexcept
    {
        samplesPerColumn = std::max (1, newSamplesPerColumn);
        reset();
    }

    void reset() noexcept
    {
        pending = { };
        counter = 0;
        writeIndex.store (0, std::memory_order_relaxed);

        for (auto& column : columns)
            column = { };
    }

    /** Called once per base rate sample, both values in ceiling normalised units. */
    void push (float input, float output) noexcept
    {
        pending.inputMin  = std::min (pending.inputMin,  input);
        pending.inputMax  = std::max (pending.inputMax,  input);
        pending.outputMin = std::min (pending.outputMin, output);
        pending.outputMax = std::max (pending.outputMax, output);

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

} // namespace crest
