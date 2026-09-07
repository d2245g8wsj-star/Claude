// Checks the C++ core against reference vectors produced by the Python engine.
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "reference.h"
#include "vocalai/dsp.h"
#include "vocalai/engine.h"
#include "vocalai/fft.h"
#include "vocalai/filters.h"

using namespace vocalai;

namespace {

void test_fft() {
    test::section("fft");
    const std::vector<double> sig = test::load("signal");
    if (sig.empty()) return;

    const auto spec = rfft(sig, 4096);
    const auto want_re = test::load("rfft_real");
    const auto want_im = test::load("rfft_imag");
    test::check(spec.size() == want_re.size(), "rfft bin count");

    double worst = 0.0;
    for (std::size_t i = 0; i < spec.size() && i < want_re.size(); ++i) {
        worst = std::max(worst, std::abs(spec[i].real() - want_re[i]));
        worst = std::max(worst, std::abs(spec[i].imag() - want_im[i]));
    }
    test::check_close(worst, 0.0, 1e-8, "rfft matches numpy");

    const auto back = irfft(spec, 4096);
    test::check_close(test::max_diff(back, test::load("irfft")), 0.0, 1e-10,
                      "irfft matches numpy");

    // Round trip must return the original signal.
    test::check_close(test::max_diff(back, sig), 0.0, 1e-10, "rfft/irfft round trip");
}

struct Design {
    std::string name;
    Sos sos;
};

std::vector<Design> designs() {
    return {
        {"lp3_0p10", butter_lowpass(3, 0.10)},
        {"lp2_0p35", butter_lowpass(2, 0.35)},
        {"lp4_0p05", butter_lowpass(4, 0.05)},
        {"hp4_0p02", butter_highpass(4, 0.02)},
        {"hp2_0p004", butter_highpass(2, 0.004)},
        {"bp3_0p02_0p04", butter_bandpass(3, 0.02, 0.04)},
        {"bp2_0p10_0p30", butter_bandpass(2, 0.10, 0.30)},
        {"bp4_0p18_0p95", butter_bandpass(4, 0.18, 0.95)},
    };
}

void test_filters() {
    test::section("filters");
    const std::vector<double> sig = test::load("signal");
    if (sig.empty()) return;

    for (const auto& design : designs()) {
        const auto want_sos = test::load("sos_" + design.name);
        test::check(design.sos.size() * 6 == want_sos.size(),
                    design.name + ": section count matches scipy");

        // Section coefficients may be paired and ordered differently from
        // scipy's; what has to agree is the filter they add up to.
        const auto got_filt = sosfilt(design.sos, sig);
        const auto want_filt = test::load("sosfilt_" + design.name);
        test::check_close(test::max_diff(got_filt, want_filt), 0.0, 1e-9,
                          design.name + ": sosfilt matches scipy");

        const auto got_ff = sosfiltfilt(design.sos, sig);
        const auto want_ff = test::load("sosfiltfilt_" + design.name);
        test::check_close(test::max_diff(got_ff, want_ff), 0.0, 1e-9,
                          design.name + ": sosfiltfilt matches scipy");

    }
}

// Zero phase means no group delay at all, which shows up as a correlation peak
// at exactly lag zero.  Comparing a filtered signal against its reversed
// counterpart would NOT test this: with scipy's short default padding, a narrow
// filter's edge transient reaches thousands of samples into the signal.
void test_zero_phase() {
    test::section("zero phase");
    const int n = 8192;
    std::vector<double> sine(n);
    for (int i = 0; i < n; ++i) sine[i] = std::sin(2.0 * M_PI * 0.05 * i);

    const std::vector<Design> mid = {{"lp2_0p35", butter_lowpass(2, 0.35)},
                                     {"bp2_0p02_0p30", butter_bandpass(2, 0.02, 0.30)}};
    for (const auto& design : mid) {
        const auto filtered = sosfiltfilt(design.sos, sine);
        double best = -1e30;
        int best_lag = 99;
        for (int lag = -4; lag <= 4; ++lag) {
            double acc = 0.0;
            for (int i = n / 3; i < 2 * n / 3; ++i) acc += sine[i] * filtered[i + lag];
            if (acc > best) {
                best = acc;
                best_lag = lag;
            }
        }
        test::check(best_lag == 0, design.name + ": zero group delay");
    }
}

// NaN cannot survive a round trip through a raw float dump, so the reference
// writes a sentinel; this compares against that convention.
double sentinel(double v) { return std::isfinite(v) ? v : -999.0; }

void test_dsp() {
    test::section("dsp");
    const auto sig = test::load("signal");
    if (sig.empty()) return;
    const std::vector<double> shortsig(sig.begin(), sig.begin() + 600);

    const auto pad = reflect_pad(shortsig, 37);
    test::check_close(test::max_diff(pad, test::load("reflect_pad_37")), 0.0, 1e-12,
                      "reflect padding matches numpy");

    const Frames frames = frame_signal(shortsig, 512, 128);
    const auto want_rows = test::load("frames_512_128_shape");
    test::check(!want_rows.empty() && frames.rows == static_cast<std::size_t>(want_rows[0]),
                "frame count matches numpy");
    test::check_close(test::max_diff(frames.data, test::load("frames_512_128")), 0.0, 1e-12,
                      "framing matches numpy");

    // Rebuild the reference's holed curve exactly as refgen made it.
    std::vector<double> curve(400);
    for (int i = 0; i < 400; ++i)
        curve[i] = 5.0 * i / 399.0 + 0.4 * std::sin(i * 0.3);
    std::vector<double> holed = curve;
    for (int i = 50; i < 53; ++i) holed[i] = nan_value();
    for (int i = 200; i < 230; ++i) holed[i] = nan_value();
    for (int i = 0; i < 4; ++i) holed[i] = nan_value();

    auto compare = [&](const std::vector<double>& got, const std::string& ref,
                       const std::string& what) {
        std::vector<double> mapped(got.size());
        for (std::size_t i = 0; i < got.size(); ++i) mapped[i] = sentinel(got[i]);
        test::check_close(test::max_diff(mapped, test::load(ref)), 0.0, 1e-10, what);
    };
    compare(holed, "gaps_input", "reference curve reconstructed");
    compare(interpolate_gaps(holed, 5), "gaps_bridged", "interpolate_gaps matches numpy");
    compare(smooth(holed, 9), "smooth_9", "smooth matches numpy");
    compare(median_filter_nan(holed, 7), "median_7", "median filter matches numpy");

    std::vector<double> idx(400);
    for (int i = 0; i < 400; ++i) idx[i] = i;
    const auto xq = test::load("interp_x");
    compare(interp(xq, idx, curve), "interp_y", "interp matches numpy");

    test::check(note_name(69.0) == "A4", "note naming");
    test::check(note_name(60.0) == "C4", "note naming middle C");
    test::check_close(hz_to_midi(440.0), 69.0, 1e-9, "hz to midi");
}

// The stages below are checked against vectors produced by the Python engine,
// which was validated first.  Agreement with it is the bar, not merely that
// the C++ runs without crashing.
void test_pitch() {
    test::section("pitch tracking");
    const auto audio = test::load("voice_audio");
    if (audio.empty()) return;
    const auto pt = track_f0(audio, 44100);
    const auto want = test::load("pitch_f0");
    test::check(pt.f0.size() == want.size(), "frame count matches the reference");

    std::size_t agree = 0, disagree = 0;
    double worst = 0.0;
    for (std::size_t i = 0; i < pt.f0.size() && i < want.size(); ++i) {
        const bool py_voiced = want[i] > 0.0;      // the dump writes -1 for unvoiced
        const bool cpp_voiced = is_finite(pt.f0[i]);
        if (py_voiced != cpp_voiced) {
            ++disagree;
            continue;
        }
        if (py_voiced) {
            worst = std::max(worst, std::abs(1200.0 * std::log2(pt.f0[i] / want[i])));
            ++agree;
        }
    }
    test::check(disagree == 0, "voicing decisions match on every frame");
    test::check(agree > 300, "enough voiced frames to compare");
    test::check_close(worst, 0.0, 1e-6, "f0 matches the reference in cents");
    test::check_close(test::max_diff(pt.voiced_prob, test::load("pitch_voiced_prob")), 0.0, 1e-9,
                      "voiced probability matches");

    const auto frame = test::load("cmndf_frame_40");
    if (!frame.empty()) {
        const auto row = cmndf_frame(frame.data(), 2048, 1024);
        test::check_close(test::max_diff(row, test::load("cmndf_row_40")), 0.0, 1e-10,
                          "difference function matches");
    }
}

void test_notes() {
    test::section("note segmentation");
    const auto audio = test::load("voice_audio");
    if (audio.empty()) return;
    const auto pt = track_f0(audio, 44100);
    const auto seq = segment_notes(pt, audio);
    const auto want = test::load("notes_flat");
    const auto count = test::load("notes_count");
    const std::size_t py_n = count.empty() ? 0 : static_cast<std::size_t>(count[0]);

    test::check(seq.notes.size() == py_n, "note count matches the reference");
    const std::size_t n = std::min(seq.notes.size(), py_n);
    double worst_centre = 0.0, worst_start = 0.0, worst_vib = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double* p = want.data() + i * 9;
        worst_start = std::max(worst_start, std::abs(seq.notes[i].start - p[0]));
        worst_centre =
            std::max(worst_centre, std::abs(1200.0 * std::log2(seq.notes[i].center_hz / p[4])));
        worst_vib = std::max(worst_vib, std::abs(seq.notes[i].vibrato_cents - p[6]));
    }
    test::check_close(worst_centre, 0.0, 1e-6, "note centres match in cents");
    test::check_close(worst_start, 0.0, 0.02, "note starts agree within a couple of frames");
    test::check_close(worst_vib, 0.0, 1.0, "vibrato depth agrees within a cent");
    for (const Note& note : seq.notes) {
        test::check(note.core_start >= note.start && note.core_end <= note.end,
                    "core lies inside its note");
        test::check(note.core_duration() > 0.0, "core has non-zero length");
    }
}

