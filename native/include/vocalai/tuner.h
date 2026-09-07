// The tuning engine: decide what was meant, then correct it like a person.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "vocalai/key.h"
#include "vocalai/model.h"
#include "vocalai/pitch.h"

namespace vocalai {

struct TuneConfig {
    double strength = 0.88;           // fraction of the error removed on the core
    double vibrato_keep = 1.0;        // 1.0 lets vibrato through untouched
    double drift_keep = 0.45;         // how much slow in-note pitch sag survives
    bool preserve_transitions = true; // keep scoops into and falls out of phrases
    double transition_max = 0.16;
    double key_weight = 1.1;
    double melodic_weight = 1.0;      // pull toward melodies that behave like melodies
    double pitch_sigma = 50.0;        // assumed spread of a singer's pitch error
    int drift_notes = 6;              // window, in notes, for a singer going flat
    double max_drift_cents = 30.0;
    double max_shift_cents = 320.0;
    double min_note_strength = 0.35;
    double bend_threshold = 62.0;
    double smooth_ms = 22.0;
    int candidates = 2;               // semitones searched either side

    static TuneConfig style(const std::string& name);
};

struct TuneResult {
    std::vector<double> correction_cents;  // per frame
    std::vector<double> ratio;             // per frame, 2^(cents/1200)
    std::map<std::string, double> stats;
    std::string key_name;
};

void assign_targets(NoteSequence& seq, const KeyEstimate& key, const TuneConfig& cfg);
std::vector<double> build_correction(const NoteSequence& seq, const PitchTrack& track,
                                     const KeyEstimate& key, const TuneConfig& cfg);
TuneResult tune(NoteSequence& seq, const PitchTrack& track, const KeyEstimate& key,
                const TuneConfig& cfg = TuneConfig());

}  // namespace vocalai
