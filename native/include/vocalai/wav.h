// RIFF/WAVE reading and writing: 8/16/24/32-bit PCM and 32/64-bit float,
// including WAVE_FORMAT_EXTENSIBLE wrappers.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

namespace vocalai {

struct AudioBuffer {
    std::vector<std::vector<double>> channels;  // channels[c][frame], in [-1, 1]
    int sample_rate = 44100;
    int source_bits = 24;
    bool source_float = false;

    std::size_t frames() const { return channels.empty() ? 0 : channels[0].size(); }
    std::size_t channel_count() const { return channels.size(); }
    double duration() const { return static_cast<double>(frames()) / sample_rate; }
    std::vector<double> mono() const;
};

class WavError : public std::runtime_error {
   public:
    explicit WavError(const std::string& what) : std::runtime_error(what) {}
};

AudioBuffer read_wav(const std::string& path);
void write_wav(const std::string& path, const std::vector<std::vector<double>>& channels,
               int sample_rate, int bits = 24, bool float_format = false);
void write_wav(const std::string& path, const std::vector<double>& mono, int sample_rate,
               int bits = 24, bool float_format = false);

}  // namespace vocalai
