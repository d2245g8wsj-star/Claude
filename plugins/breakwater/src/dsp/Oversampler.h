#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace breakwater
{

/** Linear phase oversampling by 1, 2, 4, 8, 16 or 32, built as a cascade of half
    band FIR stages.

    The first stage carries the whole burden: its transition band has to sit
    between 20 kHz and the original Nyquist frequency at 44.1 kHz, so it is
    long. Every stage after it only has to keep the images of the original
    band apart, which leaves it a wide transition and few taps. Each stage is
    a Kaiser windowed sinc of length 4m + 3, so every other tap is zero and
    the centre tap is exactly one half, and the polyphase forms below only
    ever touch the taps that are not zero.

    The stages are sized for about 110 dB of stopband rejection with passband
    ripple far below a thousandth of a dB, which is what a mastering chain
    wants: switching the factor changes how the limiter behaves, not the tone.

    The latency of the whole round trip is padded inside the oversampled
    domain to a whole number of samples at the original rate, so the host can
    compensate it exactly and a dry path can be lined up with it sample for
    sample.
*/
class Oversampler
{
public:
    static constexpr int maxStages   = 5;
    static constexpr int maxChannels = 2;

    /** @param stages      0 to 5: the factor is 2^stages
        @param maxBlock    the most samples at the original rate per call */
    void prepare (int newStages, int maxBlock)
    {
        stages = std::clamp (newStages, 0, maxStages);
        maxBlockSize = std::max (1, maxBlock);

        const auto factor = 1 << stages;

        for (auto& buffer : inner)
            buffer.assign ((size_t) (maxBlockSize * factor), 0.0f);

        for (auto& buffer : scratch)
            buffer.assign ((size_t) (maxBlockSize * factor), 0.0f);

        for (int ch = 0; ch < maxChannels; ++ch)
        {
            up[(size_t) ch].clear();
            down[(size_t) ch].clear();

            for (int s = 0; s < stages; ++s)
            {
                up[(size_t) ch].push_back (std::make_unique<HalfBand> (tapsForStage (s), false));
                down[(size_t) ch].push_back (std::make_unique<HalfBand> (tapsForStage (s), true));
            }
        }

        // Each stage s delays by its centre tap twice (up and down), at a rate
        // of 2^(s+1). Counted at the innermost rate:
        int innerDelay = 0;

        for (int s = 0; s < stages; ++s)
            innerDelay += 2 * ((tapsForStage (s) - 1) / 2) * (1 << (stages - s - 1));

        const auto padding = (factor - innerDelay % factor) % factor;

        for (auto& p : pad)
        {
            p.assign ((size_t) padding + 1, 0.0f);
            padIndex = 0;
        }

        padLength = padding;
        latency = (innerDelay + padding) / factor;

        reset();
    }

    void reset() noexcept
    {
        for (int ch = 0; ch < maxChannels; ++ch)
        {
            for (auto& s : up[(size_t) ch])   s->reset();
            for (auto& s : down[(size_t) ch]) s->reset();
            std::fill (pad[(size_t) ch].begin(), pad[(size_t) ch].end(), 0.0f);
        }

        padIndex = 0;
    }

    int getFactor() const noexcept  { return 1 << stages; }
    int getLatency() const noexcept { return latency; }

    /** Upsamples numSamples into the internal buffers and returns how many
        samples each channel now holds. */
    int processUp (const float* const* input, int numChannels, int numSamples) noexcept
    {
        numSamples = std::min (numSamples, maxBlockSize);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* target = inner[(size_t) ch].data();
            std::copy (input[ch], input[ch] + numSamples, target);

            auto count = numSamples;

            for (auto& stage : up[(size_t) ch])
            {
                auto* temp = scratch[(size_t) ch].data();
                std::copy (target, target + count, temp);

                for (int i = 0; i < count; ++i)
                    stage->interpolate (temp[i], target[2 * i], target[2 * i + 1]);

                count *= 2;
            }
        }

        return numSamples << stages;
    }

    float* getChannel (int ch) noexcept { return inner[(size_t) ch].data(); }

    /** Downsamples the internal buffers back into output, numSamples at the
        original rate. */
    void processDown (float* const* output, int numChannels, int numSamples) noexcept
    {
        numSamples = std::min (numSamples, maxBlockSize);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* source = inner[(size_t) ch].data();
            auto count = numSamples << stages;

            if (padLength > 0)
            {
                auto& line = pad[(size_t) ch];
                auto index = padIndex;

                for (int i = 0; i < count; ++i)
                {
                    const auto delayed = line[(size_t) index];
                    line[(size_t) index] = source[i];
                    source[i] = delayed;
                    index = index + 1 == padLength ? 0 : index + 1;
                }

                if (ch == numChannels - 1)
                    padIndex = index;
            }

            for (int s = stages - 1; s >= 0; --s)
            {
                auto& stage = *down[(size_t) ch][(size_t) s];
                count /= 2;

                for (int i = 0; i < count; ++i)
                    source[i] = stage.decimate (source[2 * i], source[2 * i + 1]);
            }

            std::copy (source, source + numSamples, output[ch]);
        }
    }

