#include "vocalai/pitch.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include "vocalai/fft.h"

namespace vocalai {
namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// The pYIN threshold grid and its Beta(2, 18) prior: mass concentrated near
// YIN's classic threshold of 0.1.
const std::vector<double>& thresholds() {
    static const std::vector<double> t = [] {
        std::vector<double> v;
        for (int i = 1; i <= 99; ++i) v.push_back(i * 0.01);
        return v;
    }();
    return t;
}

// Cumulative prior mass, with a leading zero so that cdf(v) is a lookup.
const std::vector<double>& prior_cdf() {
    static const std::vector<double> cdf = [] {
        const auto& t = thresholds();
        std::vector<double> w(t.size());
        double total = 0.0;
        for (std::size_t i = 0; i < t.size(); ++i) {
            w[i] = std::pow(t[i], 1.0) * std::pow(1.0 - t[i], 17.0);  // Beta(2, 18)
            total += w[i];
        }
        std::vector<double> c(t.size() + 1, 0.0);
        for (std::size_t i = 0; i < w.size(); ++i) c[i + 1] = c[i] + w[i] / total;
        return c;
    }();
    return cdf;
}

double cdf_at(double v) {
    const auto& t = thresholds();
    const std::size_t count =
        static_cast<std::size_t>(std::upper_bound(t.begin(), t.end(), v) - t.begin());
    return prior_cdf()[count];
}

// Sub-sample refinement of a CMNDF minimum by parabolic interpolation.
double refine(const std::vector<double>& row, std::size_t k) {
    if (k == 0 || k + 1 >= row.size()) return static_cast<double>(k);
    const double a = row[k - 1], b = row[k], c = row[k + 1];
    const double denom = a - 2.0 * b + c;
    if (std::abs(denom) < 1e-12) return static_cast<double>(k);
    double shift = 0.5 * (a - c) / denom;
    shift = std::min(std::max(shift, -0.5), 0.5);
    return static_cast<double>(k) + shift;
}

struct FrameObservation {
    std::vector<double> candidate_hz;
    std::vector<double> candidate_prob;
    double voiced_prob = 0.0;
    double periodicity = 0.0;
};

// pYIN's candidate probabilities in closed form.  As the threshold sweeps
// upward the trough it selects is always a running record minimum, so each
// record trough collects the prior mass of the interval that picks it - which
// replaces the loop over thresholds entirely.
FrameObservation observe(const std::vector<double>& row, int sample_rate, double fmin,
                         double fmax, double no_trough_boost = 0.05) {
    FrameObservation out;
    const std::size_t max_lag = row.size();
    const std::size_t lag_lo = std::max<std::size_t>(
        static_cast<std::size_t>(std::floor(sample_rate / fmax)), 1);
    const std::size_t lag_hi =
        std::min<std::size_t>(static_cast<std::size_t>(std::ceil(sample_rate / fmin)),
                              max_lag >= 2 ? max_lag - 2 : 0);
    if (lag_hi <= lag_lo) return out;

    std::vector<std::size_t> troughs;
    for (std::size_t i = 1; i + 1 < max_lag; ++i)
        if (row[i] < row[i - 1] && row[i] <= row[i + 1]) troughs.push_back(i);

    std::vector<double> vals, freqs;
    for (std::size_t k : troughs) {
        if (k < lag_lo || k > lag_hi) continue;
        const double lag = refine(row, k);
        if (lag <= 0.0) continue;
        const double hz = sample_rate / lag;
        if (hz < fmin || hz > fmax) continue;
        vals.push_back(row[k]);
        freqs.push_back(hz);
    }
    if (vals.empty()) return out;

    std::vector<double> probs(vals.size(), 0.0);
    double prefix_min = kInf;
    for (std::size_t i = 0; i < vals.size(); ++i) {
        if (vals[i] < prefix_min) {
            const double upper = std::isfinite(prefix_min) ? prefix_min : thresholds().back() + 1.0;
            probs[i] = std::max(cdf_at(upper) - cdf_at(vals[i]), 0.0);
            prefix_min = vals[i];
        }
    }
    const std::size_t best =
        static_cast<std::size_t>(std::min_element(vals.begin(), vals.end()) - vals.begin());
    // Thresholds too strict for any trough leave the frame nearly unvoiced.
    probs[best] += cdf_at(vals[best]) * no_trough_boost;

    double total = 0.0;
    for (double p : probs) total += p;
    if (total <= 0.0) return out;

    out.candidate_hz = freqs;
    out.candidate_prob = probs;
    out.voiced_prob = std::min(total, 1.0);
    out.periodicity = 1.0 - vals[best];
    return out;
}

// Decode the 2N-state (voiced/unvoiced x pitch bin) HMM.  Returns the chosen
// bin per frame, negative where unvoiced.
std::vector<int> viterbi(const std::vector<float>& obs, std::size_t n_frames,
                         std::size_t n_bins, const std::vector<double>& voiced_prob,
                         int max_step_bins, double switch_prob, double step_sigma_bins) {
    const int k = std::max(max_step_bins, 1);
    const std::size_t width = static_cast<std::size_t>(2 * k + 1);
    std::vector<double> move(width);
    double acc = 0.0;
    for (std::size_t m = 0; m < width; ++m) {
        const double off = static_cast<double>(static_cast<int>(m) - k);
        move[m] = -0.5 * (off / std::max(step_sigma_bins, 1e-6)) * (off / std::max(step_sigma_bins, 1e-6));
        acc += std::exp(move[m]);
    }
    const double log_norm = std::log(acc);
    for (double& m : move) m -= log_norm;

    const double log_stay = std::log(1.0 - switch_prob);
    const double log_leave = std::log(switch_prob);
    const double eps = 1e-12;

    std::vector<double> score_v(n_bins), score_u(n_bins);
    std::vector<double> next_v(n_bins), next_u(n_bins);
    std::vector<int16_t> bp(n_frames * 2 * n_bins, -1);

    auto obs_v = [&](std::size_t t, std::size_t b) {
        return std::log(std::max<double>(obs[t * n_bins + b], eps));
    };
    auto obs_u = [&](std::size_t t) {
        return std::log(std::max(1.0 - voiced_prob[t], eps) / static_cast<double>(n_bins));
    };

    const double init = std::log(0.5 / static_cast<double>(n_bins));
    for (std::size_t b = 0; b < n_bins; ++b) {
        score_v[b] = obs_v(0, b) + init;
        score_u[b] = obs_u(0) + init;
    }

    for (std::size_t t = 1; t < n_frames; ++t) {
        const double ou = obs_u(t);
        for (std::size_t j = 0; j < n_bins; ++j) {
            double best_v = -kInf, best_u = -kInf;
            int src_v = 0, src_u = 0;
            for (std::size_t m = 0; m < width; ++m) {
                const long idx = static_cast<long>(j) + static_cast<long>(m) - k;
                if (idx < 0 || idx >= static_cast<long>(n_bins)) continue;
                const double gv = score_v[idx] + move[m];
                if (gv > best_v) {
                    best_v = gv;
                    src_v = static_cast<int>(idx);
                }
                const double gu = score_u[idx] + move[m];
                if (gu > best_u) {
                    best_u = gu;
                    src_u = static_cast<int>(idx);
                }
            }
            // Voicing switches multiply in on top of the pitch movement.
            const double to_v_from_v = best_v + log_stay;
            const double to_v_from_u = best_u + log_leave;
            const double to_u_from_v = best_v + log_leave;
            const double to_u_from_u = best_u + log_stay;

            const bool v_from_v = to_v_from_v >= to_v_from_u;
            const bool u_from_v = to_u_from_v >= to_u_from_u;
            next_v[j] = (v_from_v ? to_v_from_v : to_v_from_u) + obs_v(t, j);
            next_u[j] = (u_from_v ? to_u_from_v : to_u_from_u) + ou;
            bp[t * 2 * n_bins + j] =
                static_cast<int16_t>(v_from_v ? src_v : src_u + static_cast<int>(n_bins));
            bp[t * 2 * n_bins + n_bins + j] =
                static_cast<int16_t>(u_from_v ? src_v : src_u + static_cast<int>(n_bins));
        }
        double top = -kInf;
        for (std::size_t b = 0; b < n_bins; ++b) top = std::max(top, std::max(next_v[b], next_u[b]));
        for (std::size_t b = 0; b < n_bins; ++b) {
            score_v[b] = next_v[b] - top;
            score_u[b] = next_u[b] - top;
        }
    }

    std::size_t state = 0;
    double best = -kInf;
    for (std::size_t b = 0; b < n_bins; ++b) {
        if (score_v[b] > best) {
            best = score_v[b];
            state = b;
        }
        if (score_u[b] > best) {
            best = score_u[b];
            state = b + n_bins;
        }
    }

    std::vector<int> path(n_frames, -1);
    for (std::size_t t = n_frames; t-- > 0;) {
        path[t] = state < n_bins ? static_cast<int>(state)
                                 : -static_cast<int>(state - n_bins) - 1;
        const int16_t prev = bp[t * 2 * n_bins + state];
        if (prev < 0) break;
        state = static_cast<std::size_t>(prev);
    }
    return path;
}

}  // namespace

