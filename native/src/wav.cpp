#include "vocalai/wav.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace vocalai {
namespace {

constexpr uint16_t kFormatPcm = 0x0001;
constexpr uint16_t kFormatFloat = 0x0003;
constexpr uint16_t kFormatExtensible = 0xFFFE;

uint16_t read_u16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t read_u32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
void put_u16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void put_u32(std::string& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
}

}  // namespace

std::vector<double> AudioBuffer::mono() const {
    if (channels.empty()) return {};
    if (channels.size() == 1) return channels[0];
    std::vector<double> out(frames(), 0.0);
    for (const auto& ch : channels)
        for (std::size_t i = 0; i < out.size() && i < ch.size(); ++i) out[i] += ch[i];
    const double scale = 1.0 / static_cast<double>(channels.size());
    for (double& v : out) v *= scale;
    return out;
}

AudioBuffer read_wav(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw WavError("cannot open " + path);
    std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto* data = reinterpret_cast<const unsigned char*>(raw.data());
    const std::size_t size = raw.size();
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
        throw WavError(path + " is not a RIFF/WAVE file");

    const uint32_t riff_size = read_u32(data + 4);
    std::size_t end = riff_size ? std::min(size, static_cast<std::size_t>(8) + riff_size) : size;

    const unsigned char* fmt = nullptr;
    std::size_t fmt_size = 0;
    const unsigned char* payload = nullptr;
    std::size_t payload_size = 0;

    std::size_t pos = 12;
    while (pos + 8 <= end) {
        const unsigned char* id = data + pos;
        const uint32_t chunk = read_u32(data + pos + 4);
        const std::size_t body = pos + 8;
        const std::size_t stop = std::min(body + chunk, end);
        if (std::memcmp(id, "fmt ", 4) == 0) {
            fmt = data + body;
            fmt_size = stop - body;
        } else if (std::memcmp(id, "data", 4) == 0) {
            payload = data + body;
            payload_size = stop - body;
        }
        pos = body + chunk + (chunk & 1);  // chunks are word aligned
    }
    if (!fmt || fmt_size < 16 || !payload) throw WavError(path + " has no fmt or data chunk");

    uint16_t tag = read_u16(fmt);
    const uint16_t channels = read_u16(fmt + 2);
    const uint32_t rate = read_u32(fmt + 4);
    uint16_t bits = read_u16(fmt + 14);
    if (tag == kFormatExtensible && fmt_size >= 40) {
        const uint16_t valid = read_u16(fmt + 18);
        tag = read_u16(fmt + 24);
        if (valid) bits = std::max(bits, valid);
    }
    if (channels < 1) throw WavError(path + " declares no channels");

    AudioBuffer buf;
    buf.sample_rate = static_cast<int>(rate);
    buf.source_bits = bits;
    buf.source_float = (tag == kFormatFloat);
    buf.channels.assign(channels, {});

    const std::size_t bytes_per = bits / 8;
    if (bytes_per == 0) throw WavError(path + " has an invalid bit depth");
    const std::size_t total = payload_size / (bytes_per * channels);
    for (auto& ch : buf.channels) ch.resize(total);

    for (std::size_t i = 0; i < total; ++i) {
        for (std::size_t c = 0; c < channels; ++c) {
            const unsigned char* p = payload + (i * channels + c) * bytes_per;
            double v = 0.0;
            if (tag == kFormatPcm) {
                switch (bits) {
                    case 8:
                        v = (static_cast<double>(p[0]) - 128.0) / 128.0;
                        break;
                    case 16:
                        v = static_cast<int16_t>(read_u16(p)) / 32768.0;
                        break;
                    case 24: {
                        int32_t s = p[0] | (p[1] << 8) | (p[2] << 16);
                        if (s & 0x800000) s -= 0x1000000;
                        v = s / 8388608.0;
                        break;
                    }
                    case 32:
                        v = static_cast<int32_t>(read_u32(p)) / 2147483648.0;
                        break;
                    default:
                        throw WavError(path + ": unsupported PCM bit depth");
                }
            } else if (tag == kFormatFloat) {
                if (bits == 32) {
                    float f;
                    std::memcpy(&f, p, 4);
                    v = f;
                } else if (bits == 64) {
                    double d;
                    std::memcpy(&d, p, 8);
                    v = d;
                } else {
                    throw WavError(path + ": unsupported float bit depth");
                }
            } else {
                throw WavError(path + ": unsupported WAVE format tag");
            }
            buf.channels[c][i] = v;
        }
    }
    return buf;
}

