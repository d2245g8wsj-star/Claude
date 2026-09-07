#include "vocalai/tuner.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <stdexcept>

#include "vocalai/filters.h"
#include "vocalai/notes.h"

namespace vocalai {
namespace {

// Relative frequency of melodic intervals in tonal song writing: steps
// dominate, leaps thin out quickly, the tritone is nearly absent.
const double kIntervalP[13] = {0.130, 0.100, 0.210, 0.100, 0.070, 0.070, 0.010,
                               0.045, 0.014, 0.020, 0.010, 0.006, 0.020};

double melodic_logp(double step) {
    const long k = std::labs(std::lround(step));
    double p = k <= 12 ? kIntervalP[k] : 0.02 * std::pow(0.5, static_cast<double>(k - 12));
    return std::log(std::max(p, 1e-6));
}

// Track a singer drifting flat or sharp across a phrase.  Correlated error is
// real - voices tire, and a whole section can sit under the note - so it
// belongs in the model rather than being read as a run of separate mistakes.
std::vector<double> local_drift(const std::vector<double>& sung, int window, double limit) {
    const std::size_t n = sung.size();
    std::vector<double> drift(n, 0.0);
    if (n < static_cast<std::size_t>(std::max(window, 4))) return drift;

    std::vector<double> angles(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double offset = (sung[i] - std::lround(sung[i])) * 100.0;
        angles[i] = offset * 2.0 * M_PI / 100.0;
    }
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t lo = i > static_cast<std::size_t>(window) ? i - window : 0;
        const std::size_t hi = std::min(n, i + window + 1);
        std::complex<double> acc(0.0, 0.0);
        for (std::size_t k = lo; k < hi; ++k)
            acc += std::complex<double>(std::cos(angles[k]), std::sin(angles[k]));
        acc /= static_cast<double>(hi - lo);
        if (std::abs(acc) < 0.6) continue;  // the neighbourhood disagrees
        drift[i] = std::min(std::max(std::arg(acc) * 100.0 / (2.0 * M_PI), -limit), limit);
    }
    return drift;
}

std::pair<double, std::string> note_strength(const Note& note, const TuneConfig& cfg) {
    double strength = cfg.strength;
    std::vector<std::string> reasons;

    if (std::abs(note.error_to_target_cents()) > cfg.bend_threshold && cfg.strength < 1.0) {
        // Sitting this far off after a musical decode usually means a
        // deliberate bend or blue note; nudge it rather than snap it.
        strength *= 0.55;
        reasons.push_back("bend");
    }
    if (note.core_duration() < 0.09) {
        strength *= std::max(cfg.min_note_strength, 0.5);
        reasons.push_back("short");
    }
    if (note.confidence < 0.5) {
        strength *= 0.7;
        reasons.push_back("low-confidence");
    }
    if (note.vibrato_cents > 90.0) {
        strength *= 0.9;
        reasons.push_back("wide-vibrato");
    }
    if (std::abs(note.drift_cents) > 150.0 && cfg.strength < 1.0) {
        strength *= 0.75;
        reasons.push_back("slide");
    }

    std::string joined;
    for (std::size_t i = 0; i < reasons.size(); ++i) {
        if (i) joined += "+";
        joined += reasons[i];
    }
    return {std::min(std::max(strength, 0.0), 1.0), joined};
}

// Split a pitch curve into a slow component and a vibrato component.
void decompose(const std::vector<double>& cents, double frame_rate, std::vector<double>* slow,
               std::vector<double>* vib) {
    std::vector<double> filled = interpolate_gaps(cents, -1);
    // interpolate_gaps deliberately never extrapolates, so leading and trailing
    // runs are still NaN.  They are filled with the curve's global median, not
    // with the nearest edge value: holding the edge would let one stray frame
    // at the very start bias the low-pass across the whole phrase.
    const double fallback = [&] {
        const double m = nan_median(filled.data(), filled.size());
        return is_finite(m) ? m : 0.0;
    }();
    for (double& v : filled)
        if (!is_finite(v)) v = fallback;

    const double nyq = frame_rate / 2.0;
    if (filled.size() > 24) {
        *slow = sosfiltfilt(butter_lowpass(2, std::min(3.0 / nyq, 0.95)), filled);
        const double hi = std::min(9.0 / nyq, 0.95);
        const double lo = std::min(3.5 / nyq, hi * 0.9);
        *vib = hi > lo ? sosfiltfilt(butter_bandpass(2, lo, hi), filled)
                       : std::vector<double>(filled.size(), 0.0);
    } else {
        *slow = filled;
        *vib = std::vector<double>(filled.size(), 0.0);
    }
}

// Ramp correction in and out so performed glides survive.  A glide only counts
// as expressive at the edge of a phrase: between two connected notes the
// correction stays fully engaged, letting the glide travel between two in-tune
// endpoints instead of being flattened.
std::vector<double> envelope(const Note& note, const std::vector<Note>& notes, std::size_t i,
                             std::size_t a, std::size_t b, std::size_t core_a, std::size_t core_b,
                             double frame_rate, const TuneConfig& cfg) {
    const std::size_t n = b - a;
    std::vector<double> env(n, 1.0);
    if (!cfg.preserve_transitions) return env;

    const std::size_t max_ramp = static_cast<std::size_t>(cfg.transition_max * frame_rate);
    const double prev_gap = i > 0 ? note.start - notes[i - 1].end : 1e9;
    const double next_gap = i + 1 < notes.size() ? notes[i + 1].start - note.end : 1e9;

    if (prev_gap > 0.08) {  // phrase start: preserve the scoop into the note
        std::size_t ramp = core_a > a ? core_a - a : 0;
        ramp = std::min(std::max(ramp, static_cast<std::size_t>(0.012 * frame_rate)), max_ramp);
        ramp = std::min(ramp, std::max<std::size_t>(n / 2, 1));
        if (ramp > 1)
            for (std::size_t k = 0; k < ramp; ++k)
                env[k] = std::pow(static_cast<double>(k) / (ramp - 1), 0.7);
    }
    if (next_gap > 0.08) {  // phrase end: let the release fall away naturally
        std::size_t ramp = b > core_b ? b - core_b : 0;
        ramp = std::min(std::max(ramp, static_cast<std::size_t>(0.010 * frame_rate)), max_ramp);
        ramp = std::min(ramp, std::max<std::size_t>(n / 3, 1));
        if (ramp > 1)
            for (std::size_t k = 0; k < ramp; ++k) {
                const double f = static_cast<double>(k) / (ramp - 1);
                env[n - ramp + k] = std::min(env[n - ramp + k], 1.0 + f * (0.45 - 1.0));
            }
    }
    return env;
}

}  // namespace