double PitchTrack::voiced_ratio() const {
    if (f0.empty()) return 0.0;
    std::size_t n = 0;
    for (double v : f0)
        if (is_finite(v)) ++n;
    return static_cast<double>(n) / f0.size();
}

std::vector<double> cmndf_frame(const double* frame, std::size_t frame_len, std::size_t max_lag) {
    const std::size_t win = frame_len - max_lag;
    const std::size_t nfft = next_pow2(frame_len + win);

    const auto spec_full = rfft(frame, frame_len, nfft);
    const auto spec_short = rfft(frame, win, nfft);
    std::vector<cdouble> prod(spec_full.size());
    for (std::size_t i = 0; i < prod.size(); ++i) prod[i] = spec_full[i] * std::conj(spec_short[i]);
    const auto corr = irfft(prod, nfft);

    // Cumulative energy, so the two window energies are O(1) lookups.
    std::vector<double> cum(frame_len + 1, 0.0);
    for (std::size_t i = 0; i < frame_len; ++i) cum[i + 1] = cum[i] + frame[i] * frame[i];
    const double p0 = cum[win];

    std::vector<double> d(max_lag, 0.0);
    for (std::size_t tau = 0; tau < max_lag; ++tau) {
        const double lagged = cum[tau + win] - cum[tau];
        d[tau] = std::max(p0 + lagged - 2.0 * corr[tau], 0.0);
    }

    std::vector<double> out(max_lag, 1.0);
    double running = 0.0;
    for (std::size_t tau = 1; tau < max_lag; ++tau) {
        running += d[tau];
        const double denom = running / static_cast<double>(tau);
        out[tau] = denom > 1e-12 ? d[tau] / denom : 1.0;
    }
    return out;
}

