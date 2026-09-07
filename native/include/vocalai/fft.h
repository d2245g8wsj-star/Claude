// Radix-2 FFT. Every transform size in this engine is a power of two, so a
// plain iterative Cooley-Tukey kernel with cached twiddles is enough - and it
// keeps the core free of third-party dependencies.
#pragma once

#include <complex>
#include <cstddef>
#include <vector>

namespace vocalai {

using cdouble = std::complex<double>;

// In-place complex FFT. `n` must be a power of two.
void fft(std::vector<cdouble>& data, bool inverse = false);

// Real-input forward transform. Returns n/2 + 1 bins.
std::vector<cdouble> rfft(const double* x, std::size_t n, std::size_t fft_size);
std::vector<cdouble> rfft(const std::vector<double>& x, std::size_t fft_size = 0);

// Inverse of `rfft`. `n` is the length of the real output signal.
std::vector<double> irfft(const std::vector<cdouble>& spectrum, std::size_t n);

std::size_t next_pow2(std::size_t n);

}  // namespace vocalai
