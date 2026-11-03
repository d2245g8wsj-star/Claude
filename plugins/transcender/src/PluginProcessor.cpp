#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace transcender
{

namespace
{
    using Range = juce::NormalisableRange<float>;

    Range logRange (float low, float high)
    {
        Range r (low, high);
        r.setSkewForCentre (std::sqrt (low * high));
        return r;
    }

    Range timeRange (float low, float high, float centre)
    {
        Range r (low, high);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String percent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }

    juce::String seconds (float v, int)
    {
        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms"
                        : juce::String (v, 2) + " s";
    }

    juce::String hertz (float v, int)
    {
        return v < 1000.0f ? juce::String (v, v < 10.0f ? 2 : (v < 100.0f ? 1 : 0)) + " Hz"
                           : juce::String (v / 1000.0f, 2) + " kHz";
    }

    juce::String decibels (float v, int) { return juce::String (v, 1) + " dB"; }

    juce::StringArray choiceNames (auto& table)
    {
        juce::StringArray names;
        for (auto& entry : table) names.add (entry.name);
        return names;
    }
}

// ---------------------------------------------------------------------------
// Factory sounds. The names are the sixteen programs of the original
// Transcender SE; the settings are written for this engine.

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> presets {
        { "Trance lead 1",  { { pid::detune, 0.45f }, { pid::octave, 0.55f }, { pid::stereo, 0.8f },
                              { pid::cutoff, 5200.0f }, { pid::resonance, 0.25f }, { pid::envAmount, 0.25f },
                              { pid::attack, 0.004f }, { pid::decay, 0.6f }, { pid::sustain, 0.75f }, { pid::release, 0.35f },
                              { pid::gateDepth, 1.0f }, { pid::gatePattern, 4.0f }, { pid::gateRate, 1.0f }, { pid::gateLength, 0.6f },
                              { pid::delayTime, 3.0f }, { pid::delayMix, 0.35f }, { pid::delayFeedback, 0.45f } } },
        { "Trance lead 2",  { { pid::detune, 0.6f }, { pid::octave, 0.8f }, { pid::stereo, 1.0f },
                              { pid::cutoff, 3800.0f }, { pid::resonance, 0.35f }, { pid::envAmount, 0.35f },
                              { pid::attack, 0.002f }, { pid::decay, 0.35f }, { pid::sustain, 0.6f }, { pid::release, 0.25f },
                              { pid::lfoRate, 0.25f }, { pid::lfoDepth, 0.2f },
                              { pid::gateDepth, 0.85f }, { pid::gatePattern, 3.0f }, { pid::gateRate, 1.0f }, { pid::gateLength, 0.5f },
                              { pid::delayTime, 3.0f }, { pid::delayMix, 0.4f }, { pid::delayFeedback, 0.5f } } },
        { "Timbaland",      { { pid::wave, 1.0f }, { pid::detune, 0.15f }, { pid::octave, 0.3f }, { pid::stereo, 0.4f },
                              { pid::cutoff, 900.0f }, { pid::resonance, 0.55f }, { pid::envAmount, 0.45f },
                              { pid::attack, 0.001f }, { pid::decay, 0.18f }, { pid::sustain, 0.0f }, { pid::release, 0.12f },
                              { pid::delayTime, 2.0f }, { pid::delayMix, 0.2f }, { pid::delayFeedback, 0.3f } } },
        { "Plucked string", { { pid::detune, 0.2f }, { pid::octave, 0.35f }, { pid::stereo, 0.6f },
                              { pid::cutoff, 1400.0f }, { pid::resonance, 0.15f }, { pid::envAmount, 0.55f },
                              { pid::attack, 0.001f }, { pid::decay, 0.45f }, { pid::sustain, 0.0f }, { pid::release, 0.4f },
                              { pid::delayTime, 3.0f }, { pid::delayMix, 0.25f }, { pid::delayFeedback, 0.35f } } },
        { "Warm pad",       { { pid::detune, 0.5f }, { pid::octave, 0.35f }, { pid::stereo, 1.0f },
                              { pid::cutoff, 1800.0f }, { pid::resonance, 0.1f }, { pid::envAmount, 0.1f },
                              { pid::attack, 1.2f }, { pid::decay, 1.5f }, { pid::sustain, 0.85f }, { pid::release, 2.5f },
                              { pid::lfoRate, 0.15f }, { pid::lfoDepth, 0.25f },
                              { pid::delayTime, 4.0f }, { pid::delayMix, 0.25f }, { pid::delayFeedback, 0.4f } } },
        { "VCS-3",          { { pid::wave, 2.0f }, { pid::detune, 0.3f }, { pid::octave, 0.6f }, { pid::stereo, 0.5f },
                              { pid::cutoff, 2400.0f }, { pid::resonance, 0.7f }, { pid::envAmount, -0.2f },
                              { pid::attack, 0.05f }, { pid::decay, 0.8f }, { pid::sustain, 0.7f }, { pid::release, 0.8f },
                              { pid::lfoRate, 5.5f }, { pid::lfoDepth, 0.35f },
                              { pid::delayTime, 5.0f }, { pid::delayMix, 0.3f }, { pid::delayFeedback, 0.55f } } },
        { "Mono lead",      { { pid::poly, 0.0f }, { pid::glide, 0.08f }, { pid::detune, 0.25f }, { pid::octave, 0.45f },
                              { pid::stereo, 0.5f }, { pid::cutoff, 3000.0f }, { pid::resonance, 0.35f }, { pid::envAmount, 0.3f },
                              { pid::attack, 0.003f }, { pid::decay, 0.4f }, { pid::sustain, 0.7f }, { pid::release, 0.2f },
                              { pid::lfoRate, 5.0f }, { pid::lfoDepth, 0.05f },
                              { pid::delayTime, 3.0f }, { pid::delayMix, 0.25f }, { pid::delayFeedback, 0.35f } } },
        { "Bright lead",    { { pid::detune, 0.55f }, { pid::octave, 1.0f }, { pid::stereo, 0.9f },
                              { pid::cutoff, 11000.0f }, { pid::resonance, 0.1f }, { pid::envAmount, 0.0f },
                              { pid::attack, 0.002f }, { pid::decay, 0.3f }, { pid::sustain, 0.9f }, { pid::release, 0.3f },
                              { pid::delayTime, 3.0f }, { pid::delayMix, 0.3f }, { pid::delayFeedback, 0.4f } } },
        { "Digital bass",   { { pid::poly, 0.0f }, { pid::wave, 1.0f }, { pid::detune, 0.1f }, { pid::octave, 0.25f },
                              { pid::stereo, 0.15f }, { pid::cutoff, 600.0f }, { pid::resonance, 0.45f }, { pid::envAmount, 0.5f },
                              { pid::attack, 0.001f }, { pid::decay, 0.22f }, { pid::sustain, 0.35f }, { pid::release, 0.08f } } },
        { "Moog bass",      { { pid::poly, 0.0f }, { pid::glide, 0.03f }, { pid::detune, 0.08f }, { pid::octave, 0.0f },
                              { pid::stereo, 0.0f }, { pid::cutoff, 320.0f }, { pid::resonance, 0.6f }, { pid::envAmount, 0.45f },
                              { pid::attack, 0.001f }, { pid::decay, 0.3f }, { pid::sustain, 0.4f }, { pid::release, 0.1f } } },
        { "Sweep pad",      { { pid::detune, 0.65f }, { pid::octave, 0.5f }, { pid::stereo, 1.0f },
                              { pid::cutoff, 700.0f }, { pid::resonance, 0.55f }, { pid::envAmount, 0.15f },
                              { pid::attack, 0.8f }, { pid::decay, 2.0f }, { pid::sustain, 0.8f }, { pid::release, 3.0f },
                              { pid::lfoRate, 0.08f }, { pid::lfoDepth, 0.8f },
                              { pid::delayTime, 5.0f }, { pid::delayMix, 0.3f }, { pid::delayFeedback, 0.5f } } },
        { "My Love",        { { pid::detune, 0.4f }, { pid::octave, 0.65f }, { pid::stereo, 0.85f },
                              { pid::cutoff, 2600.0f }, { pid::resonance, 0.3f }, { pid::envAmount, 0.2f },
                              { pid::attack, 0.01f }, { pid::decay, 0.5f }, { pid::sustain, 0.8f }, { pid::release, 0.6f },
                              { pid::gateDepth, 0.9f }, { pid::gatePattern, 1.0f }, { pid::gateRate, 1.0f }, { pid::gateLength, 0.75f },
                              { pid::delayTime, 3.0f }, { pid::delayMix, 0.35f }, { pid::delayFeedback, 0.45f } } },
        { "Percusive bass", { { pid::poly, 0.0f }, { pid::detune, 0.12f }, { pid::octave, 0.2f }, { pid::stereo, 0.1f },
                              { pid::cutoff, 450.0f }, { pid::resonance, 0.3f }, { pid::envAmount, 0.6f },
                              { pid::attack, 0.001f }, { pid::decay, 0.15f }, { pid::sustain, 0.0f }, { pid::release, 0.1f } } },
        { "Drone bass",     { { pid::poly, 0.0f }, { pid::detune, 0.35f }, { pid::octave, 0.4f }, { pid::stereo, 0.6f },
                              { pid::cutoff, 380.0f }, { pid::resonance, 0.4f }, { pid::envAmount, 0.0f },
                              { pid::attack, 0.3f }, { pid::decay, 1.0f }, { pid::sustain, 1.0f }, { pid::release, 1.5f },
                              { pid::lfoRate, 0.12f }, { pid::lfoDepth, 0.4f } } },
        { "Ocean",          { { pid::detune, 1.0f }, { pid::octave, 0.3f }, { pid::stereo, 1.0f },
                              { pid::cutoff, 500.0f }, { pid::resonance, 0.2f }, { pid::envAmount, 0.1f },
                              { pid::attack, 2.5f }, { pid::decay, 3.0f }, { pid::sustain, 0.9f }, { pid::release, 5.0f },
                              { pid::lfoRate, 0.06f }, { pid::lfoDepth, 1.0f },
                              { pid::delayTime, 6.0f }, { pid::delayMix, 0.4f }, { pid::delayFeedback, 0.65f } } },
        { "Tinkabell",      { { pid::wave, 2.0f }, { pid::detune, 0.15f }, { pid::octave, 1.0f }, { pid::stereo, 0.9f },
                              { pid::cutoff, 7000.0f }, { pid::resonance, 0.2f }, { pid::envAmount, 0.1f },
                              { pid::attack, 0.001f }, { pid::decay, 0.6f }, { pid::sustain, 0.0f }, { pid::release, 0.9f },
                              { pid::delayTime, 3.0f }, { pid::delayMix, 0.45f }, { pid::delayFeedback, 0.55f } } },
    };

    return presets;
}

