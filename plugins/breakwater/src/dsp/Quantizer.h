#pragma once

#include <cstdint>
#include <cmath>
#include <algorithm>

namespace breakwater
{

enum class WordLength { off = 0, bits24, bits20, bits16, numLengths };
enum class DitherType { off = 0, tpdf, shaped, numTypes };

inline int wordLengthBits (WordLength w) noexcept
{
    switch (w)
    {
        case WordLength::bits24: return 24;
        case WordLength::bits20: return 20;
        case WordLength::bits16: return 16;
        case WordLength::off:
        case WordLength::numLengths:
        default:                 return 0;
    }
}

/** Word length reduction for the delivery format: dither and noise shaping.

    Truncating to a shorter word without dither correlates the error with the
    signal, which is what low level digital grain actually is. TPDF dither
    decorrelates it at the cost of a flat noise floor; the shaped option pushes
    that noise up towards Nyquist where there is less to hear, using a second
    order error feedback filter.

    This sits at the very end of the chain, after the ceiling and the monitor
    gain, because nothing may change the samples once they are quantised.
*/
class Quantizer
{
public:
    void reset() noexcept
    {
        error1 = error2 = 0.0f;
        rngState = 0x9E3779B9u ^ (seed * 2654435761u);
    }

    void setSeed (uint32_t s) noexcept { seed = s + 1u; reset(); }

    void setParameters (WordLength w, DitherType d) noexcept
    {
        const auto bits = wordLengthBits (w);

        active = bits > 0;

        if (active)
        {
            // Full scale is +/-1.0, so the LSB step is 2 / 2^bits.
            step    = 2.0f / static_cast<float> (1u << (bits - 1));
            invStep = 1.0f / step;
        }

        dither = d;
    }

    float process (float x) noexcept
    {
        if (! active)
            return x;

        auto in = x;

        if (dither == DitherType::shaped)
            in += 1.5f * error1 - 0.5f * error2;   // second order error feedback

        auto toQuantise = in;

        if (dither != DitherType::off)
            toQuantise += step * 0.5f * (nextUniform() + nextUniform());   // TPDF, 2 LSB p-p

        const auto quantised = std::round (toQuantise * invStep) * step;

        if (dither == DitherType::shaped)
        {
            error2 = error1;
            error1 = quantised - in;
        }

        return std::clamp (quantised, -1.0f, 1.0f);
    }

private:
    /** Uniform in [-1, 1). xorshift32 - cheap, no allocation, no locks. */
    float nextUniform() noexcept
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;

        return static_cast<float> (static_cast<int32_t> (rngState)) * (1.0f / 2147483648.0f);
    }

    bool       active   { false };
    DitherType dither   { DitherType::tpdf };
    float      step     { 1.0f };
    float      invStep  { 1.0f };
    float      error1   { 0.0f };
    float      error2   { 0.0f };
    uint32_t   seed     { 1u };
    uint32_t   rngState { 0x9E3779B9u };
};

} // namespace breakwater