PitchTrack track_f0(const std::vector<double>& x, int sample_rate, const PitchConfig& cfg_in) {
    PitchConfig cfg = cfg_in;
    if (cfg.frame_len == 0) cfg.frame_len = sample_rate <= 48000 ? 2048 : 4096;
    const std::size_t max_lag = cfg.frame_len / 2;
    double fmin = cfg.fmin;
    if (sample_rate / fmin > static_cast<double>(max_lag) - 2.0)
        fmin = sample_rate / (static_cast<double>(max_lag) - 2.0);

    const double lo_c = 1200.0 * std::log2(fmin / 10.0);
    const double hi_c = 1200.0 * std::log2(cfg.fmax / 10.0);
    std::vector<double> bins;
    for (double c = lo_c; c <= hi_c + cfg.bin_cents; c += cfg.bin_cents) bins.push_back(c);
    const std::size_t n_bins = bins.size();
    std::vector<double> bin_hz(n_bins);
    for (std::size_t i = 0; i < n_bins; ++i) bin_hz[i] = 10.0 * std::pow(2.0, bins[i] / 1200.0);

    const std::size_t pad = cfg.frame_len / 2;
    const std::vector<double> padded = reflect_pad(x, pad);
    const std::size_t n_frames =
        padded.size() >= cfg.frame_len ? 1 + (padded.size() - cfg.frame_len) / cfg.hop : 1;

    std::vector<float> obs(n_frames * n_bins, 0.0f);
    std::vector<double> voiced_prob(n_frames, 0.0);
    std::vector<double> periodicity(n_frames, 0.0);
    std::vector<std::vector<double>> candidates(n_frames);

    std::vector<double> frame(cfg.frame_len);
    for (std::size_t t = 0; t < n_frames; ++t) {
        const std::size_t start = t * cfg.hop;
        for (std::size_t i = 0; i < cfg.frame_len; ++i) {
            const std::size_t idx = std::min(start + i, padded.size() - 1);
            frame[i] = padded[idx];
        }
        const auto row = cmndf_frame(frame.data(), cfg.frame_len, max_lag);
        const FrameObservation fo = observe(row, sample_rate, fmin, cfg.fmax);
        voiced_prob[t] = fo.voiced_prob;
        periodicity[t] = fo.periodicity;
        candidates[t] = fo.candidate_hz;
        for (std::size_t c = 0; c < fo.candidate_hz.size(); ++c) {
            const double cents = 1200.0 * std::log2(fo.candidate_hz[c] / 10.0);
            long b = static_cast<long>(std::upper_bound(bins.begin(), bins.end(), cents) -
                                       bins.begin()) - 1;
            b = std::max<long>(0, std::min<long>(b, static_cast<long>(n_bins) - 1));
            obs[t * n_bins + static_cast<std::size_t>(b)] +=
                static_cast<float>(fo.candidate_prob[c]);
        }
    }

    // Decode in overlapping chunks so memory stays bounded on long takes.
    const std::size_t overlap = std::min<std::size_t>(200, std::max<std::size_t>(n_frames / 10, 1));
    std::vector<int> path(n_frames, -1);
    std::size_t start = 0;
    while (start < n_frames) {
        const std::size_t stop = std::min(start + cfg.chunk_frames, n_frames);
        const std::size_t lo = start > overlap ? start - overlap : 0;
        const std::size_t count = stop - lo;
        std::vector<float> sub(obs.begin() + lo * n_bins, obs.begin() + stop * n_bins);
        std::vector<double> sub_vp(voiced_prob.begin() + lo, voiced_prob.begin() + stop);
        const auto seg = viterbi(sub, count, n_bins, sub_vp,
                                 std::max(1, static_cast<int>(std::lround(cfg.max_step_cents /
                                                                          cfg.bin_cents))),
                                 cfg.switch_prob, std::max(1.0, 35.0 / cfg.bin_cents));
        for (std::size_t t = start; t < stop; ++t) path[t] = seg[t - lo];
        start = stop;
    }

    PitchTrack track;
    track.sample_rate = sample_rate;
    track.hop = cfg.hop;
    track.voiced_prob = voiced_prob;
    track.periodicity = periodicity;
    track.times = frame_times(n_frames, cfg.hop, sample_rate);
    track.f0.assign(n_frames, nan_value());

    for (std::size_t t = 0; t < n_frames; ++t) {
        if (path[t] < 0) continue;
        const double target = bin_hz[static_cast<std::size_t>(path[t])];
        const auto& cand = candidates[t];
        if (cand.empty()) {
            track.f0[t] = target;
            continue;
        }
        // Snap the decoded bin onto the frame's own refined trough, so the bin
        // grid never limits pitch resolution.
        double best = cand[0];
        double best_err = std::abs(std::log(cand[0] / target));
        for (double hz : cand) {
            const double err = std::abs(std::log(hz / target));
            if (err < best_err) {
                best_err = err;
                best = hz;
            }
        }
        track.f0[t] = best;
    }
    return track;
}

}  // namespace vocalai