// ---------------------------------------------------------------------------

TranscenderProcessor::TranscenderProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Transcender", createParameterLayout())
{
    setCurrentProgram (0);
}

juce::AudioProcessorValueTreeState::ParameterLayout TranscenderProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto add = [&layout] (auto parameter) { layout.add (std::move (parameter)); };
    auto floatParam = [] (const char* id, const char* name, Range range, float def,
                          std::function<String (float, int)> text)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, def,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text)));
    };

    add (std::make_unique<AudioParameterChoice> (ParameterID { pid::wave, 1 }, "Waveform",
                                                 StringArray { "Saw", "Square", "Triangle", "Sine" }, 0));
    add (floatParam (pid::detune, "Detune", Range (0.0f, 1.0f), 0.4f, percent));
    add (floatParam (pid::octave, "Dual octave", Range (0.0f, 1.0f), 0.5f, percent));
    add (floatParam (pid::stereo, "Stereo", Range (0.0f, 1.0f), 0.7f, percent));
    add (std::make_unique<AudioParameterBool> (ParameterID { pid::poly, 1 }, "Poly", true));
    add (floatParam (pid::glide, "Glide", timeRange (0.0f, 2.0f, 0.2f), 0.0f, seconds));

    add (floatParam (pid::cutoff, "Filter cutoff", logRange (40.0f, 18000.0f), 6000.0f, hertz));
    add (floatParam (pid::resonance, "Filter resonance", Range (0.0f, 1.0f), 0.2f, percent));
    add (floatParam (pid::envAmount, "Filter envelope", Range (-1.0f, 1.0f), 0.2f, percent));

    add (floatParam (pid::attack,  "Env attack",  timeRange (0.001f, 5.0f, 0.3f), 0.005f, seconds));
    add (floatParam (pid::decay,   "Env decay",   timeRange (0.005f, 5.0f, 0.5f), 0.5f, seconds));
    add (floatParam (pid::sustain, "Env sustain", Range (0.0f, 1.0f), 0.8f, percent));
    add (floatParam (pid::release, "Env release", timeRange (0.005f, 8.0f, 0.6f), 0.3f, seconds));

    add (floatParam (pid::lfoRate,  "LFO speed", logRange (0.03f, 20.0f), 0.5f, hertz));
    add (floatParam (pid::lfoDepth, "LFO depth", Range (0.0f, 1.0f), 0.0f, percent));

    add (floatParam (pid::gateDepth, "Trance gate", Range (0.0f, 1.0f), 0.0f, percent));
    add (std::make_unique<AudioParameterChoice> (ParameterID { pid::gatePattern, 1 }, "Gate pattern",
                                                 choiceNames (gatePatterns), 4));
    add (std::make_unique<AudioParameterChoice> (ParameterID { pid::gateRate, 1 }, "Gate rate",
                                                 StringArray { "1/8", "1/16", "1/32" }, 1));
    add (floatParam (pid::gateLength, "Gate length", Range (0.1f, 1.0f), 0.6f, percent));

    add (std::make_unique<AudioParameterChoice> (ParameterID { pid::delayTime, 1 }, "Delay time",
                                                 choiceNames (delayDivisions), 3));
    add (floatParam (pid::delayMix, "Delay level", Range (0.0f, 1.0f), 0.3f, percent));
    add (floatParam (pid::delayFeedback, "Delay feedback", Range (0.0f, 0.9f), 0.4f, percent));

    add (floatParam (pid::volume, "Volume", Range (-36.0f, 6.0f), -6.0f, decibels));

    return layout;
}

