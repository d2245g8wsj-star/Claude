// The editable note model: analysis proposes, this records, rendering obeys.
#pragma once

#include <string>
#include <vector>

#include "vocalai/dsp.h"

namespace vocalai {

struct Note {
    int index = 0;
    double start = 0.0;          // first frame of the note, transitions included
    double end = 0.0;
    double core_start = 0.0;     // the stable centre, excluding attack and release
    double core_end = 0.0;
    double center_hz = 0.0;
    double amplitude_db = -60.0;
    double peak_db = -60.0;
    double confidence = 1.0;
    double voiced_ratio = 1.0;
    double vibrato_hz = 0.0;
    double vibrato_cents = 0.0;  // half the peak-to-peak modulation depth
    double drift_cents = 0.0;
    double onset_cents = 0.0;

    // Written by the tuning engine.
    bool has_target = false;
    double target_midi = 0.0;
    double correction_cents = 0.0;
    double strength = 1.0;
    std::string scale_degree;
    std::string reason;

    // Manual edits, applied on top of the automatic decision.
    double pitch_offset_cents = 0.0;
    double gain_db = 0.0;
    double vibrato_scale = 1.0;
    double drift_scale = 1.0;
    bool locked = false;

    double duration() const { return end > start ? end - start : 0.0; }
    double core_duration() const { return core_end > core_start ? core_end - core_start : 0.0; }
    double midi() const { return hz_to_midi(center_hz); }
    long nearest_midi() const { return std::lround(midi()); }
    double cents_off() const { return (midi() - static_cast<double>(nearest_midi())) * 100.0; }
    std::string name() const { return note_name(midi()); }
    std::string target_name() const { return has_target ? note_name(target_midi) : "-"; }
    double error_to_target_cents() const {
        return has_target ? (midi() - target_midi) * 100.0 : 0.0;
    }
    // The target in Hz, derived from target_midi so a hand edit always wins.
    double target_frequency(double tuning_hz) const {
        return has_target ? midi_to_hz(target_midi, tuning_hz) : 0.0;
    }
};

struct NoteSequence {
    std::vector<Note> notes;
    int sample_rate = 44100;
    std::size_t hop = 256;
    double tuning_hz = 440.0;
    std::string key;
    std::string source;

    std::size_t size() const { return notes.size(); }
    bool empty() const { return notes.empty(); }
};

std::string to_json(const NoteSequence& seq);
NoteSequence from_json(const std::string& text);

}  // namespace vocalai
