#include "vocalai/psola.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>

#include "vocalai/dsp.h"
#include "vocalai/filters.h"

namespace vocalai {
namespace {

const std::vector<double>& hann_cached(std::size_t n) {
    static std::map<std::size_t, std::vector<double>> cache;
    auto it = cache.find(n);
    if (it != cache.end()) return it->second;
    if (cache.size() > 4096) cache.clear();
    return cache.emplace(n, hann(n)).first->second;
}

}  // namespace

std::vector<double> frames_to_samples(const std::vector<double>& values, std::size_t hop,
                                      std::size_t n_samples) {
    std::vector<double> out(n_samples, nan_value());
    if (values.empty()) return out;
    for (std::size_t i = 0; i < n_samples; ++i) {
        const std::size_t f = std::min(i / hop, values.size() - 1);
        out[i] = values[f];
    }
    return out;
}

EpochTrack detect_epochs(const std::vector<double>& x, int sample_rate,
                         const std::vector<double>& f0, std::size_t hop, double bandwidth_hz) {
    const std::size_t n = x.size();
    EpochTrack track;
    const std::vector<double> per_sample = frames_to_samples(f0, hop, n);
    track.voiced.resize(n);
    std::vector<std::size_t> known;
    for (std::size_t i = 0; i < n; ++i) {
        track.voiced[i] = is_finite(per_sample[i]);
        if (track.voiced[i]) known.push_back(i);
    }
    if (known.empty() || n < 8) return track;

    // A continuous frequency track: unvoiced stretches hold the nearest value
    // so the reference phase stays smooth across them.
    std::vector<double> filled(n);
    {
        std::vector<double> xs, ys;
        xs.reserve(known.size());
        ys.reserve(known.size());
        for (std::size_t i : known) {
            xs.push_back(static_cast<double>(i));
            ys.push_back(per_sample[i]);
        }
        for (std::size_t i = 0; i < n; ++i) filled[i] = interp(static_cast<double>(i), xs, ys);
    }

    // Heterodyne by the tracked F0, shifting the fundamental to DC, then keep
    // only the slow residual: its phase is the fundamental's own phase.
    std::vector<double> phase_ref(n);
    double acc = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        acc += filled[i];
        phase_ref[i] = 2.0 * M_PI * acc / sample_rate;
    }
    std::vector<double> zr(n), zi(n);
    for (std::size_t i = 0; i < n; ++i) {
        zr[i] = x[i] * std::cos(-phase_ref[i]);
        zi[i] = x[i] * std::sin(-phase_ref[i]);
    }
    const double nyq = sample_rate / 2.0;
    const Sos sos = butter_lowpass(3, std::min(bandwidth_hz / nyq, 0.95));
    zr = sosfiltfilt(sos, zr);
    zi = sosfiltfilt(sos, zi);

    std::vector<double> phase(n);
    // np.unwrap leaves the first sample untouched, so the running total starts
    // at that angle rather than at zero; starting from zero would offset the
    // whole phase curve and shift every pitch mark within its cycle.
    double previous = std::atan2(zi[0], zr[0]);
    double unwrapped = previous;
    phase[0] = phase_ref[0] + unwrapped;
    for (std::size_t i = 1; i < n; ++i) {
        const double angle = std::atan2(zi[i], zr[i]);
        double delta = angle - previous;
        while (delta > M_PI) delta -= 2.0 * M_PI;
        while (delta < -M_PI) delta += 2.0 * M_PI;
        unwrapped += delta;
        previous = angle;
        phase[i] = phase_ref[i] + unwrapped;
    }
    // Guard against the unwrapped phase stalling or reversing on noisy frames.
    for (std::size_t i = 1; i < n; ++i) phase[i] = std::max(phase[i], phase[i - 1]);

