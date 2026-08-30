#pragma once

#include <array>
#include <cmath>
#include <algorithm>

namespace crest
{

/** Loudness to ITU-R BS.1770-4.

    The coefficients are derived from the analogue prototypes rather than
    copied from the table in the specification, because that table is only
    valid at 48 kHz and a mastering plugin is asked to run at 96 and 192 as
    well. Evaluated at 48 kHz these reproduce the published values.

    Measurement follows the specification's structure: energy is accumulated
    over 100 ms sub-blocks, momentary loudness is the mean of the last four of
    them (400 ms) and short term the last thirty (3 s). Integrated loudness is
    gated in the two stages the specification requires, using a histogram of
    block loudnesses so that no allocation is needed however long the meter
    runs.
*/
class LoudnessMeter
{
public:
    void prepare (double newSampleRate, int channels) noexcept
    {
        sampleRate  = newSampleRate;
        numChannels = std::clamp (channels, 1, 2);

        designHighShelf (1681.974450955533, 3.999843853973347, 0.7071752369554196, shelf);
        designHighPass  (  38.135470876020,                    0.5003270373238773, highPass);

        subBlockLength = std::max (1, (int) std::lround (newSampleRate * 0.1));

        reset();
    }

    void reset() noexcept
    {
        for (auto& channel : state)
            channel = { };

        history.fill (0.0);
        historyCount = 0;
        historyIndex = 0;

        accumulator    = 0.0;
        accumulated    = 0;
        histogram.fill (0);
    }

    void process (const float* const* channelData, int channels, int numSamples) noexcept
    {
        const auto used = std::min (channels, numChannels);

        for (int n = 0; n < numSamples; ++n)
        {
            double sum = 0.0;

            for (int ch = 0; ch < used; ++ch)
            {
                // Both weighting channels have unity gain in BS.1770 for a
                // stereo programme, so no per-channel weight is applied.
                const auto weighted = filter (state[(size_t) ch], (double) channelData[ch][n]);
                sum += weighted * weighted;
            }

            accumulator += sum;

            if (++accumulated >= subBlockLength)
            {
                pushSubBlock (accumulator / (double) subBlockLength);
                accumulator = 0.0;
                accumulated = 0;
            }
        }
    }

    /** All three in LUFS. Silence reads as -infinity, reported as -100. */
    float momentary() const noexcept { return loudnessOf (meanOverLast (4)); }
    float shortTerm() const noexcept { return loudnessOf (meanOverLast (30)); }

    float integrated() const noexcept
    {
        // Stage one: everything above the absolute gate.
        const auto firstPass = gatedMean (-70.0);

        if (firstPass <= -100.0)
            return -100.0f;

        // Stage two: everything above ten units below that.
        return (float) gatedMean (firstPass - 10.0);
    }

private:
    struct Biquad { double b0, b1, b2, a1, a2; };
    struct FilterState { double x1 {}, x2 {}, y1 {}, y2 {}, z1 {}, z2 {}, w1 {}, w2 {}; };

    double filter (FilterState& s, double x) const noexcept
    {
        const auto stage1 = shelf.b0 * x + shelf.b1 * s.x1 + shelf.b2 * s.x2
                            - shelf.a1 * s.y1 - shelf.a2 * s.y2;

        s.x2 = s.x1; s.x1 = x;
        s.y2 = s.y1; s.y1 = stage1;

        const auto stage2 = highPass.b0 * stage1 + highPass.b1 * s.z1 + highPass.b2 * s.z2
                            - highPass.a1 * s.w1 - highPass.a2 * s.w2;

        s.z2 = s.z1; s.z1 = stage1;
        s.w2 = s.w1; s.w1 = stage2;

        return stage2;
    }

    void designHighShelf (double frequency, double gainDb, double q, Biquad& target) const noexcept
    {
        const auto a  = std::pow (10.0, gainDb / 40.0);
        const auto w  = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
        const auto cw = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * q);
        const auto beta  = 2.0 * std::sqrt (a) * alpha;

        const auto a0 = (a + 1.0) - (a - 1.0) * cw + beta;

        target.b0 =        a * ((a + 1.0) + (a - 1.0) * cw + beta) / a0;
        target.b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cw)        / a0;
        target.b2 =        a * ((a + 1.0) + (a - 1.0) * cw - beta) / a0;
        target.a1 =  2.0 *     ((a - 1.0) - (a + 1.0) * cw)        / a0;
        target.a2 =            ((a + 1.0) - (a - 1.0) * cw - beta) / a0;
    }

    void designHighPass (double frequency, double q, Biquad& target) const noexcept
    {
        const auto w  = 2.0 * 3.14159265358979323846 * frequency / sampleRate;
        const auto cw = std::cos (w);
        const auto alpha = std::sin (w) / (2.0 * q);

        const auto a0 = 1.0 + alpha;

        target.b0 =  (1.0 + cw) * 0.5 / a0;
        target.b1 = -(1.0 + cw)       / a0;
        target.b2 =  (1.0 + cw) * 0.5 / a0;
        target.a1 = -2.0 * cw         / a0;
        target.a2 =  (1.0 - alpha)    / a0;
    }

    void pushSubBlock (double meanSquare) noexcept
    {
        history[(size_t) historyIndex] = meanSquare;
        historyIndex = (historyIndex + 1) % (int) history.size();
        historyCount = std::min (historyCount + 1, (int) history.size());

        // One histogram entry per 100 ms, holding that moment's 400 ms value,
        // which is what the specification integrates over.
        const auto loudness = loudnessOf (meanOverLast (4));

        if (loudness > -70.0f && loudness < 30.0f)
            ++histogram[(size_t) std::lround ((loudness + 70.0f) * 10.0f)];
    }

    double meanOverLast (int blocks) const noexcept
    {
        const auto count = std::min (blocks, historyCount);

        if (count <= 0)
            return 0.0;

        double sum = 0.0;

        for (int i = 0; i < count; ++i)
        {
            const auto index = (historyIndex - 1 - i + 2 * (int) history.size()) % (int) history.size();
            sum += history[(size_t) index];
        }

        return sum / (double) count;
    }

    static float loudnessOf (double meanSquare) noexcept
    {
        if (meanSquare <= 1.0e-12)
            return -100.0f;

        return (float) (-0.691 + 10.0 * std::log10 (meanSquare));
    }

    /** Mean loudness of every recorded block above a threshold, in LUFS. */
    double gatedMean (double thresholdDb) const noexcept
    {
        double energy = 0.0;
        double count  = 0.0;

        for (size_t bin = 0; bin < histogram.size(); ++bin)
        {
            if (histogram[bin] == 0)
                continue;

            const auto loudness = -70.0 + (double) bin * 0.1;

            if (loudness <= thresholdDb)
                continue;

            energy += std::pow (10.0, (loudness + 0.691) / 10.0) * (double) histogram[bin];
            count  += (double) histogram[bin];
        }

        if (count <= 0.0)
            return -100.0;

        return -0.691 + 10.0 * std::log10 (energy / count);
    }

    double sampleRate  { 48000.0 };
    int    numChannels { 2 };

    Biquad shelf { }, highPass { };
    std::array<FilterState, 2> state { };

    int    subBlockLength { 4800 };
    double accumulator    { 0.0 };
    int    accumulated    { 0 };

    std::array<double, 30> history { };
    int historyCount { 0 };
    int historyIndex { 0 };

    std::array<int, 1001> histogram { };
};

} // namespace crest