void test_psola() {
    test::section("psola");
    const auto audio = test::load("voice_audio");
    if (audio.empty()) return;
    const auto pt = track_f0(audio, 44100);
    const auto ep = detect_epochs(audio, 44100, pt.f0, pt.hop);

    const auto want_marks = test::load("epoch_marks");
    test::check(ep.marks.size() == want_marks.size(), "epoch count matches the reference");
    double worst = 0.0;
    for (std::size_t i = 0; i < ep.marks.size() && i < want_marks.size(); ++i)
        worst = std::max(worst, std::abs(static_cast<double>(ep.marks[i]) - want_marks[i]));
    test::check_close(worst, 0.0, 0.0, "every pitch mark lands on the same sample");

    // One mark per glottal cycle.  Comparing raw spacings against each other
    // would prove nothing here - the melody spans a fifth, so the period
    // legitimately varies - so each gap is checked against the period the
    // pitch track reports at that very mark.  A doubled mark shows up as a
    // ratio near 2, which is exactly the failure that renders a note an
    // octave down.
    if (ep.marks.size() > 20) {
        std::vector<double> ratios;
        for (std::size_t i = 1; i < ep.marks.size(); ++i) {
            const long gap = ep.marks[i] - ep.marks[i - 1];
            const double expected = ep.periods[i - 1];
            if (gap > 0 && expected > 1.0) ratios.push_back(gap / expected);
        }
        test::check_close(median(ratios), 1.0, 0.05, "one pitch mark per glottal cycle");
        test::check(percentile(ratios, 98) < 1.5, "no doubled or skipped cycles");
    }

    // Shifting must actually move the pitch by the amount requested.
    std::vector<double> ratio(pt.f0.size(), std::pow(2.0, 100.0 / 1200.0));
    const auto shifted = psola_modify(audio, 44100, ep, ratio, pt.hop);
    test::check(shifted.size() == audio.size(), "psola preserves length");
    const auto after = track_f0(shifted, 44100);
    std::vector<double> deltas;
    for (std::size_t i = 0; i < after.f0.size() && i < pt.f0.size(); ++i)
        if (is_finite(after.f0[i]) && is_finite(pt.f0[i]))
            deltas.push_back(1200.0 * std::log2(after.f0[i] / pt.f0[i]));
    test::check_close(median(deltas), 100.0, 5.0, "a 100 cent shift moves the pitch 100 cents");

    // An identity warp has to be an actual no-op at any search tolerance.
    std::vector<double> identity(audio.size());
    for (std::size_t i = 0; i < identity.size(); ++i) identity[i] = static_cast<double>(i);
    for (long tol : {0L, 8L, 352L}) {
        const auto same = wsola_time_warp(audio, 44100, identity, 0, tol);
        test::check_close(test::max_diff(same, audio), 0.0, 1e-9,
                          "identity time warp reproduces the input");
    }
}

