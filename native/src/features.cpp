#include "vocalai/features.h"

#include <algorithm>
#include <cmath>

#include "vocalai/dsp.h"
#include "vocalai/fft.h"
#include "vocalai/filters.h"

namespace vocalai {

Spectrogram spectrogram(const std::vector<double>& x, int sample_rate, std::size_t n_fft,
                        std::size_t hop) {
    const Frames frames = frame_signal(x, n_fft, hop, true);
    // Periodic Hann, matching np.hanning(n + 1)[:-1].
    std::vector<double> win(n_fft);
    for (std::size_t i = 0; i < n_fft; ++i)
        win[i] = 0.5 - 0.5 * std::cos(2.0 * M_PI * static_cast<double>(i) / static_cast<double>(n_fft));

    Spectrogram spec;
    spec.rows = frames.rows;
    spec.bins = n_fft / 2 + 1;
    spec.sample_rate = sample_rate;
    spec.hop = hop;
    spec.n_fft = n_fft;
    spec.mag.resize(spec.rows * spec.bins);
    spec.freqs.resize(spec.bins);
    for (std::size_t k = 0; k < spec.bins; ++k)
        spec.freqs[k] = static_cast<double>(k) * sample_rate / static_cast<double>(n_fft);
    spec.times = frame_times(spec.rows, hop, sample_rate);

    std::vector<double> buf(n_fft);
    for (std::size_t r = 0; r < frames.rows; ++r) {
        const double* row = frames.row(r);
        for (std::size_t i = 0; i < n_fft; ++i) buf[i] = row[i] * win[i];
        const auto s = rfft(buf.data(), n_fft, n_fft);
        for (std::size_t k = 0; k < spec.bins; ++k) spec.mag[r * spec.bins + k] = std::abs(s[k]);
    }
    return spec;
}

std::vector<double> onset_envelope(const Spectrogram& spec, std::size_t lag) {
    const std::size_t rows = spec.rows, bins = spec.bins;
    std::vector<double> flux(rows, 0.0);
    if (rows <= lag) return flux;

    // Half-wave-rectified spectral flux on a log-compressed magnitude.
    for (std::size_t r = lag; r < rows; ++r) {
        double acc = 0.0;
        for (std::size_t k = 0; k < bins; ++k) {
            const double now = std::log1p(spec.mag[r * bins + k] * 100.0);
            const double before = std::log1p(spec.mag[(r - lag) * bins + k] * 100.0);
            const double d = now - before;
            if (d > 0.0) acc += d;
        }
        flux[r] = acc;
    }

    // Subtract a local mean so the envelope is comparable across dynamics.
    const std::size_t win = 21, half = win / 2;
    std::vector<double> out(rows, 0.0);
    for (std::size_t r = 0; r < rows; ++r) {
        double sum = 0.0;
        for (std::size_t j = 0; j < win; ++j) {
            const long idx = static_cast<long>(r) + static_cast<long>(j) - static_cast<long>(half);
            if (idx >= 0 && idx < static_cast<long>(rows)) sum += flux[static_cast<std::size_t>(idx)];
        }
        out[r] = std::max(flux[r] - sum / static_cast<double>(win), 0.0);
    }
    const double peak = out.empty() ? 0.0 : *std::max_element(out.begin(), out.end());
    if (peak > 0.0)
        for (double& v : out) v /= peak;
    return out;
}

std::vector<double> pick_onsets(const std::vector<double>& env, const std::vector<double>& times,
                                double delta, double min_gap) {
    std::vector<std::size_t> peaks;
    for (std::size_t i = 1; i + 1 < env.size(); ++i)
        if (env[i] > env[i - 1] && env[i] >= env[i + 1] && env[i] > delta) peaks.push_back(i);
    if (peaks.empty()) return {};

    std::vector<std::size_t> keep{peaks.front()};
    for (std::size_t i = 1; i < peaks.size(); ++i) {
        const std::size_t p = peaks[i];
        if (times[p] - times[keep.back()] >= min_gap) keep.push_back(p);
        else if (env[p] > env[keep.back()]) keep.back() = p;
    }
    std::vector<double> out;
    out.reserve(keep.size());
    for (std::size_t p : keep) out.push_back(times[p]);
    return out;
}

std::vector<double> loudness_envelope(const std::vector<double>& x, int sample_rate,
                                      std::size_t hop, std::size_t frame_len) {
    const Sos sos = butter_highpass(2, 100.0 / (sample_rate / 2.0));
    const std::vector<double> weighted = sosfilt(sos, x);
    const Frames frames = frame_signal(weighted, frame_len, hop, true);
    std::vector<double> out(frames.rows);
    for (std::size_t r = 0; r < frames.rows; ++r) {
        const double* row = frames.row(r);
        double acc = 0.0;
        for (std::size_t i = 0; i < frames.cols; ++i) acc += row[i] * row[i];
        const double rms = std::sqrt(acc / static_cast<double>(frames.cols) + 1e-20);
        out[r] = 20.0 * std::log10(std::max(rms, 1e-10));
    }
    return out;
}

}  // namespace vocalai
