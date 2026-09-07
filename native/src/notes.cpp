#include "vocalai/notes.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "vocalai/fft.h"

namespace vocalai {
namespace {

std::vector<double> rolling_median(const std::vector<double>& x, std::size_t frames) {
    return median_filter_nan(x, std::max<std::size_t>(3, frames | 1));
}

double segment_median(const std::vector<double>& x, std::size_t lo, std::size_t hi) {
    if (hi <= lo) return nan_value();
    return nan_median(x.data() + lo, hi - lo);
}

bool any_finite(const std::vector<double>& x, std::size_t lo, std::size_t hi) {
    for (std::size_t i = lo; i < hi && i < x.size(); ++i)
        if (is_finite(x[i])) return true;
    return false;
}

// Recursively split a range wherever the two halves' medians disagree.
void binary_split(const std::vector<double>& cents, std::size_t lo, std::size_t hi,
                  const SegmentConfig& cfg, std::size_t min_frames, std::vector<std::size_t>& out) {
    const std::size_t n = hi - lo;
    if (n < 2 * min_frames) return;
    if (!any_finite(cents, lo, hi)) return;

    long best_pos = -1;
    double best_gap = 0.0;
    const std::size_t steps[2] = {std::max<std::size_t>(min_frames / 2, 2), 1};
    for (std::size_t s = 0; s < 2; ++s) {
        const std::size_t step = steps[s];
        long lo_b = static_cast<long>(min_frames);
        long hi_b = static_cast<long>(n - min_frames);
        if (best_pos > 0) {
            lo_b = std::max<long>(static_cast<long>(min_frames), best_pos - static_cast<long>(step) * 2);
            hi_b = std::min<long>(static_cast<long>(n - min_frames), best_pos + static_cast<long>(step) * 2);
        }
        for (long k = lo_b; k <= hi_b; k += static_cast<long>(step)) {
            const std::size_t cut = lo + static_cast<std::size_t>(k);
            if (!any_finite(cents, lo, cut) || !any_finite(cents, cut, hi)) continue;
            const double gap = std::abs(segment_median(cents, cut, hi) - segment_median(cents, lo, cut));
            if (gap > best_gap) {
                best_gap = gap;
                best_pos = k;
            }
        }
    }
    if (best_pos < 0 || best_gap < cfg.split_cents) return;

    const std::size_t cut = lo + static_cast<std::size_t>(best_pos);
    out.push_back(cut);
    binary_split(cents, lo, cut, cfg, min_frames, out);
    binary_split(cents, cut, hi, cfg, min_frames, out);
}

// Move a cut onto the steepest point of the glide between two notes.
std::size_t refine_cut(const std::vector<double>& cents, std::size_t cut, std::size_t radius) {
    const std::size_t lo = cut > radius + 1 ? cut - radius : 1;
    const std::size_t hi = std::min(cents.size() - 1, cut + radius);
    if (hi <= lo) return cut;
    for (std::size_t i = lo - 1; i <= hi && i < cents.size(); ++i)
        if (!is_finite(cents[i])) return cut;

    double best = -1.0;
    std::size_t best_i = lo;
    for (std::size_t i = lo; i <= hi; ++i) {
        const double slope = std::abs(cents[i] - cents[i - 1]);
        if (slope > best) {
            best = slope;
            best_i = i;
        }
    }
    return best_i;
}

std::vector<std::size_t> dedupe(std::vector<std::size_t> cuts, std::size_t min_frames) {
    std::sort(cuts.begin(), cuts.end());
    std::vector<std::size_t> out;
    for (std::size_t c : cuts)
        if (out.empty() || c - out.back() >= min_frames) out.push_back(c);
    return out;
}

// Drop cuts that separate two segments of effectively the same pitch, and any
// cut that leaves a fragment too short to be a note anyone sang.
std::vector<std::size_t> merge_segments(std::vector<std::size_t> bounds,
                                        const std::vector<double>& center_c,
                                        const SegmentConfig& cfg,
                                        const std::vector<double>& onsets, double frame_rate,
                                        std::size_t min_frames) {
    bool changed = true;
    while (changed && bounds.size() > 2) {
        changed = false;

        std::size_t shortest = bounds.size();
        std::size_t shortest_len = min_frames;
        for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
            const std::size_t len = bounds[i + 1] - bounds[i];
            if (len < shortest_len) {
                shortest_len = len;
                shortest = i;
            }
        }
        if (shortest < bounds.size()) {
            const std::size_t drop = shortest > 0 ? shortest : shortest + 1;
            if (drop > 0 && drop < bounds.size() - 1) {
                bounds.erase(bounds.begin() + static_cast<long>(drop));
                changed = true;
                continue;
            }
        }

        for (std::size_t i = 1; i + 1 < bounds.size(); ++i) {
            const std::size_t a = bounds[i - 1], cut = bounds[i], b = bounds[i + 1];
            if (!any_finite(center_c, a, cut) || !any_finite(center_c, cut, b)) continue;
            const double gap =
                std::abs(segment_median(center_c, cut, b) - segment_median(center_c, a, cut));
            bool near_onset = false;
            for (double t : onsets)
                if (std::abs(t - cut / frame_rate) < 0.035) near_onset = true;
            const bool too_short = (cut - a) < min_frames || (b - cut) < min_frames;
            if (gap < cfg.merge_cents && (!near_onset || too_short)) {
                bounds.erase(bounds.begin() + static_cast<long>(i));
                changed = true;
                break;
            }
        }
    }
    return bounds;
}

// Remove segments that are a slide between two notes rather than a note.  A
// sung note always has somewhere it settles; a segment whose centre sweeps
// steadily through a large interval and never holds still is the glide of a
// leap, and reporting it invents a pitch the singer never sang.
std::vector<std::size_t> dissolve_glides(std::vector<std::size_t> bounds,
                                         const std::vector<double>& center_c,
                                         const std::vector<double>& smooth_c,
                                         const SegmentConfig& cfg, double frame_rate) {
    const std::size_t max_frames = static_cast<std::size_t>(cfg.max_glide * frame_rate);
    bool changed = true;
    while (changed && bounds.size() > 2) {
        changed = false;
        for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
            const std::size_t a = bounds[i], b = bounds[i + 1];
            if (b - a > max_frames) continue;
            std::vector<double> seg;
            for (std::size_t k = a; k < b && k < center_c.size(); ++k)
                if (is_finite(center_c[k])) seg.push_back(center_c[k]);
            if (seg.size() < 3) continue;
            const double span = *std::max_element(seg.begin(), seg.end()) -
                                *std::min_element(seg.begin(), seg.end());
            if (span < cfg.glide_span) continue;

            double travel = 0.0, path = 0.0;
            for (std::size_t k = 1; k < seg.size(); ++k) {
                const double d = seg[k] - seg[k - 1];
                travel += d;
                path += std::abs(d);
            }
            if (path <= 0.0 || std::abs(travel) / path < cfg.glide_monotonic)
                continue;  // it wobbles, so it is a note being sung, not a slide

            if (i == 0) {
                bounds.erase(bounds.begin() + 1);
            } else if (i == bounds.size() - 2) {
                bounds.erase(bounds.end() - 2);
            } else {
                bounds[i] = refine_cut(smooth_c, (a + b) / 2, (b - a) / 2);
                bounds.erase(bounds.begin() + static_cast<long>(i) + 1);
            }
            changed = true;
            break;
        }
    }
    return bounds;
}

