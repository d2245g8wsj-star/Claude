// Command line front end for the native core.  Its main job is to make the
// C++ engine directly comparable with the Python reference on the same files.
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include "vocalai/engine.h"
#include "vocalai/model.h"
#include "vocalai/wav.h"

using namespace vocalai;

namespace {

void usage() {
    std::cout <<
        "vocalai-native - musically aware vocal tuning (C++ core)\n\n"
        "  vocalai-native analyse IN.wav [--key K] [--json OUT.json]\n"
        "  vocalai-native tune IN.wav OUT.wav [--style S] [--key K] [--json OUT.json]\n"
        "  vocalai-native render IN.wav OUT.wav --notes NOTES.json [--style S]\n\n"
        "  --style   transparent | natural | tight | hard   (default: natural)\n"
        "  --key     e.g. \"F# minor\", \"Bb dorian\", \"chromatic\" (default: detected)\n";
}

std::string arg_value(int argc, char** argv, const std::string& flag,
                      const std::string& fallback = "") {
    for (int i = 0; i < argc - 1; ++i)
        if (flag == argv[i]) return argv[i + 1];
    return fallback;
}

void print_notes(const NoteSequence& seq, const TuneResult* result) {
    std::cout << seq.notes.size() << " notes  key=" << (seq.key.empty() ? "?" : seq.key)
              << "  tuning=" << std::fixed << std::setprecision(2) << seq.tuning_hz << " Hz\n";
    for (const Note& n : seq.notes) {
        std::cout << "  [" << std::setw(3) << n.index << "] " << std::setw(7)
                  << std::setprecision(3) << n.start << "s " << std::setw(5) << n.duration()
                  << "s  " << std::setw(4) << n.name() << " " << std::showpos << std::setw(6)
                  << std::setprecision(1) << n.cents_off() << "c" << std::noshowpos;
        if (n.has_target) std::cout << " -> " << std::setw(4) << n.target_name();
        std::cout << "  vib " << std::setw(4) << std::setprecision(0) << n.vibrato_cents << "c/"
                  << std::setprecision(1) << n.vibrato_hz << "Hz";
        if (!n.reason.empty()) std::cout << "  " << n.reason;
        std::cout << "\n";
    }
    if (result) {
        std::cout << "\n";
        for (const auto& kv : result->stats)
            std::cout << std::setw(26) << kv.first << ": " << std::setprecision(3) << kv.second
                      << "\n";
    }
}

int run(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    const std::string command = argv[1];
    const std::string in_path = argv[2];

    const AudioBuffer buf = read_wav(in_path);
    TrackAnalysis analysis = analyze_track(buf.mono(), buf.sample_rate, in_path);

    const std::string key_name = arg_value(argc, argv, "--key");
    KeyEstimate key = key_name.empty() ? estimate_key(analysis.notes)
                                       : key_from_name(key_name, estimate_tuning(analysis.notes));
    const std::string tuning = arg_value(argc, argv, "--tuning");
    if (!tuning.empty()) key.tuning_hz = std::stod(tuning);

    const TuneConfig cfg = TuneConfig::style(arg_value(argc, argv, "--style", "natural"));
    const std::string json_out = arg_value(argc, argv, "--json");

    if (command == "analyse" || command == "analyze") {
        const TuneResult result = tune_track(analysis, cfg, &key);
        print_notes(analysis.notes, &result);
        if (!json_out.empty()) std::ofstream(json_out) << to_json(analysis.notes);
        return 0;
    }

    if (command == "tune" || command == "render") {
        if (argc < 4) {
            usage();
            return 2;
        }
        const std::string out_path = argv[3];
        std::vector<double> ratio;

        if (command == "render") {
            const std::string notes_path = arg_value(argc, argv, "--notes");
            if (notes_path.empty()) {
                std::cerr << "vocalai: render needs --notes\n";
                return 2;
            }
            std::ifstream f(notes_path);
            if (!f) {
                std::cerr << "vocalai: cannot read " << notes_path << "\n";
                return 2;
            }
            std::stringstream ss;
            ss << f.rdbuf();
            NoteSequence edited = from_json(ss.str());
            if (edited.tuning_hz > 0.0) key.tuning_hz = edited.tuning_hz;
            const auto correction = build_correction(edited, analysis.pitch, key, cfg);
            ratio.resize(correction.size());
            for (std::size_t i = 0; i < ratio.size(); ++i)
                ratio[i] = std::pow(2.0, correction[i] / 1200.0);
            analysis.notes = edited;
        } else {
            const TuneResult result = tune_track(analysis, cfg, &key);
            ratio = result.ratio;
            print_notes(analysis.notes, &result);
        }

        // One shared curve across channels keeps a stereo image coherent.
        std::vector<std::vector<double>> out;
        for (const auto& ch : buf.channels) out.push_back(render_correction(analysis, ratio, &ch));
        write_wav(out_path, out, buf.sample_rate, buf.source_bits, buf.source_float);
        std::cout << "wrote " << out_path << "\n";
        if (!json_out.empty()) std::ofstream(json_out) << to_json(analysis.notes);
        return 0;
    }

    usage();
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "vocalai: " << e.what() << "\n";
        return 2;
    }
}