void test_tuner() {
    test::section("tuner");
    const auto audio = test::load("voice_audio");
    if (audio.empty()) return;
    auto analysis = analyze_track(audio, 44100, "test");
    const auto key = estimate_key(analysis.notes);
    test::check(key.tuning_hz > 380.0 && key.tuning_hz < 500.0, "concert pitch is plausible");

    const auto result = tune_track(analysis, TuneConfig::style("natural"), &key);
    test::check(result.stats.at("mean_error_after_cents") <
                    result.stats.at("mean_error_before_cents") * 0.5,
                "tuning at least halves the pitch error");
    test::check(result.stats.at("mean_error_after_cents") < 8.0, "residual error is small");

    // Styles must differ in how much they actually correct.
    double previous = -1.0;
    for (const char* style : {"tight", "natural", "transparent"}) {
        auto fresh = analyze_track(audio, 44100, "test");
        const auto r = tune_track(fresh, TuneConfig::style(style), &key);
        const double residual = r.stats.at("mean_error_after_cents");
        test::check(residual > previous, std::string(style) + " leaves more error than the tighter style");
        previous = residual;
    }

    // A locked note must come through untouched.
    auto locked = analyze_track(audio, 44100, "test");
    if (!locked.notes.empty()) {
        locked.notes.notes[0].locked = true;
        assign_targets(locked.notes, key, TuneConfig::style("tight"));
        test::check(!locked.notes.notes[0].has_target, "a locked note is never given a target");
    }
}