// ---------------------------------------------------------------------------

int TranscenderProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const juce::String TranscenderProcessor::getProgramName (int index)
{
    const auto& presets = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) presets.size()) ? juce::String (presets[(size_t) index].name)
                                                                  : juce::String();
}

void TranscenderProcessor::setCurrentProgram (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return;

    currentProgram = index;

    // Every parameter goes back to its default first, so a preset is the
    // same sound whatever was loaded before it. Volume is left alone.
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() != pid::volume)
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [id, v] : presets[(size_t) index].values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
}

// ---------------------------------------------------------------------------

void TranscenderProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;

    for (auto& v : voices)
        v.prepare (sampleRate);

    gate.prepare (sampleRate);
    delay.prepare (sampleRate, 5.0);
    outputGain.reset (sampleRate, 0.03);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (value (pid::volume)));

    heldNotes.clear();
    sustainedNotes.clear();
    lastNote = -1;
}

bool TranscenderProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

int TranscenderProcessor::getActiveVoiceCount() const noexcept
{
    int count = 0;
    for (auto& v : voices)
        if (v.isActive()) ++count;
    return count;
}

// ---------------------------------------------------------------------------
// Notes

int TranscenderProcessor::findVoiceFor (int note)
{
    // The same key again reuses its own voice.
    for (int i = 0; i < maxVoices; ++i)
        if (voices[(size_t) i].isActive() && voices[(size_t) i].getNote() == note)
            return i;

    for (int i = 0; i < maxVoices; ++i)
        if (! voices[(size_t) i].isActive())
            return i;

    // Steal: the oldest released voice, otherwise the oldest of all.
    int best = -1;
    for (int pass = 0; pass < 2 && best < 0; ++pass)
    {
        juce::uint32 oldest = std::numeric_limits<juce::uint32>::max();

        for (int i = 0; i < maxVoices; ++i)
        {
            if (pass == 0 && voices[(size_t) i].isHeld())
                continue;

            if (voiceAge[(size_t) i] < oldest)
            {
                oldest = voiceAge[(size_t) i];
                best = i;
            }
        }
    }

    return juce::jmax (0, best);
}