// Rate and depth of vibrato on a note core.  Depth comes from the band-limited
// component alone, so a note that merely sags is not called modulated.
void measure_vibrato(const std::vector<double>& core, double frame_rate, double* rate_out,
                     double* depth_out, double min_depth = 6.0) {
    *rate_out = 0.0;
    *depth_out = 0.0;
    const std::size_t n = core.size();
    if (n < static_cast<std::size_t>(0.18 * frame_rate)) return;
    for (double v : core)
        if (!is_finite(v)) return;

    // Remove the linear trend: drift is not vibrato.
    double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double xi = static_cast<double>(i);
        sx += xi;
        sy += core[i];
        sxx += xi * xi;
        sxy += xi * core[i];
    }
    const double denom = n * sxx - sx * sx;
    const double slope = std::abs(denom) > 1e-12 ? (n * sxy - sx * sy) / denom : 0.0;
    const double intercept = (sy - slope * sx) / static_cast<double>(n);
    std::vector<double> detrended(n);
    for (std::size_t i = 0; i < n; ++i)
        detrended[i] = core[i] - (slope * static_cast<double>(i) + intercept);

    std::size_t nfft = 1024;
    while (nfft < n * 4) nfft <<= 1;
    std::vector<double> windowed(n);
    const auto win = hann(n);
    for (std::size_t i = 0; i < n; ++i) windowed[i] = detrended[i] * win[i];
    const auto spec = rfft(windowed.data(), n, nfft);

    double band_energy = 0.0, low_energy = 0.0, peak = -1.0;
    std::size_t peak_k = 0;
    for (std::size_t k = 0; k < spec.size(); ++k) {
        const double f = static_cast<double>(k) * frame_rate / static_cast<double>(nfft);
        const double mag = std::abs(spec[k]);
        if (f >= 0.2 && f <= 20.0) low_energy += mag * mag;
        if (f >= kVibratoMinHz && f <= kVibratoMaxHz) {
            band_energy += mag * mag;
            if (mag > peak) {
                peak = mag;
                peak_k = k;
            }
        }
    }
    if (peak <= 0.0 || low_energy <= 0.0) return;
    // Both sums span the same padded grid, so the ratio is padding-invariant.
    if (band_energy / low_energy < 0.30) return;

    // Depth from the band-limited residual alone.
    auto coeff = rfft(detrended.data(), n, n);
    for (std::size_t k = 0; k < coeff.size(); ++k) {
        const double f = static_cast<double>(k) * frame_rate / static_cast<double>(n);
        if (f < kVibratoMinHz || f > kVibratoMaxHz) coeff[k] = cdouble(0.0, 0.0);
    }
    const auto band = irfft(coeff, n);
    double mean = 0.0;
    for (double v : band) mean += v;
    mean /= static_cast<double>(n);
    double var = 0.0;
    for (double v : band) var += (v - mean) * (v - mean);
    const double depth = std::sqrt(2.0) * std::sqrt(var / static_cast<double>(n));
    if (depth < min_depth) return;

    *rate_out = static_cast<double>(peak_k) * frame_rate / static_cast<double>(nfft);
    *depth_out = depth;
}

