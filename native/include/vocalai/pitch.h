// Probabilistic monophonic pitch tracking, a pYIN-style estimator.
//
// A direct port of the Python reference: YIN's cumulative mean normalised
// difference function supplies candidates, a Beta prior over the YIN threshold
// turns them into probabilities, and a hidden Markov model decoded with
// Viterbi picks the trajectory that moves the way a larynx actually does.
#pragma once

#include <cstddef>
#include <vector>

#include "vocalai/dsp.h"

namespace vocalai {

struct PitchTrack {
    std::vector<double> f0;           // Hz, NaN where unvoiced
    std::vector<double> voiced_prob;
    std::vector<double> periodicity;
    std::vector<double> times;
    int sample_rate = 44100;
    std::size_t hop = 256;

    std::size_t size() const { return f0.size(); }
    bool voiced(std::size_t i) const { return is_finite(f0[i]); }
    double voiced_ratio() const;
    double frame_rate() const { return static_cast<double>(sample_rate) / hop; }
};

struct PitchConfig {
    double fmin = 65.0;
    double fmax = 1100.0;
    std::size_t frame_len = 0;        // 0 selects by sample rate
    std::size_t hop = 256;
    double bin_cents = 10.0;
    double switch_prob = 0.02;
    double max_step_cents = 250.0;
    std::size_t chunk_frames = 12000;
    std::size_t block_frames = 1024;
};

PitchTrack track_f0(const std::vector<double>& x, int sample_rate,
                    const PitchConfig& cfg = PitchConfig());

// Exposed for testing against the reference implementation.
std::vector<double> cmndf_frame(const double* frame, std::size_t frame_len, std::size_t max_lag);

}  // namespace vocalai
