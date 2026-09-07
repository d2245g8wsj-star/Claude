// Key, scale and concert-pitch estimation.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "vocalai/model.h"

namespace vocalai {

// Below this circular concentration the notes are too scattered to imply a
// tuning reference, and inventing one would rename every note in the song.
constexpr double kMinTuningConcentration = 0.45;

struct KeyEstimate {
    int tonic = 0;
    std::string mode = "major";
    std::string scale_name = "major";
    double confidence = 0.0;
    double tuning_hz = 440.0;
    std::array<double, 12> weights{};

    std::string name() const;
    std::vector<int> pitch_classes() const;
    bool contains(double midi) const;
    std::string degree_name(double midi) const;
};

double estimate_tuning(const std::vector<const NoteSequence*>& sequences, double a4 = 440.0);
double estimate_tuning(const NoteSequence& sequence, double a4 = 440.0);

KeyEstimate estimate_key(const std::vector<const NoteSequence*>& sequences,
                         double tuning_hz = 0.0, const std::string& scale_name = "");
KeyEstimate estimate_key(const NoteSequence& sequence, double tuning_hz = 0.0,
                         const std::string& scale_name = "");

// Parse "F# minor", "Bb dorian", "chromatic", ...
KeyEstimate key_from_name(const std::string& text, double tuning_hz = 440.0);

const std::vector<int>* scale_by_name(const std::string& name);

}  // namespace vocalai