void TranscenderProcessor::noteOn (int note, float velocity)
{
    heldNotes.removeFirstMatchingValue (note);
    heldNotes.add (note);
    sustainedNotes.removeFirstMatchingValue (note);

    const bool poly = value (pid::poly) > 0.5f;

    if (poly)
    {
        const auto index = findVoiceFor (note);
        auto& voice = voices[(size_t) index];

        // Glide in poly mode starts the new voice from the last note played.
        const bool glide = value (pid::glide) > 0.0005f && lastNote >= 0;
        if (glide && ! voice.isActive())
        {
            voice.start (lastNote, velocity, true, random);
            voice.glideTo (note);
        }
        else
        {
            voice.start (note, velocity, true, random);
        }

        voiceAge[(size_t) index] = ++ageCounter;
    }
    else
    {
        auto& voice = voices[0];
        // Legato: a key pressed while another is held only moves the pitch.
        const bool legato = voice.isActive() && heldNotes.size() > 1;

        if (legato)
            voice.glideTo (note);
        else if (voice.isActive() && value (pid::glide) > 0.0005f)
        {
            voice.start (voice.getNote(), velocity, true, random);
            voice.glideTo (note);
        }
        else
            voice.start (note, velocity, true, random);
    }

    lastNote = note;
}

void TranscenderProcessor::noteOff (int note)
{
    heldNotes.removeFirstMatchingValue (note);

    if (sustainPedal)
    {
        sustainedNotes.addIfNotAlreadyThere (note);
        return;
    }

    const bool poly = value (pid::poly) > 0.5f;

    if (poly)
    {
        for (auto& v : voices)
            if (v.isActive() && v.isHeld() && v.getNote() == note)
                v.stop (true);
    }
    else
    {
        auto& voice = voices[0];

        if (! heldNotes.isEmpty())
            voice.glideTo (heldNotes.getLast());   // back to the key still down
        else if (voice.getNote() == note)
            voice.stop (true);
    }
}