    // Centre the marks on the loudest instant of the cycle - roughly the
    // glottal closure - so each window holds the strongest part of the wave.
    double offset = 0.0;
    {
        constexpr int kBins = 24;
        double totals[kBins] = {0.0};
        std::size_t counted = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (!track.voiced[i] || !is_finite(phase[i])) continue;
            double wrapped = std::fmod(phase[i], 2.0 * M_PI);
            if (wrapped < 0.0) wrapped += 2.0 * M_PI;
            const int b = std::min(static_cast<int>(wrapped / (2.0 * M_PI) * kBins), kBins - 1);
            totals[b] += x[i] * x[i];
            ++counted;
        }
        if (counted >= 64) {
            const int best = static_cast<int>(std::max_element(totals, totals + kBins) - totals);
            offset = (best + 0.5) / kBins * 2.0 * M_PI;
        }
    }

    const long lo_k = static_cast<long>(std::ceil((phase.front() - offset) / (2.0 * M_PI)));
    const long hi_k = static_cast<long>(std::floor((phase.back() - offset) / (2.0 * M_PI)));
    if (hi_k <= lo_k) return track;

    std::vector<double> idx(n);
    for (std::size_t i = 0; i < n; ++i) idx[i] = static_cast<double>(i);
    for (long k = lo_k; k <= hi_k; ++k) {
        const double target = 2.0 * M_PI * static_cast<double>(k) + offset;
        const double pos = interp(target, phase, idx);
        long mark = std::lround(pos);
        mark = std::max<long>(0, std::min<long>(mark, static_cast<long>(n) - 1));
        if (!track.voiced[static_cast<std::size_t>(mark)]) continue;
        track.marks.push_back(mark);
        track.periods.push_back(sample_rate /
                                std::max(filled[static_cast<std::size_t>(mark)], 1e-6));
    }
    if (track.marks.size() < 2) {
        track.marks.clear();
        track.periods.clear();
    }
    return track;
}

namespace {

// Overlap-add windowed glottal cycles at their new spacing.  This loop runs
// once per output pitch period, so everything inside it is scalar indexing.
void synthesize_voiced(const std::vector<double>& x, std::vector<double>& out,
                       std::vector<double>& norm, const EpochTrack& epochs,
                       const std::vector<double>& ratio_s,
                       const std::vector<double>& time_map) {
    const long n = static_cast<long>(x.size());
    const long out_len = static_cast<long>(out.size());
    const long map_len = static_cast<long>(time_map.size());
    const std::size_t n_marks = epochs.marks.size();
    if (n_marks < 2 || out_len == 0) return;

    double out_pos = 0.0;
    while (out_pos < static_cast<double>(out_len)) {
        const long idx_out = static_cast<long>(out_pos);
        const double src = time_map[static_cast<std::size_t>(
            idx_out < map_len ? idx_out : map_len - 1)];
        long i = static_cast<long>(src);
        i = std::max<long>(0, std::min<long>(i, n - 1));
        if (!epochs.voiced[static_cast<std::size_t>(i)]) {
            out_pos += 1.0;
            continue;
        }

        std::size_t k = static_cast<std::size_t>(
            std::lower_bound(epochs.marks.begin(), epochs.marks.end(), i) -
            epochs.marks.begin());
        if (k >= n_marks) k = n_marks - 1;
        else if (k > 0 && std::abs(epochs.marks[k] - i) > std::abs(epochs.marks[k - 1] - i)) --k;

        const long mark = epochs.marks[k];
        const double period = epochs.periods[k];
        if (period < 2.0) {
            out_pos += 1.0;
            continue;
        }

        const long half = std::lround(period);
        const long lo = mark - half, hi = mark + half;
        const long seg_lo = std::max<long>(lo, 0);
        const long seg_hi = std::min<long>(hi, n);
        if (seg_hi - seg_lo < 4) {
            out_pos += 1.0;
            continue;
        }

        const std::vector<double>& win = hann_cached(static_cast<std::size_t>(2 * half + 1));
        const long dst = std::lround(out_pos);
        const long d_lo = dst - (mark - seg_lo);
        const long d_hi = dst + (seg_hi - mark);
        const long c_lo = std::max<long>(d_lo, 0);
        const long c_hi = std::min<long>(d_hi, out_len);
        for (long p = c_lo; p < c_hi; ++p) {
            const long src_i = seg_lo + (p - d_lo);
            const long w_i = src_i - lo;
            if (src_i < 0 || src_i >= n || w_i < 0 ||
                w_i >= static_cast<long>(win.size()))
                continue;
            const double w = win[static_cast<std::size_t>(w_i)];
            out[static_cast<std::size_t>(p)] += x[static_cast<std::size_t>(src_i)] * w;
            norm[static_cast<std::size_t>(p)] += w;
        }

        // One output period: the input period divided by the pitch ratio, and
        // scaled again by however fast the time map is moving here.
        const long nxt = idx_out + 1;
        double stretch = nxt < map_len ? time_map[static_cast<std::size_t>(nxt)] -
                                             time_map[static_cast<std::size_t>(idx_out)]
                                       : 1.0;
        if (stretch < 1e-3) stretch = 1e-3;
        double ratio = ratio_s[static_cast<std::size_t>(i)];
        if (!(ratio > 1e-3)) ratio = 1e-3;
        const double step = period / ratio / stretch;
        out_pos += step > 2.0 ? step : 2.0;
    }
}

// Pass unvoiced audio through untouched, crossfading at the boundaries: the
// consonants have no pulses to re-space, and smearing them is audible.
void copy_unvoiced(const std::vector<double>& x, std::vector<double>& out,
                   std::vector<double>& norm, const std::vector<bool>& voiced,
                   const std::vector<double>& time_map, int sample_rate) {
    const long n = static_cast<long>(x.size());
    const long out_len = static_cast<long>(out.size());
    const long fade = std::max<long>(static_cast<long>(0.004 * sample_rate), 8);

    std::vector<long> src(static_cast<std::size_t>(out_len));
    std::vector<bool> unvoiced_out(static_cast<std::size_t>(out_len));
    for (long i = 0; i < out_len; ++i) {
        long s = std::lround(time_map[static_cast<std::size_t>(
            std::min<long>(i, static_cast<long>(time_map.size()) - 1))]);
        s = std::max<long>(0, std::min<long>(s, n - 1));
        src[static_cast<std::size_t>(i)] = s;
        unvoiced_out[static_cast<std::size_t>(i)] = !voiced[static_cast<std::size_t>(s)];
    }

    for (const auto& run : true_runs(unvoiced_out)) {
        const long lo = static_cast<long>(run.first);
        const long hi = static_cast<long>(run.second);
        const long lo_f = std::max<long>(lo - fade, 0);
        const long hi_f = std::min<long>(hi + fade, out_len);
        const long head = lo - lo_f;
        const long tail = hi_f - hi;
        for (long i = lo_f; i < hi_f; ++i) {
            double w = 1.0;
            if (head > 0 && i < lo) w = static_cast<double>(i - lo_f) / head;
            if (tail > 0 && i >= hi) w = static_cast<double>(hi_f - i) / tail;
            out[static_cast<std::size_t>(i)] += x[static_cast<std::size_t>(src[static_cast<std::size_t>(i)])] * w;
            norm[static_cast<std::size_t>(i)] += w;
        }
    }
}

}  // namespace

