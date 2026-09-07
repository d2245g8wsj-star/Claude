// Track-level API: analyse a vocal, tune it, render the result.
// This is the surface an ARA document controller drives.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "vocalai/features.h"
#include "vocalai/key.h"
#include "vocalai/notes.h"
#include "vocalai/pitch.h"
#include "vocalai/psola.h"
#include "vocalai/tuner.h"

namespace vocalai {

struct TrackAnalysis {
    std::vector<double> audio;   // analysis-side mono
    int sample_rate = 44100;
    PitchTrack pitch;
    Spectrogram spec;
    NoteSequence notes;
    KeyEstimate key;
    bool key_known = false;
    EpochTrack epochs;
    bool epochs_known = false;
    std::string name;

    std::size_t hop() const { return pitch.hop; }
    double duration() const { return static_cast<double>(audio.size()) / sample_rate; }
};

TrackAnalysis analyze_track(const std::vector<double>& mono, int sample_rate,
                            const std::string& name = "track", std::size_t hop = 256,
                            const PitchConfig& pitch_cfg = PitchConfig(),
                            const SegmentConfig& seg_cfg = SegmentConfig());

// Apply a frame-rate correction curve to one channel.
std::vector<double> render_correction(TrackAnalysis& analysis, const std::vector<double>& ratio,
                                      const std::vector<double>* channel = nullptr);

TuneResult tune_track(TrackAnalysis& analysis, const TuneConfig& cfg,
                      const KeyEstimate* key = nullptr);

}  // namespace vocalai
