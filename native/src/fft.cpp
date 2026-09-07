#include "vocalai/fft.h"

#include <cmath>
#include <mutex>
#include <unordered_map>

namespace vocalai {
namespace {

// Bit-reversal permutation and twiddle tables, cached per transform size.
struct Plan {
    std::vector<std::size_t> reverse;
    std::vector<cdouble> twiddles;  // forward twiddles, stage-major
};

const Plan& plan_for(std::size_t n) {
    static std::mutex mutex;
    static std::unordered_map<std::size_t, Plan> cache;
    std::lock_guard<std::mutex> lock(mutex);
    auto it = cache.find(n);
    if (it != cache.end()) return it->second;

    Plan plan;
    plan.reverse.resize(n);
    std::size_t bits = 0;
    while ((std::size_t{1} << bits) < n) ++bits;
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t r = 0;
        for (std::size_t b = 0; b < bits; ++b)
            if (i & (std::size_t{1} << b)) r |= std::size_t{1} << (bits - 1 - b);
        plan.reverse[i] = r;
    }
    plan.twiddles.resize(n / 2 ? n / 2 : 1);
    for (std::size_t k = 0; k < n / 2; ++k) {
        const double angle = -2.0 * M_PI * static_cast<double>(k) / static_cast<double>(n);
        plan.twiddles[k] = cdouble(std::cos(angle), std::sin(angle));
    }
    return cache.emplace(n, std::move(plan)).first->second;
}

}  // namespace

std::size_t next_pow2(std::size_t n) {
    std::size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

namespace {

bool is_pow2(std::size_t n) { return n && (n & (n - 1)) == 0; }

// Bluestein's algorithm: an arbitrary-length DFT expressed as a convolution,
// which a power-of-two transform can carry out.  numpy accepts any length, so
// anything ported from it must too - a radix-2-only kernel silently walks off
// the end of its buffers when handed, say, a 97-frame note core.
void fft_bluestein(std::vector<cdouble>& data, bool inverse) {
    const std::size_t n = data.size();
    const std::size_t m = next_pow2(2 * n - 1);
    const double sign = inverse ? 1.0 : -1.0;

    std::vector<cdouble> chirp(n);
    for (std::size_t k = 0; k < n; ++k) {
        // Reduce k^2 modulo 2n before scaling, so the angle stays accurate.
        const std::size_t kk = (k % (2 * n)) * (k % (2 * n)) % (2 * n);
        const double angle = sign * M_PI * static_cast<double>(kk) / static_cast<double>(n);
        chirp[k] = cdouble(std::cos(angle), std::sin(angle));
    }

    std::vector<cdouble> a(m, cdouble(0.0, 0.0));
    std::vector<cdouble> b(m, cdouble(0.0, 0.0));
    for (std::size_t k = 0; k < n; ++k) {
        a[k] = data[k] * chirp[k];
        b[k] = std::conj(chirp[k]);
        if (k) b[m - k] = std::conj(chirp[k]);
    }
    fft(a, false);
    fft(b, false);
    for (std::size_t k = 0; k < m; ++k) a[k] *= b[k];
    fft(a, true);

    for (std::size_t k = 0; k < n; ++k) data[k] = a[k] * chirp[k];
    if (inverse) {
        const double scale = 1.0 / static_cast<double>(n);
        for (auto& v : data) v *= scale;
    }
}

}  // namespace

void fft(std::vector<cdouble>& data, bool inverse) {
    const std::size_t n = data.size();
    if (n < 2) return;
    if (!is_pow2(n)) {
        fft_bluestein(data, inverse);
        return;
    }
    const Plan& plan = plan_for(n);

    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t j = plan.reverse[i];
        if (i < j) std::swap(data[i], data[j]);
    }

    for (std::size_t len = 2; len <= n; len <<= 1) {
        const std::size_t half = len / 2;
        const std::size_t stride = n / len;
        for (std::size_t start = 0; start < n; start += len) {
            for (std::size_t k = 0; k < half; ++k) {
                cdouble w = plan.twiddles[k * stride];
                if (inverse) w = std::conj(w);
                const cdouble even = data[start + k];
                const cdouble odd = data[start + k + half] * w;
                data[start + k] = even + odd;
                data[start + k + half] = even - odd;
            }
        }
    }
    if (inverse) {
        const double scale = 1.0 / static_cast<double>(n);
        for (auto& v : data) v *= scale;
    }
}