std::vector<double> psola_modify(const std::vector<double>& x, int sample_rate,
                                 const EpochTrack& epochs,
                                 const std::vector<double>& pitch_ratio, std::size_t hop,
                                 const std::vector<double>* time_map_in,
                                 std::size_t out_length) {
    const std::size_t n = x.size();
    std::vector<double> ratio_clean(pitch_ratio.size());
    for (std::size_t i = 0; i < pitch_ratio.size(); ++i)
        ratio_clean[i] = is_finite(pitch_ratio[i]) ? pitch_ratio[i] : 1.0;
    std::vector<double> ratio_s = frames_to_samples(ratio_clean, hop, n);
    for (double& r : ratio_s) r = std::min(std::max(is_finite(r) ? r : 1.0, 0.25), 4.0);

    std::vector<double> time_map;
    std::size_t out_len;
    if (time_map_in == nullptr) {
        out_len = out_length ? out_length : n;
        time_map.resize(out_len);
        for (std::size_t i = 0; i < out_len; ++i) time_map[i] = static_cast<double>(i);
    } else {
        time_map = *time_map_in;
        out_len = out_length ? out_length : time_map.size();
    }
    if (out_len == 0) return {};

    std::vector<double> out(out_len, 0.0), norm(out_len, 0.0);
    synthesize_voiced(x, out, norm, epochs, ratio_s, time_map);
    copy_unvoiced(x, out, norm, epochs.voiced, time_map, sample_rate);
    for (std::size_t i = 0; i < out_len; ++i)
        if (norm[i] > 1e-6) out[i] /= norm[i];
    return out;
}

namespace {

// Offset whose leading half best continues the previous tail.  Textbook WSOLA
// takes the similarity maximum outright; over silence every candidate scores
// the same, the tie breaks toward an arbitrary edge of the search range, and
// because each frame continues from the drifted one the output stays offset
// from the timing that was asked for.  A quadratic pull toward the mapped
// position costs nothing when the peak is real and settles it when it is not.
long best_offset(const std::vector<double>& x, const std::vector<double>& tmpl, long lo, long hi,
                 long half, long target, double bias = 0.08) {
    const long n = static_cast<long>(x.size());
    if (hi <= lo || tmpl.empty()) return target;
    double t_norm = 0.0;
    for (double v : tmpl) t_norm += v * v;
    t_norm = std::sqrt(t_norm);
    if (t_norm < 1e-9) return target;

    const long reach = std::max<long>(std::max(hi - target, target - lo), 1);
    double best_score = -1e300;
    long best = lo;
    for (long off = lo; off <= hi; ++off) {
        if (off + half > n) break;
        double dot = 0.0, energy = 0.0;
        for (long j = 0; j < half; ++j) {
            const double v = x[static_cast<std::size_t>(off + j)];
            dot += v * tmpl[static_cast<std::size_t>(j)];
            energy += v * v;
        }
        double score = dot / (std::sqrt(energy) + 1e-9) / t_norm;
        const double d = static_cast<double>(off - target) / reach;
        score -= bias * d * d;
        if (score > best_score) {
            best_score = score;
            best = off;
        }
    }
    return best;
}

}  // namespace

