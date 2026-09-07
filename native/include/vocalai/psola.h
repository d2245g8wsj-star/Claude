// Pitch-synchronous overlap-add: pitch shifting that leaves formants alone,
// and waveform-similarity time warping that leaves pitch alone.
#pragma once

#include <cstddef>
#include <vector>

namespace vocalai {

struct EpochTrack {
    std::vector<long> marks;        // sample indices, ascending
    std::vector<double> periods;    // local period in samples at each mark
    std::vector<bool> voiced;       // per sample
};

// One pitch mark per glottal cycle, locked to the tracked F0's phase.
EpochTrack detect_epochs(const std::vector<double>& x, int sample_rate,
                         const std::vector<double>& f0, std::size_t hop,
                         double bandwidth_hz = 70.0);

// Re-synthesise with a frame-rate pitch ratio curve (1.0 leaves pitch alone).
// `time_map`, when given, maps output sample index to input sample index.
std::vector<double> psola_modify(const std::vector<double>& x, int sample_rate,
                                 const EpochTrack& epochs,
                                 const std::vector<double>& pitch_ratio, std::size_t hop,
                                 const std::vector<double>* time_map = nullptr,
                                 std::size_t out_length = 0);

// Time warping at constant pitch. `transients` are input sample positions at
// which the similarity search is clamped so onsets land where they were asked.
std::vector<double> wsola_time_warp(const std::vector<double>& x, int sample_rate,
                                    const std::vector<double>& time_map,
                                    std::size_t frame_len = 0, long tolerance = -1,
                                    const std::vector<double>* transients = nullptr,
                                    long transient_tolerance = -1);

std::vector<double> frames_to_samples(const std::vector<double>& values, std::size_t hop,
                                      std::size_t n_samples);

}  // namespace vocalai
