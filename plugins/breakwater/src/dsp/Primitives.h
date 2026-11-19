#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace breakwater
{

inline float gainToDb (float gain) noexcept    { return 20.0f * std::log10 (std::max (gain, 1.0e-9f)); }
inline float dbToGain (float db) noexcept      { return std::pow (10.0f, db * 0.05f); }

/** Coefficient of a one pole smoother reaching 63 % in `ms` at `sampleRate`. */
inline float onePole (double ms, double sampleRate) noexcept
{
    if (ms <= 0.0)
        return 0.0f;

    return (float) std::exp (-1000.0 / (ms * sampleRate));
}

/** Maximum of the last `window` values pushed, in O(1) amortised time.

    A monotonic queue: a value is dropped from the back as soon as a larger one
    arrives, because it can never be the maximum again. Storage is fixed at
    prepare(), so pushing never allocates.
*/
class SlidingMax
{
public:
    void prepare (int maximumWindow)
    {
        capacity = std::max (1, maximumWindow) + 1;
        values.assign ((size_t) capacity, 0.0f);
        stamps.assign ((size_t) capacity, 0);
        window = 1;
        reset();
    }

    void setWindow (int newWindow) noexcept { window = std::clamp (newWindow, 1, capacity - 1); }

    void reset() noexcept
    {
        front = 0;
        count = 0;
        now   = 0;
    }

    float push (float value) noexcept
    {
        while (count > 0 && values[(size_t) slot (count - 1)] <= value)
            --count;

        values[(size_t) slot (count)] = value;
        stamps[(size_t) slot (count)] = now;
        ++count;

        while (stamps[(size_t) front] <= now - window)
        {
            front = (front + 1) % capacity;
            --count;
        }

        ++now;
        return values[(size_t) front];
    }

private:
    int slot (int indexFromFront) const noexcept { return (front + indexFromFront) % capacity; }

    std::vector<float>   values;
    std::vector<int64_t> stamps;
    int     capacity { 2 };
    int     window   { 1 };
    int     front    { 0 };
    int     count    { 0 };
    int64_t now      { 0 };
};

/** Running mean over a fixed number of samples. The sum is kept in double so
    the add-one-drop-one update does not drift over hours of audio. */
class BoxAverage
{
public:
    void prepare (int maximumLength)
    {
        ring.assign ((size_t) std::max (1, maximumLength), 0.0f);
        setLength (1);
    }

    /** Changing the length restarts the average from zero, or with
        keepLevel, from the value it was giving, so the output does not jump. */
    void setLength (int newLength, bool keepLevel = false) noexcept
    {
        const auto level = length == 1 ? last : (float) (sum / (double) length);

        length = std::clamp (newLength, 1, (int) ring.size());
        reset();

        if (keepLevel)
        {
            std::fill (ring.begin(), ring.begin() + length, level);
            sum  = (double) level * (double) length;
            last = level;
        }
    }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        sum   = 0.0;
        index = 0;
        last  = 0.0f;
    }

    float push (float value) noexcept
    {
        if (length == 1)
            return last = value;

        sum += (double) value - (double) ring[(size_t) index];
        ring[(size_t) index] = value;
        index = index + 1 == length ? 0 : index + 1;

        return (float) (sum / (double) length);
    }

private:
    std::vector<float> ring;
    double sum    { 0.0 };
    float  last   { 0.0f };
    int    length { 1 };
    int    index  { 0 };
};

/** Integer sample delay with a fixed maximum. */
class SampleDelay
{
public:
    void prepare (int maximumDelay)
    {
        ring.assign ((size_t) std::max (1, maximumDelay) + 1, 0.0f);
        setDelay (0);
    }

    void setDelay (int samples) noexcept
    {
        delay = std::clamp (samples, 0, (int) ring.size() - 1);
        reset();
    }

    int getDelay() const noexcept { return delay; }

    void reset() noexcept
    {
        std::fill (ring.begin(), ring.end(), 0.0f);
        write = 0;
    }

    float process (float value) noexcept
    {
        if (delay == 0)
            return value;

        const auto size = (int) ring.size();
        ring[(size_t) write] = value;

        auto read = write - delay;
        if (read < 0)
            read += size;

        write = write + 1 == size ? 0 : write + 1;
        return ring[(size_t) read];
    }

private:
    std::vector<float> ring;
    int delay { 0 };
    int write { 0 };
};

} // namespace breakwater
