#include "vocalai/engine.h"

namespace vocalai {

TrackAnalysis analyze_track(const std::vector<double>& mono, int sample_rate,
                            const std::string& name, std::size_t hop,
                            const PitchConfig& pitch_cfg, const SegmentConfig& seg_cfg) {
    TrackAnalysis a;
    a.audio = mono;
    a.sample_rate = sample_rate;
    a.name = name;

    PitchConfig pc = pitch_cfg;
    pc.hop = hop;
    a.pitch = track_f0(mono, sample_rate, pc);
    a.spec = spectrogram(mono, sample_rate, 2048, hop);
    a.notes = segment_notes(a.pitch, mono, &a.spec, seg_cfg, name);
    return a;
}

std::vector<double> render_correction(TrackAnalysis& analysis, const std::vector<double>& ratio,
                                      const std::vector<double>* channel) {
    if (!analysis.epochs_known) {
        analysis.epochs =
            detect_epochs(analysis.audio, analysis.sample_rate, analysis.pitch.f0, analysis.hop());
        analysis.epochs_known = true;
    }
    // Every channel is shifted with the one curve derived from the mono
    // analysis, which is what keeps a stereo image phase-coherent.
    const std::vector<double>& source = channel ? *channel : analysis.audio;
    return psola_modify(source, analysis.sample_rate, analysis.epochs, ratio, analysis.hop());
}

TuneResult tune_track(TrackAnalysis& analysis, const TuneConfig& cfg, const KeyEstimate* key) {
    if (key) {
        analysis.key = *key;
        analysis.key_known = true;
    } else if (!analysis.key_known) {
        analysis.key = estimate_key(analysis.notes);
        analysis.key_known = true;
    }
    return tune(analysis.notes, analysis.pitch, analysis.key, cfg);
}

}  // namespace vocalai