TuneConfig TuneConfig::style(const std::string& name) {
    TuneConfig cfg;
    if (name == "natural") return cfg;
    if (name == "transparent") {
        cfg.strength = 0.68;
        cfg.drift_keep = 0.75;
        cfg.vibrato_keep = 1.0;
        return cfg;
    }
    if (name == "tight") {
        cfg.strength = 0.96;
        cfg.drift_keep = 0.25;
        cfg.vibrato_keep = 0.9;
        return cfg;
    }
    if (name == "hard") {
        cfg.strength = 1.0;
        cfg.vibrato_keep = 0.0;
        cfg.drift_keep = 0.0;
        cfg.preserve_transitions = false;
        cfg.smooth_ms = 6.0;
        cfg.min_note_strength = 1.0;
        return cfg;
    }
    throw std::invalid_argument("unknown style: " + name);
}

// Choose a target for every note by decoding the melody as a whole.  The model
// is generative: an intended sequence, drawn from a key prior and a melodic
// interval prior, performed with a slow drift plus per-note error.  There is
// deliberately no term rewarding the *sung* interval - that evidence is already
// in each note's pitch, and counting it twice is what makes a decoder invent
// chromatic notes to explain away two ordinary mistakes.
void assign_targets(NoteSequence& seq, const KeyEstimate& key, const TuneConfig& cfg) {
    std::vector<Note*> notes;
    for (Note& n : seq.notes)
        if (!n.locked) notes.push_back(&n);
    if (notes.empty()) return;

    const double tuning = key.tuning_hz;
    const std::size_t n_notes = notes.size();
    std::vector<double> sung(n_notes);
    std::vector<std::vector<double>> cands(n_notes);
    for (std::size_t i = 0; i < n_notes; ++i) {
        sung[i] = 69.0 + 12.0 * std::log2(std::max(notes[i]->center_hz, 1e-9) / tuning);
        const long base = std::lround(sung[i]);
        for (long c = base - cfg.candidates; c <= base + cfg.candidates; ++c)
            cands[i].push_back(static_cast<double>(c));
    }
    const auto drift = local_drift(sung, cfg.drift_notes, cfg.max_drift_cents);

    auto emission = [&](std::size_t i) {
        const Note& n = *notes[i];
        std::vector<double> score(cands[i].size());
        const double weight = std::min(std::max(n.core_duration() / 0.25, 0.35), 2.0) *
                              std::min(std::max(n.confidence * 1.5, 0.3), 1.0);
        for (std::size_t c = 0; c < cands[i].size(); ++c) {
            const double dist = (sung[i] - cands[i][c]) * 100.0 - drift[i];
            double s = -(dist / cfg.pitch_sigma) * (dist / cfg.pitch_sigma);
            const int pc = ((static_cast<int>(std::lround(cands[i][c])) % 12) + 12) % 12;
            s += cfg.key_weight * std::log(std::max(key.weights[pc], 1e-6) * 12.0);
            s *= weight;
            if (std::abs(dist) > cfg.max_shift_cents) s = -1e6;
            score[c] = s;
        }
        return score;
    };

    std::vector<double> scores = emission(0);
    std::vector<std::vector<std::size_t>> back(n_notes);
    back[0].assign(cands[0].size(), 0);

    for (std::size_t i = 1; i < n_notes; ++i) {
        const auto& prev = cands[i - 1];
        const auto& cur = cands[i];
        // A rest loosens the melodic tie between two notes.
        const double gap = std::max(notes[i]->start - notes[i - 1]->end, 0.0);
        const double tie = std::exp(-gap / 1.2);
        std::vector<double> best(cur.size(), -1e300);
        back[i].assign(cur.size(), 0);
        for (std::size_t c = 0; c < cur.size(); ++c) {
            for (std::size_t p = 0; p < prev.size(); ++p) {
                const double trans = cfg.melodic_weight * melodic_logp(cur[c] - prev[p]) * tie;
                const double total = scores[p] + trans;
                if (total > best[c]) {
                    best[c] = total;
                    back[i][c] = p;
                }
            }
        }
        const auto emit = emission(i);
        scores.resize(cur.size());
        for (std::size_t c = 0; c < cur.size(); ++c) scores[c] = best[c] + emit[c];
    }

    std::vector<std::size_t> path(n_notes, 0);
    path[n_notes - 1] =
        static_cast<std::size_t>(std::max_element(scores.begin(), scores.end()) - scores.begin());
    for (std::size_t i = n_notes - 1; i > 0; --i) path[i - 1] = back[i][path[i]];

    for (std::size_t i = 0; i < n_notes; ++i) {
        Note& note = *notes[i];
        note.has_target = true;
        note.target_midi = cands[i][path[i]];
        note.scale_degree = key.degree_name(note.target_midi);
        const auto decided = note_strength(note, cfg);
        note.strength = decided.first;
        note.reason = decided.second;
        note.correction_cents = -note.error_to_target_cents() * note.strength;
    }
}

