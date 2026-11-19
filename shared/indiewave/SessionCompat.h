#pragma once

/*
    Guards that a new version opens the sessions an older one saved.

    A host finds a plugin in a session by its codes and gives it back the state
    it saved; automation is stored against parameter IDs and normalised values.
    So for a session to carry on as it was, every parameter a released version
    had must still exist with the same ID, range, default and choices, and a
    state saved by that version must load back to the same values.

    tests/compat/ next to each plugin's tests holds what released versions
    looked like:

        parameters.txt      one line per parameter, append only
        state-<v>.bin       a state saved by version <v>, every parameter moved
                            off its default

    run() checks the current build against them. With INDIEWAVE_WRITE_COMPAT=1
    in the environment it also records the current version's state and adds
    any new parameters to the manifest; that is done once, at release.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>

namespace indiewave::compat
{

using Check = std::function<void (bool, const juce::String&)>;
using Factory = std::function<std::unique_ptr<juce::AudioProcessor>()>;

/** Five significant figures: enough to tell ranges apart, few enough that
    the rounding of a platform's maths library does not show. Anything
    smaller than a millionth of `scale` is noise from that rounding (a
    default of 0 on a -100..100 range comes back from the normalised value
    as 1.5e-06 on some machines) and reads as 0. */
inline juce::String number (double v, double scale = 1.0)
{
    if (std::abs (v) < 1.0e-6 * std::max (1.0, std::abs (scale)))
        return "0";
    char text[32];
    std::snprintf (text, sizeof (text), "%.5g", v);
    return text;
}

/** One manifest line: everything about a parameter a session depends on. */
inline juce::String describe (juce::RangedAudioParameter& p)
{
    const auto& r = p.getNormalisableRange();
    juce::String line;
    line << p.getParameterID() << " | ";

    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (&p))
        line << "choice | " << c->choices.joinIntoString (";");
    else if (dynamic_cast<juce::AudioParameterBool*> (&p) != nullptr)
        line << "bool";
    else
        line << (dynamic_cast<juce::AudioParameterInt*> (&p) != nullptr ? "int" : "float")
             << " | " << number (r.start) << ".." << number (r.end)
             << " step " << number (r.interval) << " skew " << number (r.skew)
             << (r.symmetricSkew ? " sym" : "");

    line << " | default " << number (p.convertFrom0to1 (p.getDefaultValue()),
                                     (double) r.end - (double) r.start);
    return line;
}

inline std::map<juce::String, juce::String> manifest (juce::AudioProcessor& processor)
{
    std::map<juce::String, juce::String> lines;
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            lines[ranged->getParameterID()] = describe (*ranged);
    return lines;
}

/** A saved state as a tree, whether it was written as XML or as a binary
    ValueTree. */
inline juce::ValueTree decode (const juce::MemoryBlock& data)
{
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(), (int) data.getSize()))
        return juce::ValueTree::fromXml (*xml);

    return juce::ValueTree::readFromData (data.getData(), data.getSize());
}

inline juce::ValueTree stateOf (juce::AudioProcessor& processor)
{
    juce::MemoryBlock data;
    processor.getStateInformation (data);
    return decode (data);
}

inline std::map<juce::String, juce::var> paramValues (const juce::ValueTree& state)
{
    std::map<juce::String, juce::var> values;
    for (const auto& child : state)
        if (child.hasType ("PARAM"))
            values[child["id"].toString()] = child["value"];
    return values;
}

/** Every parameter moved off its default to a value it can hold exactly. */
inline void moveEverything (juce::AudioProcessor& processor)
{
    int index = 0;
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto d = ranged->getDefaultValue();
            auto target = std::fmod (d + 0.37f + 0.11f * (float) (index++ % 5), 1.0f);
            if (std::abs (target - d) < 0.05f)
                target = std::fmod (target + 0.5f, 1.0f);
            ranged->setValueNotifyingHost (ranged->convertTo0to1 (ranged->convertFrom0to1 (target)));
        }
}

inline void run (const Factory& make, const juce::File& dir, const juce::String& version, const Check& check)
{
    const auto writing = juce::SystemStats::getEnvironmentVariable ("INDIEWAVE_WRITE_COMPAT", {}) == "1";
    dir.createDirectory();

    auto fresh = make();
    const auto now = manifest (*fresh);

    // The manifest: nothing a released version had may change or go.
    const auto file = dir.getChildFile ("parameters.txt");
    std::map<juce::String, juce::String> recorded;
    juce::StringArray order;
    for (const auto& line : juce::StringArray::fromLines (file.loadFileAsString()))
        if (line.isNotEmpty())
        {
            const auto id = line.upToFirstOccurrenceOf (" | ", false, false);
            recorded[id] = line;
            order.add (line);
        }

    for (const auto& [id, line] : recorded)
    {
        const auto it = now.find (id);
        check (it != now.end(), "parameter '" + id + "' still exists");
        if (it != now.end())
            check (it->second == line, "parameter '" + id + "' unchanged: was [" + line + "] now [" + it->second + "]");
    }

    for (const auto& [id, line] : now)
        if (recorded.find (id) == recorded.end())
        {
            if (writing)
                order.add (line);
            else
                check (false, "new parameter '" + id + "' is in the manifest (run with INDIEWAVE_WRITE_COMPAT=1 at release)");
        }

    if (writing)
        file.replaceWithText (order.joinIntoString ("\n") + "\n");

    // This version's state, recorded at release for the versions after it.
    if (writing)
    {
        auto source = make();
        moveEverything (*source);
        juce::MemoryBlock data;
        source->getStateInformation (data);
        dir.getChildFile ("state-" + version + ".bin").replaceWithData (data.getData(), data.getSize());
    }

    // Every recorded state opens with the values and settings it was saved with.
    for (const auto& saved : dir.findChildFiles (juce::File::findFiles, false, "state-*.bin"))
    {
        juce::MemoryBlock data;
        saved.loadFileAsData (data);
        const auto before = decode (data);
        const auto name = saved.getFileNameWithoutExtension();
        check (before.isValid(), name + " can be read");

        auto processor = make();
        processor->setStateInformation (data.getData(), (int) data.getSize());
        const auto after = stateOf (*processor);

        const auto was = paramValues (before), is = paramValues (after);
        for (const auto& [id, value] : was)
        {
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (
                [&]() -> juce::AudioProcessorParameter* { for (auto* q : processor->getParameters())
                    if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (q); r != nullptr && r->getParameterID() == id) return q;
                  return nullptr; }());
            check (p != nullptr, name + ": '" + id + "' is still a parameter");
            if (p == nullptr) continue;
            const auto expected = (float) value;
            const auto actual = p->convertFrom0to1 (p->getValue());
            const auto span = p->getNormalisableRange().end - p->getNormalisableRange().start;
            check (std::abs (actual - expected) <= 1.0e-4f * juce::jmax (1.0f, span),
                   name + ": '" + id + "' opens at " + number (expected) + " (got " + number (actual) + ")");
            check (is.count (id) > 0, name + ": '" + id + "' is saved again");
        }

        for (int i = 0; i < before.getNumProperties(); ++i)
        {
            const auto key = before.getPropertyName (i);
            check (after.getProperty (key) == before.getProperty (key),
                   name + ": setting '" + key.toString() + "' survives (" + before.getProperty (key).toString() + ")");
        }
    }
}

} // namespace indiewave::compat
