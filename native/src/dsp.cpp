#include "vocalai/dsp.h"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <numeric>

namespace vocalai {

std::vector<double> reflect_pad(const std::vector<double>& x, std::size_t pad) {
    const std::size_t n = x.size();
    std::vector<double> out(n + 2 * pad);
    if (n == 0) return out;
    for (std::size_t i = 0; i < pad; ++i) {
        // numpy's "reflect" mirrors without repeating the edge sample.
        const std::size_t src = (n > 1) ? std::min(pad - i, n - 1) : 0;
        out[i] = x[src];
    }
    std::copy(x.begin(), x.end(), out.begin() + pad);
    for (std::size_t i = 0; i < pad; ++i) {
        const std::size_t back = (n > 1) ? std::min(i + 2, n) : 1;
        out[n + pad + i] = x[n - back];
    }
    return out;
}

Frames frame_signal(const std::vector<double>& x, std::size_t frame_len, std::size_t hop,
                    bool center) {
    std::vector<double> padded = center ? reflect_pad(x, frame_len / 2) : x;
    if (padded.size() < frame_len) padded.resize(frame_len, 0.0);
    Frames f;
    f.cols = frame_len;
    f.rows = 1 + (padded.size() - frame_len) / hop;
    f.data.resize(f.rows * f.cols);
    for (std::size_t i = 0; i < f.rows; ++i)
        std::copy(padded.begin() + i * hop, padded.begin() + i * hop + frame_len,
                  f.data.begin() + i * f.cols);
    return f;
}

std::vector<double> frame_times(std::size_t n_frames, std::size_t hop, int sample_rate) {
    std::vector<double> t(n_frames);
    for (std::size_t i = 0; i < n_frames; ++i)
        t[i] = static_cast<double>(i * hop) / sample_rate;
    return t;
}

double hz_to_cents(double hz, double ref) {
    return hz > 0.0 ? 1200.0 * std::log2(hz / ref) : nan_value();
}
double cents_to_hz(double cents, double ref) { return ref * std::pow(2.0, cents / 1200.0); }
double hz_to_midi(double hz, double a4) {
    return 69.0 + 12.0 * std::log2(std::max(hz, 1e-9) / a4);
}
double midi_to_hz(double midi, double a4) { return a4 * std::pow(2.0, (midi - 69.0) / 12.0); }

std::string note_name(double midi) {
    static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    // Clamp to the MIDI range before naming: a NaN or a wild value must not
    // become a nonsense octave number.
    const double safe = std::isfinite(midi) ? std::min(std::max(midi, -12.0), 200.0) : 0.0;
    const long m = std::lround(safe);
    const long pc = ((m % 12) + 12) % 12;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s%ld", names[pc], m / 12 - 1);
    return std::string(buf);
}

double interp(double x, const std::vector<double>& xp, const std::vector<double>& fp) {
    if (xp.empty()) return nan_value();
    if (x <= xp.front()) return fp.front();
    if (x >= xp.back()) return fp.back();
    const auto it = std::upper_bound(xp.begin(), xp.end(), x);
    const std::size_t hi = static_cast<std::size_t>(it - xp.begin());
    const std::size_t lo = hi - 1;
    const double span = xp[hi] - xp[lo];
    if (span <= 0.0) return fp[lo];
    const double frac = (x - xp[lo]) / span;
    return fp[lo] + frac * (fp[hi] - fp[lo]);
}

std::vector<double> interp(const std::vector<double>& x, const std::vector<double>& xp,
                           const std::vector<double>& fp) {
    std::vector<double> out(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) out[i] = interp(x[i], xp, fp);
    return out;
}

std::vector<double> smooth(const std::vector<double>& x, std::size_t width) {
    if (width <= 1) return x;
    width |= 1;  // an even kernel would shift the result by half a sample
    const std::size_t half = width / 2;
    const std::size_t n = x.size();
    std::vector<double> out(n, nan_value());
    for (std::size_t i = 0; i < n; ++i) {
        if (!is_finite(x[i])) continue;
        double sum = 0.0;
        std::size_t count = 0;
        const std::size_t lo = i > half ? i - half : 0;
        const std::size_t hi = std::min(n, i + half + 1);
        for (std::size_t j = lo; j < hi; ++j) {
            if (is_finite(x[j])) {
                sum += x[j];
                ++count;
            }
        }
        out[i] = count ? sum / static_cast<double>(count) : nan_value();
    }
    return out;
}

double nan_median(const double* data, std::size_t n) {
    std::vector<double> vals;
    vals.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        if (std::isfinite(data[i])) vals.push_back(data[i]);
    if (vals.empty()) return nan_value();
    const std::size_t mid = vals.size() / 2;
    std::nth_element(vals.begin(), vals.begin() + mid, vals.end());
    const double upper = vals[mid];
    if (vals.size() % 2) return upper;
    const double lower = *std::max_element(vals.begin(), vals.begin() + mid);
    return 0.5 * (lower + upper);
}

double median(std::vector<double> values) { return nan_median(values.data(), values.size()); }

double percentile(std::vector<double> values, double q) {
    std::vector<double> vals;
    for (double v : values)
        if (std::isfinite(v)) vals.push_back(v);
    if (vals.empty()) return nan_value();
    std::sort(vals.begin(), vals.end());
    const double pos = q / 100.0 * static_cast<double>(vals.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
    return vals[lo] + (pos - lo) * (vals[hi] - vals[lo]);
}

std::vector<double> median_filter_nan(const std::vector<double>& x, std::size_t width) {
    if (width <= 1 || x.empty()) return x;
    width |= 1;
    const std::size_t half = width / 2;
    const std::size_t n = x.size();
    std::vector<double> out(n, nan_value());
    std::vector<double> window(width);
    for (std::size_t i = 0; i < n; ++i) {
        if (!is_finite(x[i])) continue;
        for (std::size_t k = 0; k < width; ++k) {
            // Edge padding repeats the boundary sample, matching np.pad("edge").
            const long idx = static_cast<long>(i) + static_cast<long>(k) - static_cast<long>(half);
            const std::size_t clamped =
                static_cast<std::size_t>(std::max<long>(0, std::min<long>(idx, n - 1)));
            window[k] = x[clamped];
        }
        out[i] = nan_median(window.data(), window.size());
    }
    return out;
}

std::vector<std::pair<std::size_t, std::size_t>> true_runs(const std::vector<bool>& mask) {
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    std::size_t i = 0;
    while (i < mask.size()) {
        if (!mask[i]) {
            ++i;
            continue;
        }
        const std::size_t start = i;
        while (i < mask.size() && mask[i]) ++i;
        runs.emplace_back(start, i);
    }
    return runs;
}

std::vector<std::pair<std::size_t, std::size_t>> finite_runs(const std::vector<double>& x) {
    std::vector<bool> mask(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) mask[i] = is_finite(x[i]);
    return true_runs(mask);
}

std::vector<double> interpolate_gaps(const std::vector<double>& x, long max_gap) {
    std::vector<double> out = x;
    const std::size_t n = x.size();
    std::vector<std::size_t> known;
    for (std::size_t i = 0; i < n; ++i)
        if (is_finite(x[i])) known.push_back(i);
    if (known.size() < 2) return out;

    for (std::size_t k = 0; k + 1 < known.size(); ++k) {
        const std::size_t a = known[k];
        const std::size_t b = known[k + 1];
        if (b == a + 1) continue;
        const long gap = static_cast<long>(b - a - 1);
        if (max_gap >= 0 && gap > max_gap) continue;
        for (std::size_t i = a + 1; i < b; ++i) {
            const double frac = static_cast<double>(i - a) / static_cast<double>(b - a);
            out[i] = x[a] + frac * (x[b] - x[a]);
        }
    }
    return out;
}

std::vector<double> hann(std::size_t n) {
    std::vector<double> w(n);
    if (n == 1) {
        w[0] = 1.0;
        return w;
    }
    for (std::size_t i = 0; i < n; ++i)
        w[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / (n - 1));
    return w;
}

}  // namespace vocalai
