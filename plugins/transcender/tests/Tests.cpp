/*
    Offline checks for Transcender: that it plays, stops, stays finite at the
    edges of every control, and that the gate, mono mode, presets and saved
    state do what the panel says.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "dsp/Effects.h"

#include <cmath>
#include <iostream>

namespace
{

int failures = 0;
int checks   = 0;

void check (bool condition, const juce::String& what)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::cout << "  FAIL  " << what << std::endl;
    }
}

void section (const juce::String& name) { std::cout << "\n== " << name << std::endl; }

constexpr double rate  = 48000.0;
constexpr int    block = 256;

struct Rendered
{
    std::vector<float> left, right;

    double rms (int from, int to) const
    {
        double sum = 0.0;
        to = juce::jmin (to, (int) left.size());
        for (int i = from; i < to; ++i)
            sum += 0.5 * ((double) left[(size_t) i] * left[(size_t) i] + (double) right[(size_t) i] * right[(size_t) i]);
        return std::sqrt (sum / juce::jmax (1, to - from));
    }

    float peak() const
    {
        float p = 0.0f;
        for (size_t i = 0; i < left.size(); ++i)
            p = juce::jmax (p, std::abs (left[i]), std::abs (right[i]));
        return p;
    }

    bool finite() const
    {
        for (size_t i = 0; i < left.size(); ++i)
            if (! std::isfinite (left[i]) || ! std::isfinite (right[i]))
                return false;
        return true;
    }
};

void set (transcender::TranscenderProcessor& p, const char* id, float value)
{
    auto* parameter = p.apvts.getParameter (id);
    parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
}

/** Plays notes: each event is (sample, note, velocity), velocity 0 is note off. */
Rendered play (transcender::TranscenderProcessor& p, int numSamples,
               std::vector<std::tuple<int, int, float>> events)
{
    Rendered out;
    juce::AudioBuffer<float> buffer (2, block);

    for (int start = 0; start < numSamples; start += block)
    {
        juce::MidiBuffer midi;

        for (auto& [when, note, velocity] : events)
            if (when >= start && when < start + block)
                midi.addEvent (velocity > 0.0f ? juce::MidiMessage::noteOn (1, note, velocity)
                                               : juce::MidiMessage::noteOff (1, note),
                               when - start);

        p.processBlock (buffer, midi);

        for (int i = 0; i < block; ++i)
        {
            out.left.push_back (buffer.getSample (0, i));
            out.right.push_back (buffer.getSample (1, i));
        }
    }

    return out;
}