// A real-input transform of length N is computed as a complex transform of
// length N/2: pack even samples into the real part and odd into the imaginary,
// transform, then untangle.  That halves the work, which matters because the
// difference function runs three transforms per analysis frame.
std::vector<cdouble> rfft(const double* x, std::size_t n, std::size_t fft_size) {
    const std::size_t size = fft_size ? fft_size : next_pow2(n);
    if (size < 2) {
        std::vector<cdouble> out(1, cdouble(size && n ? x[0] : 0.0, 0.0));
        return out;
    }
    if (size % 2 != 0) {
        // The even/odd packing needs an even length; odd sizes go the long way.
        std::vector<cdouble> buf(size, cdouble(0.0, 0.0));
        for (std::size_t i = 0; i < n && i < size; ++i) buf[i] = cdouble(x[i], 0.0);
        fft(buf, false);
        buf.resize(size / 2 + 1);
        return buf;
    }
    const std::size_t half = size / 2;

    std::vector<cdouble> z(half);
    for (std::size_t k = 0; k < half; ++k) {
        const std::size_t even = 2 * k;
        const std::size_t odd = even + 1;
        z[k] = cdouble(even < n ? x[even] : 0.0, odd < n ? x[odd] : 0.0);
    }
    fft(z, false);

    std::vector<cdouble> out(half + 1);
    for (std::size_t k = 0; k <= half; ++k) {
        const std::size_t kk = k % half;
        const cdouble a = z[kk];
        const cdouble b = std::conj(z[(half - kk) % half]);
        const cdouble fe = 0.5 * (a + b);
        const cdouble fo = cdouble(0.0, -0.5) * (a - b);
        if (k == half) {
            out[k] = fe - fo;  // the Nyquist bin reuses the k = 0 split
        } else {
            const double angle = -2.0 * M_PI * static_cast<double>(k) / static_cast<double>(size);
            out[k] = fe + cdouble(std::cos(angle), std::sin(angle)) * fo;
        }
    }
    return out;
}

std::vector<cdouble> rfft(const std::vector<double>& x, std::size_t fft_size) {
    return rfft(x.data(), x.size(), fft_size ? fft_size : next_pow2(x.size()));
}

std::vector<double> irfft(const std::vector<cdouble>& spectrum, std::size_t n) {
    if (n < 2) {
        return std::vector<double>(n, spectrum.empty() ? 0.0 : spectrum[0].real());
    }
    if (n % 2 != 0) {
        std::vector<cdouble> buf(n, cdouble(0.0, 0.0));
        for (std::size_t k = 0; k < spectrum.size() && k < n; ++k) buf[k] = spectrum[k];
        for (std::size_t k = 1; k < n - k; ++k)
            if (k < spectrum.size()) buf[n - k] = std::conj(spectrum[k]);
        fft(buf, true);
        std::vector<double> out(n);
        for (std::size_t i = 0; i < n; ++i) out[i] = buf[i].real();
        return out;
    }
    const std::size_t half = n / 2;
    auto bin = [&](std::size_t k) {
        return k < spectrum.size() ? spectrum[k] : cdouble(0.0, 0.0);
    };

    std::vector<cdouble> z(half);
    for (std::size_t k = 0; k < half; ++k) {
        const cdouble a = bin(k);
        const cdouble b = std::conj(bin(half - k));
        const cdouble fe = 0.5 * (a + b);
        const double angle = 2.0 * M_PI * static_cast<double>(k) / static_cast<double>(n);
        const cdouble fo = 0.5 * (a - b) * cdouble(std::cos(angle), std::sin(angle));
        z[k] = fe + cdouble(0.0, 1.0) * fo;
    }
    fft(z, true);

    std::vector<double> out(n);
    for (std::size_t k = 0; k < half; ++k) {
        out[2 * k] = z[k].real();
        out[2 * k + 1] = z[k].imag();
    }
    return out;
}

}  // namespace vocalai
