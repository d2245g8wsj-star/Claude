// Framing, interpolation and the NaN-aware helpers the analysis stages share.
// Unvoiced frames are carried as NaN throughout, exactly as in the Python
// reference, so "no pitch here" never gets confused with "pitch zero".
#pragma once

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace vocalai {

inline double nan_value() { return std::numeric_limits<double>::quiet_NaN(); }
inline bool is_finite(double v) { return std::isfinite(v); }

// Row-major frames: `n_frames` rows of `frame_len`, hop `hop`, reflect-padded.
struct Frames {
    std::vector<double> data;
    std::size_t rows = 0;
    std::size_t cols = 0;
    const double* row(std::size_t i) const { return data.data() + i * cols; }
    double* row(std::size_t i) { return data.data() + i * cols; }
};

Frames frame_signal(const std::vector<double>& x, std::size_t frame_len, std::size_t hop,
                    bool center = true);
std::vector<double> reflect_pad(const std::vector<double>& x, std::size_t pad);
std::vector<double> frame_times(std::size_t n_frames, std::size_t hop, int sample_rate);

double hz_to_cents(double hz, double ref = 10.0);
double cents_to_hz(double cents, double ref = 10.0);
double hz_to_midi(double hz, double a4 = 440.0);
double midi_to_hz(double midi, double a4 = 440.0);
std::string note_name(double midi);

// np.interp: linear interpolation with edge clamping. `xp` must be ascending.
double interp(double x, const std::vector<double>& xp, const std::vector<double>& fp);
std::vector<double> interp(const std::vector<double>& x, const std::vector<double>& xp,
                           const std::vector<double>& fp);

// Moving average that ignores NaN. The window is forced odd, because an even
// kernel is off-centre and would delay every curve by half a sample.
std::vector<double> smooth(const std::vector<double>& x, std::size_t width);
std::vector<double> median_filter_nan(const std::vector<double>& x, std::size_t width);

// Bridge NaN runs no longer than `max_gap`; never extrapolate past the edges.
std::vector<double> interpolate_gaps(const std::vector<double>& x, long max_gap = -1);
std::vector<std::pair<std::size_t, std::size_t>> true_runs(const std::vector<bool>& mask);
std::vector<std::pair<std::size_t, std::size_t>> finite_runs(const std::vector<double>& x);

std::vector<double> hann(std::size_t n);
double median(std::vector<double> values);          // ignores NaN
double nan_median(const double* data, std::size_t n);
double percentile(std::vector<double> values, double q);

}  // namespace vocalai
