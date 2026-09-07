#include "vocalai/model.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace vocalai {
namespace {

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string number(double v) {
    if (!std::isfinite(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.10g", v);
    return buf;
}

// A deliberately small JSON reader: the note file is machine-written and
// re-read by this program, so only flat objects of numbers, strings and
// booleans need to be understood.
struct Parser {
    const std::string& text;
    std::size_t pos = 0;

    explicit Parser(const std::string& t) : text(t) {}

    void skip() {
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) ++pos;
    }
    bool consume(char c) {
        skip();
        if (pos < text.size() && text[pos] == c) {
            ++pos;
            return true;
        }
        return false;
    }
    void expect(char c) {
        if (!consume(c))
            throw std::runtime_error(std::string("expected '") + c + "' in note JSON");
    }
    std::string string_value() {
        expect('"');
        std::string out;
        while (pos < text.size() && text[pos] != '"') {
            if (text[pos] == '\\' && pos + 1 < text.size()) ++pos;
            out.push_back(text[pos++]);
        }
        expect('"');
        return out;
    }
    double number_value() {
        skip();
        const std::size_t start = pos;
        while (pos < text.size() &&
               (std::isdigit(static_cast<unsigned char>(text[pos])) || text[pos] == '-' ||
                text[pos] == '+' || text[pos] == '.' || text[pos] == 'e' || text[pos] == 'E'))
            ++pos;
        if (start == pos) throw std::runtime_error("expected a number in note JSON");
        return std::stod(text.substr(start, pos - start));
    }
    // Skip any value, so unknown keys do not break the reader.
    void skip_value() {
        skip();
        if (pos >= text.size()) return;
        const char c = text[pos];
        if (c == '"') {
            string_value();
        } else if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            int depth = 0;
            while (pos < text.size()) {
                if (text[pos] == '"') {
                    string_value();
                    continue;
                }
                if (text[pos] == c) ++depth;
                if (text[pos] == close) {
                    --depth;
                    ++pos;
                    if (depth == 0) return;
                    continue;
                }
                ++pos;
            }
        } else {
            while (pos < text.size() && text[pos] != ',' && text[pos] != '}' && text[pos] != ']')
                ++pos;
        }
    }
};

}  // namespace

std::string to_json(const NoteSequence& seq) {
    std::ostringstream out;
    out << "{\n  \"sample_rate\": " << seq.sample_rate << ",\n  \"hop\": " << seq.hop
        << ",\n  \"tuning_hz\": " << number(seq.tuning_hz) << ",\n  \"key\": \""
        << escape(seq.key) << "\",\n  \"source\": \"" << escape(seq.source)
        << "\",\n  \"notes\": [\n";
    for (std::size_t i = 0; i < seq.notes.size(); ++i) {
        const Note& n = seq.notes[i];
        out << "    {"
            << "\"index\": " << n.index << ", \"start\": " << number(n.start)
            << ", \"end\": " << number(n.end) << ", \"core_start\": " << number(n.core_start)
            << ", \"core_end\": " << number(n.core_end)
            << ", \"center_hz\": " << number(n.center_hz) << ", \"name\": \"" << n.name()
            << "\", \"midi\": " << number(n.midi())
            << ", \"cents_off\": " << number(n.cents_off())
            << ", \"amplitude_db\": " << number(n.amplitude_db)
            << ", \"confidence\": " << number(n.confidence)
            << ", \"vibrato_hz\": " << number(n.vibrato_hz)
            << ", \"vibrato_cents\": " << number(n.vibrato_cents)
            << ", \"drift_cents\": " << number(n.drift_cents)
            << ", \"has_target\": " << (n.has_target ? "true" : "false")
            << ", \"target_midi\": " << number(n.target_midi) << ", \"target_name\": \""
            << n.target_name() << "\", \"correction_cents\": " << number(n.correction_cents)
            << ", \"strength\": " << number(n.strength) << ", \"scale_degree\": \""
            << escape(n.scale_degree) << "\", \"reason\": \"" << escape(n.reason)
            << "\", \"pitch_offset_cents\": " << number(n.pitch_offset_cents)
            << ", \"vibrato_scale\": " << number(n.vibrato_scale)
            << ", \"drift_scale\": " << number(n.drift_scale)
            << ", \"gain_db\": " << number(n.gain_db) << ", \"locked\": "
            << (n.locked ? "true" : "false") << "}";
        if (i + 1 < seq.notes.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

NoteSequence from_json(const std::string& text) {
    NoteSequence seq;
    Parser p(text);
    p.expect('{');
    while (true) {
        p.skip();
        if (p.consume('}')) break;
        const std::string key = p.string_value();
        p.expect(':');
        if (key == "sample_rate") {
            seq.sample_rate = static_cast<int>(p.number_value());
        } else if (key == "hop") {
            seq.hop = static_cast<std::size_t>(p.number_value());
        } else if (key == "tuning_hz") {
            seq.tuning_hz = p.number_value();
        } else if (key == "key") {
            seq.key = p.string_value();
        } else if (key == "source") {
            seq.source = p.string_value();
        } else if (key == "notes") {
            p.expect('[');
            while (true) {
                p.skip();
                if (p.consume(']')) break;
                p.expect('{');
                Note n;
                while (true) {
                    p.skip();
                    if (p.consume('}')) break;
                    const std::string field = p.string_value();
                    p.expect(':');
                    p.skip();
                    if (field == "index") n.index = static_cast<int>(p.number_value());
                    else if (field == "start") n.start = p.number_value();
                    else if (field == "end") n.end = p.number_value();
                    else if (field == "core_start") n.core_start = p.number_value();
                    else if (field == "core_end") n.core_end = p.number_value();
                    else if (field == "center_hz") n.center_hz = p.number_value();
                    else if (field == "amplitude_db") n.amplitude_db = p.number_value();
                    else if (field == "confidence") n.confidence = p.number_value();
                    else if (field == "vibrato_hz") n.vibrato_hz = p.number_value();
                    else if (field == "vibrato_cents") n.vibrato_cents = p.number_value();
                    else if (field == "drift_cents") n.drift_cents = p.number_value();
                    else if (field == "target_midi") n.target_midi = p.number_value();
                    else if (field == "correction_cents") n.correction_cents = p.number_value();
                    else if (field == "strength") n.strength = p.number_value();
                    else if (field == "pitch_offset_cents") n.pitch_offset_cents = p.number_value();
                    else if (field == "vibrato_scale") n.vibrato_scale = p.number_value();
                    else if (field == "drift_scale") n.drift_scale = p.number_value();
                    else if (field == "gain_db") n.gain_db = p.number_value();
                    else if (field == "has_target") {
                        n.has_target = p.text.compare(p.pos, 4, "true") == 0;
                        p.skip_value();
                    } else if (field == "locked") {
                        n.locked = p.text.compare(p.pos, 4, "true") == 0;
                        p.skip_value();
                    } else if (field == "scale_degree") {
                        n.scale_degree = p.string_value();
                    } else if (field == "reason") {
                        n.reason = p.string_value();
                    } else {
                        p.skip_value();
                    }
                    p.consume(',');
                }
                // A hand-edited file may set target_midi without the flag.
                if (n.target_midi != 0.0) n.has_target = true;
                seq.notes.push_back(n);
                p.consume(',');
            }
        } else {
            p.skip_value();
        }
        p.consume(',');
    }
    return seq;
}

}  // namespace vocalai
