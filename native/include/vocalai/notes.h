// Segment a continuous pitch curve into discrete sung notes.
#pragma once

#include "vocalai/features.h"
#include "vocalai/model.h"
#include "vocalai/pitch.h"

namespace vocalai {

struct SegmentConfig {
    double min_note = 0.055;          // shorter than this is an ornament, not a note
    double min_core = 0.030;
    double split_cents = 62.0;        // pitch step that separates two notes
    double merge_cents = 34.0;
    double max_gap = 0.075;           // unvoiced gap that still keeps a note together
    double center_window = 0.171;     // about one vibrato cycle
    double onset_delta = 0.07;
    double core_tolerance = 40.0;
    double transition_window = 0.09;  // two cues this close describe one boundary
    double max_glide = 0.20;          // a sweep this short is a transition, not a note
    double glide_span = 66.0;
    double glide_monotonic = 0.78;
};

constexpr double kCentsRef = 10.0;
constexpr double kVibratoMinHz = 3.5;
constexpr double kVibratoMaxHz = 9.0;

NoteSequence segment_notes(const PitchTrack& track, const std::vector<double>& audio,
                           const Spectrogram* spec = nullptr,
                           const SegmentConfig& cfg = SegmentConfig(),
                           const std::string& source = "");

}  // namespace vocalai