// Trim the approach and release glides, leaving the settled part of the note.
void find_core(const std::vector<double>& smooth_c, std::size_t a, std::size_t b, double center,
               const SegmentConfig& cfg, double frame_rate, std::size_t* out_start,
               std::size_t* out_stop) {
    const std::size_t n = b - a;
    const std::size_t max_trim = static_cast<std::size_t>(0.45 * n);
    const std::size_t hold = std::max<std::size_t>(1, static_cast<std::size_t>(0.025 * frame_rate));

    std::size_t start = a + std::min(max_trim, static_cast<std::size_t>(0.15 * n));
    for (std::size_t i = a; i < a + max_trim && i < smooth_c.size(); ++i) {
        bool settled = true;
        for (std::size_t k = i; k < i + hold; ++k) {
            if (k >= smooth_c.size() || !is_finite(smooth_c[k]) ||
                std::abs(smooth_c[k] - center) > cfg.core_tolerance) {
                settled = false;
                break;
            }
        }
        if (settled) {
            start = i;
            break;
        }
    }

    std::size_t stop = b - std::min(max_trim, static_cast<std::size_t>(0.1 * n));
    for (std::size_t i = b; i > b - max_trim && i > a; --i) {
        const std::size_t lo = i > hold && i - hold > a ? i - hold : a;
        bool settled = lo < i;
        for (std::size_t k = lo; k < i; ++k) {
            if (k >= smooth_c.size() || !is_finite(smooth_c[k]) ||
                std::abs(smooth_c[k] - center) > cfg.core_tolerance) {
                settled = false;
                break;
            }
        }
        if (settled) {
            stop = i;
            break;
        }
    }

    if (stop <= start ||
        static_cast<double>(stop - start) / frame_rate < cfg.min_core) {
        const std::size_t mid = (a + b) / 2;
        const std::size_t half =
            std::max<std::size_t>(1, static_cast<std::size_t>(0.5 * cfg.min_core * frame_rate));
        start = mid > half && mid - half > a ? mid - half : a;
        stop = std::min(b, mid + half);
    }
    *out_start = start;
    *out_stop = stop;
}

}  // namespace