void write_wav(const std::string& path, const std::vector<std::vector<double>>& channels,
               int sample_rate, int bits, bool float_format) {
    if (channels.empty()) throw WavError("nothing to write");
    const std::size_t frames = channels[0].size();
    const std::size_t n_ch = channels.size();
    if (float_format) bits = 32;

    std::string payload;
    payload.reserve(frames * n_ch * (bits / 8));
    for (std::size_t i = 0; i < frames; ++i) {
        for (std::size_t c = 0; c < n_ch; ++c) {
            const double v = i < channels[c].size() ? channels[c][i] : 0.0;
            if (float_format) {
                const float f = static_cast<float>(v);
                char tmp[4];
                std::memcpy(tmp, &f, 4);
                payload.append(tmp, 4);
                continue;
            }
            // Round first, then clip, so full scale survives as full scale.
            switch (bits) {
                case 16: {
                    double q = std::nearbyint(v * 32768.0);
                    q = std::min(std::max(q, -32768.0), 32767.0);
                    put_u16(payload, static_cast<uint16_t>(static_cast<int16_t>(q)));
                    break;
                }
                case 24: {
                    double q = std::nearbyint(v * 8388608.0);
                    q = std::min(std::max(q, -8388608.0), 8388607.0);
                    const auto s = static_cast<uint32_t>(static_cast<int32_t>(q)) & 0xFFFFFFu;
                    payload.push_back(static_cast<char>(s & 0xFF));
                    payload.push_back(static_cast<char>((s >> 8) & 0xFF));
                    payload.push_back(static_cast<char>((s >> 16) & 0xFF));
                    break;
                }
                case 32: {
                    double q = std::nearbyint(v * 2147483648.0);
                    q = std::min(std::max(q, -2147483648.0), 2147483647.0);
                    put_u32(payload, static_cast<uint32_t>(static_cast<int32_t>(q)));
                    break;
                }
                default:
                    throw WavError("unsupported bit depth for writing");
            }
        }
    }

    const uint16_t block_align = static_cast<uint16_t>(n_ch * bits / 8);
    std::string fmt;
    put_u16(fmt, float_format ? kFormatFloat : kFormatPcm);
    put_u16(fmt, static_cast<uint16_t>(n_ch));
    put_u32(fmt, static_cast<uint32_t>(sample_rate));
    put_u32(fmt, static_cast<uint32_t>(sample_rate) * block_align);
    put_u16(fmt, block_align);
    put_u16(fmt, static_cast<uint16_t>(bits));

    std::string chunks = "fmt ";
    put_u32(chunks, static_cast<uint32_t>(fmt.size()));
    chunks += fmt;
    chunks += "data";
    put_u32(chunks, static_cast<uint32_t>(payload.size()));
    chunks += payload;
    if (payload.size() & 1) chunks.push_back('\0');

    std::string out = "RIFF";
    put_u32(out, static_cast<uint32_t>(4 + chunks.size()));
    out += "WAVE";
    out += chunks;

    std::ofstream file(path, std::ios::binary);
    if (!file) throw WavError("cannot write " + path);
    file.write(out.data(), static_cast<std::streamsize>(out.size()));
}

void write_wav(const std::string& path, const std::vector<double>& mono, int sample_rate,
               int bits, bool float_format) {
    write_wav(path, std::vector<std::vector<double>>{mono}, sample_rate, bits, float_format);
}

}  // namespace vocalai