// Turn per-note decisions into a smooth per-frame correction in cents.
std::vector<double> build_correction(const NoteSequence& seq, const PitchTrack& track,
                                     const KeyEstimate& key, const TuneConfig& cfg) {
    const double frame_rate = track.frame_rate();
    const std::size_t n_frames = track.f0.size();
    std::vector<double> correction(n_frames, 0.0);
    if (n_frames == 0) return correction;

    std::vector<double> cents(n_frames);
    for (std::size_t i = 0; i < n_frames; ++i)
        cents[i] = is_finite(track.f0[i])
                       ? 1200.0 * std::log2(std::max(track.f0[i], 1e-9) / kCentsRef)
                       : nan_value();
    std::vector<double> slow, vib;
    decompose(cents, frame_rate, &slow, &vib);

    for (std::size_t i = 0; i < seq.notes.size(); ++i) {
        const Note& note = seq.notes[i];
        const double target_hz = note.target_frequency(key.tuning_hz);
        if (note.locked || !(target_hz > 0.0)) continue;

        auto clampf = [&](double t) {
            const long f = std::lround(t * frame_rate);
            return static_cast<std::size_t>(
                std::min<long>(std::max<long>(f, 0), static_cast<long>(n_frames) - 1));
        };
        const std::size_t a = clampf(note.start);
        std::size_t b = std::max(clampf(note.end), a + 1);
        b = std::min(b, n_frames);
        std::size_t core_a = std::min(std::max(clampf(note.core_start), a), b - 1);
        std::size_t core_b = std::min(std::max(clampf(note.core_end), core_a + 1), b);

        const double target_c = 1200.0 * std::log2(target_hz / kCentsRef);
        const double center_c = nan_median(slow.data() + core_a, core_b - core_a);
        if (!is_finite(center_c)) continue;

        const double offset = (target_c - center_c) + note.pitch_offset_cents;
        const double drift_keep = std::min(std::max(cfg.drift_keep * note.drift_scale, 0.0), 1.0);
        const double vib_keep = std::min(std::max(cfg.vibrato_keep * note.vibrato_scale, 0.0), 2.0);
        const auto env = envelope(note, seq.notes, i, a, b, core_a, core_b, frame_rate, cfg);

        for (std::size_t k = a; k < b; ++k) {
            // Move where the note sits; hand back its wobble and its sag.
            const double seg = offset - (1.0 - drift_keep) * (slow[k] - center_c) -
                               (1.0 - vib_keep) * vib[k];
            correction[k] = seg * env[k - a] * note.strength;
        }
    }

    if (cfg.smooth_ms > 0.0) {
        const std::size_t width =
            std::max<std::size_t>(1, static_cast<std::size_t>(cfg.smooth_ms * frame_rate / 1000.0));
        correction = smooth(correction, width);
        for (double& v : correction)
            if (!is_finite(v)) v = 0.0;
    }
    for (double& v : correction)
        v = std::min(std::max(v, -cfg.max_shift_cents), cfg.max_shift_cents);
    return correction;
}