NoteSequence segment_notes(const PitchTrack& track, const std::vector<double>& audio,
                           const Spectrogram* spec_in, const SegmentConfig& cfg,
                           const std::string& source) {
    NoteSequence seq;
    seq.sample_rate = track.sample_rate;
    seq.hop = track.hop;
    seq.source = source;
    if (track.f0.empty()) return seq;

    const double frame_rate = track.frame_rate();
    Spectrogram owned;
    if (!spec_in) owned = spectrogram(audio, track.sample_rate, 2048, track.hop);
    const Spectrogram& spec = spec_in ? *spec_in : owned;

    const auto onsets = pick_onsets(onset_envelope(spec), spec.times, cfg.onset_delta);
    const auto loud = loudness_envelope(audio, track.sample_rate, track.hop);

    std::vector<double> cents(track.f0.size());
    for (std::size_t i = 0; i < track.f0.size(); ++i)
        cents[i] = is_finite(track.f0[i])
                       ? 1200.0 * std::log2(std::max(track.f0[i], 1e-9) / kCentsRef)
                       : nan_value();

    // Bridge consonant-length dropouts so one word is not chopped into pieces.
    const auto bridged =
        interpolate_gaps(cents, static_cast<long>(cfg.max_gap * frame_rate));
    const auto smooth_c = rolling_median(bridged, static_cast<std::size_t>(0.029 * frame_rate));
    const auto center_c = rolling_median(bridged, static_cast<std::size_t>(cfg.center_window * frame_rate));

    const std::size_t min_frames =
        std::max<std::size_t>(2, static_cast<std::size_t>(cfg.min_note * frame_rate));
    const std::size_t window_frames =
        std::max(min_frames, static_cast<std::size_t>(cfg.transition_window * frame_rate));

    for (const auto& run : finite_runs(bridged)) {
        const std::size_t lo = run.first, hi = run.second;
        if (hi - lo < min_frames) continue;

        std::vector<std::size_t> cuts;
        binary_split(center_c, lo, hi, cfg, min_frames, cuts);

        // A repeated note re-articulates without a pitch change: trust the onset.
        for (double t : onsets) {
            const long f = std::lround(t * frame_rate);
            if (f < static_cast<long>(lo + min_frames) || f > static_cast<long>(hi - min_frames))
                continue;
            bool clash = false;
            for (std::size_t c : cuts)
                if (std::abs(static_cast<long>(c) - f) <= static_cast<long>(min_frames)) clash = true;
            if (!clash) cuts.push_back(static_cast<std::size_t>(f));
        }

        // Refine first, then de-duplicate: refinement moves cuts, and two cues
        // pointing at one transition must not leave a sliver between them.
        const std::size_t radius = std::max<std::size_t>(1, static_cast<std::size_t>(0.030 * frame_rate));
        for (std::size_t& c : cuts) c = refine_cut(smooth_c, c, radius);
        cuts = dedupe(cuts, window_frames);

        std::vector<std::size_t> bounds{lo};
        for (std::size_t c : cuts)
            if (c > lo && c < hi) bounds.push_back(c);
        bounds.push_back(hi);
        bounds = merge_segments(bounds, center_c, cfg, onsets, frame_rate, window_frames);
        bounds = dissolve_glides(bounds, center_c, smooth_c, cfg, frame_rate);

        for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
            const std::size_t a = bounds[i], b = bounds[i + 1];
            if (b <= a) continue;
            if (static_cast<double>(b - a) / frame_rate < cfg.min_note) continue;

            // The centre is read from the middle, away from the glides.
            const std::size_t inner_lo = a + static_cast<std::size_t>(0.20 * (b - a));
            const std::size_t inner_hi = b - static_cast<std::size_t>(0.15 * (b - a));
            double center = inner_hi > inner_lo && any_finite(smooth_c, inner_lo, inner_hi)
                                ? segment_median(smooth_c, inner_lo, inner_hi)
                                : segment_median(smooth_c, a, b);
            if (!is_finite(center)) continue;

            std::size_t core_a = a, core_b = b;
            find_core(smooth_c, a, b, center, cfg, frame_rate, &core_a, &core_b);
            if (core_b > core_a && any_finite(smooth_c, core_a, core_b))
                center = segment_median(smooth_c, core_a, core_b);
            if (!is_finite(center)) continue;

            std::vector<double> core;
            for (std::size_t k = core_a; k < core_b && k < smooth_c.size(); ++k)
                core.push_back(is_finite(smooth_c[k]) ? smooth_c[k] : center);

            Note note;
            note.index = static_cast<int>(seq.notes.size());
            note.start = static_cast<double>(a) / frame_rate;
            note.end = static_cast<double>(b) / frame_rate;
            note.core_start = static_cast<double>(core_a) / frame_rate;
            note.core_end = static_cast<double>(core_b) / frame_rate;
            note.center_hz = cents_to_hz(center, kCentsRef);
            measure_vibrato(core, frame_rate, &note.vibrato_hz, &note.vibrato_cents);

            if (core.size() >= 3) {
                double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
                for (std::size_t k = 0; k < core.size(); ++k) {
                    sx += k;
                    sy += core[k];
                    sxx += static_cast<double>(k) * k;
                    sxy += static_cast<double>(k) * core[k];
                }
                const double nn = static_cast<double>(core.size());
                const double den = nn * sxx - sx * sx;
                const double slope = std::abs(den) > 1e-12 ? (nn * sxy - sx * sy) / den : 0.0;
                note.drift_cents = slope * nn;
            }

            const std::size_t head_end =
                std::min(a + std::max<std::size_t>(1, static_cast<std::size_t>(0.02 * frame_rate)), b);
            const double head = segment_median(smooth_c, a, head_end);
            note.onset_cents = is_finite(head) ? head - center : 0.0;

            const std::size_t lo_f = std::min(a, loud.empty() ? 0 : loud.size() - 1);
            const std::size_t hi_f = std::min(std::max(b, lo_f + 1), loud.size());
            if (hi_f > lo_f) {
                double sum = 0.0, peak = -1e30;
                for (std::size_t k = lo_f; k < hi_f; ++k) {
                    sum += loud[k];
                    peak = std::max(peak, loud[k]);
                }
                note.amplitude_db = sum / static_cast<double>(hi_f - lo_f);
                note.peak_db = peak;
            }

            double conf = 0.0, voiced = 0.0;
            std::size_t counted = 0;
            for (std::size_t k = a; k < b && k < track.voiced_prob.size(); ++k) {
                conf += track.voiced_prob[k];
                voiced += track.voiced(k) ? 1.0 : 0.0;
                ++counted;
            }
            if (counted) {
                note.confidence = conf / counted;
                note.voiced_ratio = voiced / counted;
            }
            seq.notes.push_back(note);
        }
    }
    for (std::size_t i = 0; i < seq.notes.size(); ++i) seq.notes[i].index = static_cast<int>(i);
    return seq;
}

}  // namespace vocalai
