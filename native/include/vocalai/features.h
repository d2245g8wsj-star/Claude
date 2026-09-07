// Spectral features: onsets, loudness and the descriptors segmentation needs.
#pragma once

#include <cstddef>
#include <vector>

namespace vocalai {

struct Spectrogram {
    std::vector<double> mag;      // row-major, rows x bins
    std::size_t rows = 0;
    std::size_t bins = 0;
    std::vector<double> freqs;
    std::vector<double> times;
    int sample_rate = 44100;
    std::size_t hop = 256;
    std::size_t n_fft = 2048;
    const double* row(std::size_t i) const { return mag.data() + i * bins; }
};

Spectrogram spectrogram(const std::vector<double>& x, int sample_rate, std::size_t n_fft = 2048,
                        std::size_t hop = 256);

std::vector<double> onset_envelope(const Spectrogram& spec, std::size_t lag = 2);
std::vector<double> pick_onsets(const std::vector<double>& env, const std::vector<double>& times,
                                double delta = 0.06, double min_gap = 0.05);
std::vector<double> loudness_envelope(const std::vector<double>& x, int sample_rate,
                                      std::size_t hop = 256, std::size_t frame_len = 1024);

}  // namespace vocalai