void TranscenderProcessor::allNotesOff (bool allowTail)
{
    heldNotes.clear();
    sustainedNotes.clear();

    for (auto& v : voices)
        v.stop (allowTail);
}

void TranscenderProcessor::handleMidi (const juce::MidiMessage& m)
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isPitchWheel())
        pitchBend = (m.getPitchWheelValue() - 8192) / 8192.0 * 2.0;
    else if (m.isController())
    {
        const auto cc = m.getControllerNumber();

        if (cc == 1)
            modWheel = m.getControllerValue() / 127.0;
        else if (cc == 64)
        {
            sustainPedal = m.getControllerValue() >= 64;

            if (! sustainPedal)
            {
                const auto released = sustainedNotes;
                sustainedNotes.clear();
                for (auto n : released)
                    if (! heldNotes.contains (n))
                        noteOff (n);
            }
        }
        else if (cc == 120)
            allNotesOff (false);
        else if (cc == 123)
            allNotesOff (true);
    }
    else if (m.isAllNotesOff())
        allNotesOff (true);
    else if (m.isAllSoundOff())
        allNotesOff (false);
}

// ---------------------------------------------------------------------------

void TranscenderProcessor::renderVoices (float* left, float* right, int numSamples)
{
    VoiceSettings s;
    s.wave         = (Wave) juce::roundToInt (value (pid::wave));
    s.detuneCents  = 50.0 * std::pow ((double) value (pid::detune), 1.5);
    s.octaveLevel  = value (pid::octave);
    s.stereoWidth  = value (pid::stereo);
    s.cutoffHz     = value (pid::cutoff);
    s.resonance    = value (pid::resonance);
    s.envToFilter  = value (pid::envAmount);
    s.glideSeconds = value (pid::glide);
    s.pitchBendSemis = pitchBend;

    const auto depth = juce::jlimit (0.0, 1.0, (double) value (pid::lfoDepth) + modWheel);
    s.lfoOctaves = 3.0 * depth * lfo.advance (value (pid::lfoRate), numSamples, currentSampleRate);

    juce::ADSR::Parameters env { value (pid::attack), value (pid::decay), value (pid::sustain), value (pid::release) };

    for (auto& v : voices)
    {
        v.setEnvelope (env);
        v.render (s, left, right, numSamples);
    }
}

void TranscenderProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    buffer.clear();

    if (numSamples == 0)
        return;

    // A change of the poly switch starts clean rather than leaving voices hung.
    const bool poly = value (pid::poly) > 0.5f;
    if (poly != wasPoly)
    {
        allNotesOff (true);
        wasPoly = poly;
    }

    // Tempo and position from the host, or a free running clock.
    double bpm = fallbackBpm;
    double beat = freeRunningBeat;
    bool hostPosition = false;

    if (auto* head = getPlayHead())
    {
        if (auto position = head->getPosition())
        {
            if (auto hostBpm = position->getBpm())
                bpm = juce::jlimit (30.0, 300.0, *hostBpm);

            if (position->getIsPlaying())
                if (auto ppq = position->getPpqPosition())
                {
                    beat = *ppq;
                    hostPosition = true;
                }
        }
    }

    // Render in slices between MIDI events so notes start on their sample.
    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    juce::HeapBlock<float> scratch;
    if (right == nullptr)
    {
        scratch.allocate ((size_t) numSamples, true);
        right = scratch.get();
    }

    int position = 0;

    for (const auto metadata : midi)
    {
        const auto when = juce::jlimit (0, numSamples, metadata.samplePosition);

        if (when > position)
        {
            renderVoices (left + position, right + position, when - position);
            position = when;
        }

        handleMidi (metadata.getMessage());
    }

    if (position < numSamples)
        renderVoices (left + position, right + position, numSamples - position);

    // Gate, then echo, so the repeats fill the gaps the gate cuts.
    const auto gateDepth = (double) value (pid::gateDepth);
    if (gateDepth > 0.0001)
    {
        const auto rate = gateRates[(size_t) juce::jlimit (0, 2, juce::roundToInt (value (pid::gateRate)))];
        gateStep.store (gate.process (left, right, numSamples, beat, bpm, rate,
                                      juce::roundToInt (value (pid::gatePattern)), gateDepth,
                                      value (pid::gateLength)),
                        std::memory_order_relaxed);
    }
    else
    {
        gateStep.store (-1, std::memory_order_relaxed);
    }

    const auto mix = (double) value (pid::delayMix);
    const auto division = delayDivisions[(size_t) juce::jlimit (0, (int) delayDivisions.size() - 1,
                                                                juce::roundToInt (value (pid::delayTime)))];
    delay.process (left, right, numSamples, division.beats * 60.0 / bpm, mix, value (pid::delayFeedback));

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (value (pid::volume)));

    for (int i = 0; i < numSamples; ++i)
    {
        const auto g = outputGain.getNextValue();
        left[i]  *= g;
        right[i] *= g;
    }

    if (buffer.getNumChannels() == 1)
        for (int i = 0; i < numSamples; ++i)
            left[i] = 0.5f * (left[i] + right[i]);

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto beatsThisBlock = (double) numSamples * bpm / (60.0 * currentSampleRate);
    freeRunningBeat = (hostPosition ? beat : freeRunningBeat) + beatsThisBlock;
}

// ---------------------------------------------------------------------------

void TranscenderProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destination);
}

void TranscenderProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto state = juce::ValueTree::fromXml (*xml);
            currentProgram = juce::jlimit (0, getNumPrograms() - 1, (int) state.getProperty ("program", 0));
            apvts.replaceState (state);
        }
    }
}

juce::AudioProcessorEditor* TranscenderProcessor::createEditor()
{
    return new TranscenderEditor (*this);
}

} // namespace transcender

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new transcender::TranscenderProcessor();
}