void test_note_json() {
    test::section("note model json");
    NoteSequence seq;
    seq.key = "A minor";
    seq.tuning_hz = 441.5;
    Note n;
    n.index = 0;
    n.start = 0.25;
    n.end = 0.80;
    n.core_start = 0.32;
    n.core_end = 0.72;
    n.center_hz = 262.5;
    n.has_target = true;
    n.target_midi = 60.0;
    n.locked = true;
    n.pitch_offset_cents = -12.5;
    n.reason = "bend";
    seq.notes.push_back(n);

    const auto back = from_json(to_json(seq));
    test::check(back.notes.size() == 1, "round trip keeps the note");
    test::check(back.key == "A minor", "round trip keeps the key");
    test::check_close(back.tuning_hz, 441.5, 1e-9, "round trip keeps the tuning");
    test::check(back.notes[0].locked, "round trip keeps the lock");
    test::check_close(back.notes[0].pitch_offset_cents, -12.5, 1e-9, "round trip keeps the edit");
    test::check(back.notes[0].reason == "bend", "round trip keeps the reason");

    // Editing target_midi by hand must win over any cached frequency.
    Note edited;
    edited.center_hz = 262.0;
    edited.has_target = true;
    edited.target_midi = 72.0;
    test::check_close(edited.target_frequency(440.0), 523.2511, 1e-3,
                      "target frequency follows target_midi");
}

}  // namespace

int main() {
    test_dsp();
    test_fft();
    test_filters();
    test_zero_phase();
    test_pitch();
    test_notes();
    test_psola();
    test_tuner();
    test_note_json();
    return test::report();
}