std::unique_ptr<transcender::TranscenderProcessor> fresh()
{
    auto p = std::make_unique<transcender::TranscenderProcessor>();
    p->setPlayConfigDetails (0, 2, rate, block);
    set (*p, transcender::pid::delayMix, 0.0f);
    set (*p, transcender::pid::gateDepth, 0.0f);
    p->prepareToPlay (rate, block);
    return p;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    using namespace transcender;

    section ("Silence and sound");
    {
        auto p = fresh();
        auto quiet = play (*p, 24000, {});
        check (quiet.peak() <= 0.0f, "no notes, no output");

        auto note = play (*p, 48000, { { 0, 60, 0.8f }, { 24000, 60, 0.0f } });
        check (note.finite(), "a note is finite");
        check (note.rms (2000, 20000) > 0.01, "a held note sounds");
        check (note.peak() < 1.5f, "a single note at the default level stays sensible (peak "
                                       + juce::String (note.peak()) + ")");
        check (note.rms (46000, 48000) < note.rms (2000, 20000) * 0.05, "released note dies away");
        check (p->getActiveVoiceCount() == 0, "voice frees after its release");
    }

    section ("Stereo width");
    {
        auto p = fresh();
        set (*p, pid::stereo, 0.0f);
        auto narrow = play (*p, 12000, { { 0, 64, 1.0f } });
        double diff = 0.0;
        for (size_t i = 2000; i < narrow.left.size(); ++i) diff += std::abs (narrow.left[i] - narrow.right[i]);
        check (diff < 1.0e-3, "stereo 0 is mono");

        auto q = fresh();
        set (*q, pid::stereo, 1.0f);
        auto wide = play (*q, 12000, { { 0, 64, 1.0f } });
        diff = 0.0;
        for (size_t i = 2000; i < wide.left.size(); ++i) diff += std::abs (wide.left[i] - wide.right[i]);
        check (diff > 10.0, "stereo 100 % differs between sides");
    }

    section ("Polyphony and mono");
    {
        auto p = fresh();
        play (*p, 4800, { { 0, 60, 1.0f }, { 10, 64, 1.0f }, { 20, 67, 1.0f } });
        check (p->getActiveVoiceCount() == 3, "poly: three keys, three voices");

        play (*p, 4800, { { 0, 70, 1.0f }, { 1, 71, 1.0f }, { 2, 72, 1.0f }, { 3, 73, 1.0f },
                          { 4, 74, 1.0f }, { 5, 75, 1.0f }, { 6, 76, 1.0f } });
        check (p->getActiveVoiceCount() == maxVoices, "poly: ten keys use all eight voices, no more");

        auto m = fresh();
        set (*m, pid::poly, 0.0f);
        play (*m, 4800, { { 0, 60, 1.0f }, { 100, 64, 1.0f }, { 200, 67, 1.0f } });
        check (m->getActiveVoiceCount() == 1, "mono: one voice whatever is held");
    }

    section ("Trance gate");
    {
        auto p = fresh();
        set (*p, pid::gateDepth, 1.0f);
        set (*p, pid::gatePattern, 1.0f);   // offbeat: steps 1, 3, 5 ... open
        set (*p, pid::gateRate, 1.0f);      // sixteenths
        set (*p, pid::gateLength, 1.0f);
        set (*p, pid::attack, 0.001f);
        set (*p, pid::sustain, 1.0f);

        // No host: free running at the fallback tempo from beat 0.
        auto out = play (*p, 48000, { { 0, 57, 1.0f } });
        const auto stepSamples = (int) std::round (rate * 60.0 / TranscenderProcessor::fallbackBpm / 4.0);
        const auto closed = out.rms (stepSamples * 4 + 400, stepSamples * 5 - 100);
        const auto open   = out.rms (stepSamples * 5 + 400, stepSamples * 6 - 100);
        check (open > 0.01, "an on step is open");
        check (closed < open * 0.02, "an off step is shut at full depth ("
                                          + juce::String (closed / juce::jmax (open, 1e-9), 4) + ")");

        // Every pattern has at least one open step, and none is all closed.
        for (int i = 0; i < (int) gatePatterns.size(); ++i)
        {
            int on = 0;
            for (int s = 0; s < 16; ++s) on += gateStepOn (i, s) ? 1 : 0;
            check (on > 0, juce::String ("pattern has steps: ") + gatePatterns[(size_t) i].name);
        }
    }

    section ("Delay");
    {
        auto p = fresh();
        set (*p, pid::delayMix, 1.0f);
        set (*p, pid::delayFeedback, 0.0f);
        set (*p, pid::delayTime, 4.0f);       // a quarter note
        set (*p, pid::release, 0.005f);
        auto out = play (*p, 48000, { { 0, 60, 1.0f }, { 2400, 60, 0.0f } });
        const auto quarter = (int) std::round (rate * 60.0 / TranscenderProcessor::fallbackBpm);
        check (out.rms (4000, quarter - 200) < 1.0e-4, "silence between the note and its echo");
        check (out.rms (quarter + 200, quarter + 2000) > 0.005, "the echo arrives a quarter note later");
    }

    section ("Edges of every control");
    {
        for (int corner = 0; corner < 2; ++corner)
        {
            auto p = fresh();

            for (auto* parameter : p->getParameters())
                parameter->setValueNotifyingHost (corner == 0 ? 0.0f : 1.0f);

            set (*p, pid::volume, 0.0f);
            auto out = play (*p, 96000, { { 0, 36, 1.0f }, { 0, 108, 1.0f }, { 50, 0, 1.0f }, { 60, 127, 1.0f },
                                          { 48000, 36, 0.0f }, { 48000, 108, 0.0f } });
            check (out.finite(), corner == 0 ? "all controls at minimum stay finite" : "all controls at maximum stay finite");
            check (out.peak() < 20.0f, "and bounded (peak " + juce::String (out.peak()) + ")");
        }
    }

    section ("Presets");
    {
        auto p = fresh();
        check (p->getNumPrograms() == 16, "sixteen factory sounds");

        for (int i = 0; i < p->getNumPrograms(); ++i)
        {
            p->setCurrentProgram (i);
            p->prepareToPlay (rate, block);
            auto out = play (*p, 48000, { { 0, 60, 0.9f }, { 0, 67, 0.9f }, { 24000, 60, 0.0f }, { 24000, 67, 0.0f } });
            const auto name = p->getProgramName (i);
            check (out.finite(), name + " is finite");
            check (out.rms (0, 24000) > 0.003, name + " sounds");
            check (out.peak() < 2.0f, name + " is not wildly loud (peak " + juce::String (out.peak()) + ")");
        }
    }

    section ("Saved state");
    {
        auto p = fresh();
        p->setCurrentProgram (9);
        set (*p, pid::cutoff, 1234.0f);
        juce::MemoryBlock state;
        p->getStateInformation (state);

        auto q = fresh();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check (q->getCurrentProgram() == 9, "program number comes back");
        check (std::abs (q->apvts.getRawParameterValue (pid::cutoff)->load() - 1234.0f) < 0.5f, "a parameter comes back");
    }

    std::cout << "\n" << (checks - failures) << " of " << checks << " checks passed" << std::endl;
    return failures == 0 ? 0 : 1;
}