private:
    /** Taps per stage, each 4m + 3. The first is long; the rest are short. */
    static int tapsForStage (int stage) noexcept
    {
        constexpr std::array<int, maxStages> taps { 167, 31, 23, 19, 19 };
        return taps[(size_t) stage];
    }

    /** One half band stage, used either to interpolate or to decimate.

        With the centre at c (odd) and h[c] = 1/2, the taps that are not zero
        are the centre and the even ones. Interpolating by two (with a gain of
        two), the even outputs are the even taps against the input, and the
        odd outputs are the input delayed by (c - 1) / 2. Decimating, each
        output sits on an even input: the even taps against the even inputs,
        plus one half of the odd input c samples back, which is the odd input
        of the pair (c + 1) / 2 pairs ago. Either way the delay is c samples
        at the higher rate.
    */
    class HalfBand
    {
    public:
        HalfBand (int length, bool decimator)
        {
            const auto centre = (length - 1) / 2;
            constexpr double beta = 11.0;   // about 110 dB

            for (int k = 0; k < length; k += 2)
            {
                const auto t = (k - centre) / 2.0;
                const auto sinc = std::sin (3.14159265358979323846 * t) / (3.14159265358979323846 * t);
                const auto r = (double) (k - centre) / centre;
                const auto window = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
                evenTaps.push_back ((float) (0.5 * sinc * window));
            }

            // Normalise so the even taps sum to exactly one half: unity DC
            // gain. The prototype is symmetric, so its taps read the same
            // oldest first, which is the order the dot products run in.
            double sum = 0.0;
            for (auto t : evenTaps) sum += t;
            for (auto& t : evenTaps) t = (float) (t * 0.5 / sum);

            numTaps = (int) evenTaps.size();
            centreDelay = decimator ? (centre + 1) / 2 : (centre - 1) / 2;   // in calls

            evenHistory.assign ((size_t) (2 * numTaps + 1), 0.0f);
            oddHistory.assign ((size_t) (centreDelay + 1), 0.0f);
            reset();
        }

        void reset() noexcept
        {
            std::fill (evenHistory.begin(), evenHistory.end(), 0.0f);
            std::fill (oddHistory.begin(), oddHistory.end(), 0.0f);
            evenPosition = oddPosition = 0;
        }

        void interpolate (float x, float& first, float& second) noexcept
        {
            first  = 2.0f * dot (pushEven (x));
            second = pushOdd (x);
        }

        float decimate (float even, float odd) noexcept
        {
            return dot (pushEven (even)) + 0.5f * pushOdd (odd);
        }

    private:
        /** Returns a pointer to the oldest of the last numTaps samples; the
            newer ones follow it in memory. */
        const float* pushEven (float x) noexcept
        {
            evenHistory[(size_t) evenPosition] = x;
            evenHistory[(size_t) (evenPosition + numTaps)] = x;

            const auto* oldest = evenHistory.data() + evenPosition + 1;
            evenPosition = evenPosition + 1 == numTaps ? 0 : evenPosition + 1;
            return oldest;
        }

        float dot (const float* oldest) const noexcept
        {
            const auto* taps = evenTaps.data();
            float y = 0.0f;

            for (int k = 0; k < numTaps; ++k)
                y += taps[k] * oldest[k];

            return y;
        }

        /** Delays by centreDelay samples. */
        float pushOdd (float x) noexcept
        {
            if (centreDelay == 0)
                return x;

            const auto out = oddHistory[(size_t) oddPosition];
            oddHistory[(size_t) oddPosition] = x;
            oddPosition = oddPosition + 1 == centreDelay ? 0 : oddPosition + 1;
            return out;
        }

        static double besselI0 (double x) noexcept
        {
            double sum = 1.0, term = 1.0;

            for (int k = 1; k < 50; ++k)
            {
                term *= (x / (2.0 * k)) * (x / (2.0 * k));
                sum += term;
            }

            return sum;
        }

        std::vector<float> evenTaps;
        std::vector<float> evenHistory, oddHistory;
        int numTaps { 0 };
        int centreDelay { 0 };
        int evenPosition { 0 }, oddPosition { 0 };
    };

    int stages { 0 };
    int maxBlockSize { 1 };
    int latency { 0 };
    int padLength { 0 };
    int padIndex { 0 };

    std::array<std::vector<std::unique_ptr<HalfBand>>, maxChannels> up, down;
    std::array<std::vector<float>, maxChannels> inner, scratch, pad;
};

} // namespace breakwater