std::vector<double> wsola_time_warp(const std::vector<double>& x, int sample_rate,
                                    const std::vector<double>& time_map, std::size_t frame_len_in,
                                    long tolerance_in, const std::vector<double>* transients,
                                    long transient_tolerance_in) {
    const long n = static_cast<long>(x.size());
    const long out_len = static_cast<long>(time_map.size());
    if (n == 0 || out_len == 0) return std::vector<double>(static_cast<std::size_t>(out_len), 0.0);

    long frame_len = static_cast<long>(frame_len_in ? frame_len_in
                                                    : static_cast<std::size_t>(0.030 * sample_rate));
    frame_len = std::max<long>(frame_len / 2 * 2, 64);
    const long half = frame_len / 2;
    const long tolerance =
        tolerance_in >= 0 ? tolerance_in : static_cast<long>(0.008 * sample_rate);
    const long transient_tolerance =
        transient_tolerance_in >= 0 ? transient_tolerance_in
                                    : std::max<long>(static_cast<long>(0.001 * sample_rate), 8);

    std::vector<double> marks;
    if (transients) {
        marks = *transients;
        std::sort(marks.begin(), marks.end());
    }

    const std::vector<double>& win = hann_cached(static_cast<std::size_t>(frame_len));
    std::vector<double> out(static_cast<std::size_t>(out_len + frame_len), 0.0);
    std::vector<double> norm(static_cast<std::size_t>(out_len + frame_len), 0.0);
    std::vector<double> prev_tail(static_cast<std::size_t>(half), 0.0);
    std::vector<double> seg(static_cast<std::size_t>(frame_len));

    for (long out_start = 0; out_start < out_len; out_start += half) {
        const double target_f = time_map[static_cast<std::size_t>(out_start)];
        const long target = std::lround(target_f);

        long tol = tolerance;
        if (!marks.empty()) {
            const auto it = std::lower_bound(marks.begin(), marks.end(), target_f);
            double near = 1e30;
            if (it != marks.end()) near = std::min(near, std::abs(*it - target_f));
            if (it != marks.begin()) near = std::min(near, std::abs(*(it - 1) - target_f));
            if (near < static_cast<double>(half)) tol = transient_tolerance;
        }

        long best;
        if (out_start == 0) {
            best = target;
        } else {
            const long lo = std::max<long>(target - tol, 0);
            const long hi = std::min<long>(target + tol, std::max<long>(n - frame_len, 0));
            best = hi > lo ? best_offset(x, prev_tail, lo, hi, half, target)
                           : std::min<long>(target, std::max<long>(n - frame_len, 0));
        }
        best = std::max<long>(0, std::min<long>(best, std::max<long>(n - frame_len, 0)));

        const long take = std::min<long>(frame_len, n - best);
        if (take <= 0) break;
        std::fill(seg.begin(), seg.end(), 0.0);
        for (long i = 0; i < take; ++i) seg[static_cast<std::size_t>(i)] = x[static_cast<std::size_t>(best + i)];
        for (long i = 0; i < frame_len; ++i) {
            out[static_cast<std::size_t>(out_start + i)] += seg[static_cast<std::size_t>(i)] * win[static_cast<std::size_t>(i)];
            norm[static_cast<std::size_t>(out_start + i)] += win[static_cast<std::size_t>(i)];
        }
        for (long i = 0; i < half; ++i) prev_tail[static_cast<std::size_t>(i)] = seg[static_cast<std::size_t>(half + i)];
    }

    std::vector<double> result(static_cast<std::size_t>(out_len));
    for (long i = 0; i < out_len; ++i)
        result[static_cast<std::size_t>(i)] =
            norm[static_cast<std::size_t>(i)] > 1e-6
                ? out[static_cast<std::size_t>(i)] / norm[static_cast<std::size_t>(i)]
                : 0.0;
    return result;
}

}  // namespace vocalai