TuneResult tune(NoteSequence& seq, const PitchTrack& track, const KeyEstimate& key,
                const TuneConfig& cfg) {
    assign_targets(seq, key, cfg);
    TuneResult result;
    result.correction_cents = build_correction(seq, track, key, cfg);
    result.ratio.resize(result.correction_cents.size());
    for (std::size_t i = 0; i < result.ratio.size(); ++i)
        result.ratio[i] = std::pow(2.0, result.correction_cents[i] / 1200.0);

    seq.tuning_hz = key.tuning_hz;
    seq.key = key.name();
    result.key_name = key.name();

    double before = 0.0, after = 0.0, max_shift = 0.0;
    std::size_t counted = 0, moved = 0;
    for (const Note& n : seq.notes) {
        if (!n.has_target) continue;
        before += std::abs(n.cents_off());
        after += std::abs(n.error_to_target_cents()) * (1.0 - n.strength);
        max_shift = std::max(max_shift, std::abs(n.correction_cents));
        if (std::abs(n.error_to_target_cents()) > 5.0) ++moved;
        ++counted;
    }
    result.stats["notes"] = static_cast<double>(seq.notes.size());
    result.stats["notes_moved"] = static_cast<double>(moved);
    result.stats["mean_error_before_cents"] = counted ? before / counted : 0.0;
    result.stats["mean_error_after_cents"] = counted ? after / counted : 0.0;
    result.stats["max_shift_cents"] = max_shift;
    result.stats["tuning_hz"] = key.tuning_hz;
    return result;
}

}  // namespace vocalai
