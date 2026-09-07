#include "vocalai/key.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <stdexcept>

namespace vocalai {
namespace {

// Krumhansl-Kessler key profiles.
const std::array<double, 12> kMajor = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09,
                                       2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
const std::array<double, 12> kMinor = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53,
                                       2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

const std::map<std::string, std::vector<int>>& scales() {
    static const std::map<std::string, std::vector<int>> table = {
        {"major", {0, 2, 4, 5, 7, 9, 11}},
        {"minor", {0, 2, 3, 5, 7, 8, 10}},
        {"harmonic_minor", {0, 2, 3, 5, 7, 8, 11}},
        {"melodic_minor", {0, 2, 3, 5, 7, 9, 11}},
        {"dorian", {0, 2, 3, 5, 7, 9, 10}},
        {"mixolydian", {0, 2, 4, 5, 7, 9, 10}},
        {"pentatonic_major", {0, 2, 4, 7, 9}},
        {"pentatonic_minor", {0, 3, 5, 7, 10}},
        {"blues", {0, 3, 5, 6, 7, 10}},
        {"chromatic", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    };
    return table;
}

const char* kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Prior probability that a pitch class is the intended note.  Scale tones
// start high, but a chromatic tone the singer actually used a lot is promoted:
// blues thirds and passing tones are deliberate, and forcing them into the
// scale is the classic sound of a bad auto-tune job.
std::array<double, 12> scale_weights(int tonic, const std::string& scale_name,
                                     const std::array<double, 12>& hist) {
    const std::vector<int>* scale = scale_by_name(scale_name);
    std::array<double, 12> w{};
    double total_hist = 0.0;
    for (double v : hist) total_hist += v;
    double sum = 0.0;
    for (int pc = 0; pc < 12; ++pc) {
        const int interval = ((pc - tonic) % 12 + 12) % 12;
        const bool in_scale =
            scale && std::find(scale->begin(), scale->end(), interval) != scale->end();
        w[pc] = in_scale ? 1.0 : 0.16;
        if (total_hist > 0.0) w[pc] += 1.4 * hist[pc] / total_hist;
        sum += w[pc];
    }
    if (sum > 0.0)
        for (double& v : w) v /= sum;
    return w;
}

std::array<double, 12> pitch_class_histogram(const std::vector<const NoteSequence*>& seqs,
                                             double tuning_hz) {
    std::array<double, 12> hist{};
    for (const auto* seq : seqs) {
        if (!seq) continue;
        for (const Note& n : seq->notes) {
            if (n.confidence < 0.35) continue;
            const double midi = 69.0 + 12.0 * std::log2(std::max(n.center_hz, 1e-9) / tuning_hz);
            const int pc = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
            const double loud = std::pow(10.0, n.amplitude_db / 20.0);
            hist[pc] += std::max(n.core_duration(), 0.02) * (0.3 + loud);
        }
    }
    return hist;
}

}  // namespace

const std::vector<int>* scale_by_name(const std::string& name) {
    const auto it = scales().find(name);
    return it == scales().end() ? &scales().at("major") : &it->second;
}

std::string KeyEstimate::name() const {
    return std::string(kNoteNames[((tonic % 12) + 12) % 12]) + " " + mode;
}

std::vector<int> KeyEstimate::pitch_classes() const {
    const std::vector<int>* scale = scale_by_name(scale_name);
    std::vector<int> out;
    for (int d : *scale) out.push_back(((tonic + d) % 12 + 12) % 12);
    std::sort(out.begin(), out.end());
    return out;
}

bool KeyEstimate::contains(double midi) const {
    const int pc = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
    const auto pcs = pitch_classes();
    return std::find(pcs.begin(), pcs.end(), pc) != pcs.end();
}

std::string KeyEstimate::degree_name(double midi) const {
    static const char* roman[] = {"I",  "bII", "II", "bIII", "III", "IV",
                                  "bV", "V",   "bVI", "VI",  "bVII", "VII"};
    const int pc = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
    const int interval = ((pc - tonic) % 12 + 12) % 12;
    const std::vector<int>* scale = scale_by_name(scale_name);
    const bool in_scale = std::find(scale->begin(), scale->end(), interval) != scale->end();
    return in_scale ? roman[interval] : std::string("(") + roman[interval] + ")";
}

double estimate_tuning(const std::vector<const NoteSequence*>& sequences, double a4) {
    std::vector<double> offsets, weights;
    for (const auto* seq : sequences) {
        if (!seq) continue;
        for (const Note& n : seq->notes) {
            if (n.core_duration() < 0.08 || n.confidence < 0.4) continue;
            const double midi = 69.0 + 12.0 * std::log2(std::max(n.center_hz, 1e-9) / a4);
            offsets.push_back((midi - std::lround(midi)) * 100.0);
            weights.push_back(n.core_duration());
        }
    }
    if (offsets.size() < 4) return a4;

    // Every offset is an angle on a circle of 100 cents; the circular mean is
    // robust to individual notes being sharp or flat.
    std::complex<double> resultant(0.0, 0.0);
    double total_w = 0.0;
    for (std::size_t i = 0; i < offsets.size(); ++i) {
        const double angle = offsets[i] * 2.0 * M_PI / 100.0;
        resultant += weights[i] * std::complex<double>(std::cos(angle), std::sin(angle));
        total_w += weights[i];
    }
    resultant /= total_w;

    const double concentration = std::abs(resultant);
    if (concentration < kMinTuningConcentration) return a4;
    // Shrink toward concert pitch in proportion to how tight the cluster is, so
    // a barely-significant reading nudges rather than lurches.
    const double shrink =
        std::min(std::max((concentration - kMinTuningConcentration) / 0.30, 0.0), 1.0);
    double cents = std::arg(resultant) * 100.0 / (2.0 * M_PI) * shrink;
    cents = std::min(std::max(cents, -35.0), 35.0);
    return a4 * std::pow(2.0, cents / 1200.0);
}

double estimate_tuning(const NoteSequence& sequence, double a4) {
    return estimate_tuning(std::vector<const NoteSequence*>{&sequence}, a4);
}

KeyEstimate estimate_key(const std::vector<const NoteSequence*>& sequences, double tuning_hz,
                         const std::string& scale_name) {
    KeyEstimate key;
    key.tuning_hz = tuning_hz > 0.0 ? tuning_hz : estimate_tuning(sequences);
    const auto hist = pitch_class_histogram(sequences, key.tuning_hz);
    double total = 0.0;
    for (double v : hist) total += v;
    if (total <= 0.0) {
        key.weights.fill(1.0 / 12.0);
        return key;
    }

    double mean = total / 12.0;
    std::array<double, 12> x{};
    double x_norm = 0.0;
    for (int i = 0; i < 12; ++i) {
        x[i] = hist[i] - mean;
        x_norm += x[i] * x[i];
    }
    x_norm = std::sqrt(x_norm);

    std::vector<double> all_scores;
    double best_r = -2.0;
    int best_tonic = 0;
    std::string best_mode = "major";
    for (int tonic = 0; tonic < 12; ++tonic) {
        for (int m = 0; m < 2; ++m) {
            const auto& profile = m == 0 ? kMajor : kMinor;
            std::array<double, 12> p{};
            double p_mean = 0.0;
            for (int i = 0; i < 12; ++i) {
                p[i] = profile[((i - tonic) % 12 + 12) % 12];
                p_mean += p[i];
            }
            p_mean /= 12.0;
            double dot = 0.0, p_norm = 0.0;
            for (int i = 0; i < 12; ++i) {
                const double pv = p[i] - p_mean;
                dot += x[i] * pv;
                p_norm += pv * pv;
            }
            p_norm = std::sqrt(p_norm);
            const double r = (x_norm > 0.0 && p_norm > 0.0) ? dot / (x_norm * p_norm) : 0.0;
            all_scores.push_back(r);
            if (r > best_r) {
                best_r = r;
                best_tonic = tonic;
                best_mode = m == 0 ? "major" : "minor";
            }
        }
    }
    std::sort(all_scores.begin(), all_scores.end(), std::greater<double>());
    const double margin = all_scores.size() > 1 ? all_scores[0] - all_scores[1] : 0.0;

    key.tonic = best_tonic;
    key.mode = best_mode;
    key.confidence = std::min(std::max(0.5 * std::max(best_r, 0.0) + 2.5 * margin, 0.0), 1.0);
    key.scale_name = !scale_name.empty() ? scale_name : best_mode;
    key.weights = scale_weights(best_tonic, key.scale_name, hist);
    return key;
}

KeyEstimate estimate_key(const NoteSequence& sequence, double tuning_hz,
                         const std::string& scale_name) {
    return estimate_key(std::vector<const NoteSequence*>{&sequence}, tuning_hz, scale_name);
}

KeyEstimate key_from_name(const std::string& text, double tuning_hz) {
    std::string t;
    for (char c : text) t.push_back(c == '-' ? ' ' : static_cast<char>(std::tolower(c)));
    while (!t.empty() && t.front() == ' ') t.erase(t.begin());
    while (!t.empty() && t.back() == ' ') t.pop_back();

    KeyEstimate key;
    key.tuning_hz = tuning_hz;
    if (t == "chromatic" || t == "none" || t == "off") {
        key.mode = "chromatic";
        key.scale_name = "chromatic";
        key.confidence = 1.0;
        key.weights.fill(1.0 / 12.0);
        return key;
    }

    const std::size_t space = t.find(' ');
    const std::string root = space == std::string::npos ? t : t.substr(0, space);
    std::string mode = space == std::string::npos ? "major" : t.substr(space + 1);
    for (char& c : mode)
        if (c == ' ') c = '_';

    static const std::map<std::string, int> flats = {
        {"db", 1}, {"eb", 3}, {"gb", 6}, {"ab", 8}, {"bb", 10}};
    static const std::map<std::string, int> naturals = {
        {"c", 0}, {"d", 2}, {"e", 4}, {"f", 5}, {"g", 7}, {"a", 9}, {"b", 11}};

    int tonic;
    const auto flat = flats.find(root);
    if (flat != flats.end()) {
        tonic = flat->second;
    } else {
        std::string letter;
        int sharps = 0;
        for (char c : root) {
            if (c == '#') ++sharps;
            else letter.push_back(c);
        }
        const auto nat = naturals.find(letter);
        if (nat == naturals.end()) throw std::invalid_argument("unrecognised key root: " + text);
        tonic = (nat->second + sharps) % 12;
    }

    static const std::map<std::string, std::string> aliases = {
        {"maj", "major"}, {"min", "minor"},          {"m", "minor"},
        {"", "major"},    {"harmonic", "harmonic_minor"}, {"melodic", "melodic_minor"}};
    const auto alias = aliases.find(mode);
    const std::string scale_name = alias != aliases.end() ? alias->second : mode;
    if (scales().find(scale_name) == scales().end())
        throw std::invalid_argument("unknown scale: " + mode);

    key.tonic = tonic;
    key.mode = scale_name.find("minor") != std::string::npos ? "minor" : scale_name;
    key.scale_name = scale_name;
    key.confidence = 1.0;
    key.weights = scale_weights(tonic, scale_name, std::array<double, 12>{});
    return key;
}

}  // namespace vocalai
