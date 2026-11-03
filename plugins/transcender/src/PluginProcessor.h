#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Effects.h"
#include "dsp/Voice.h"

#include <atomic>

namespace transcender
{

inline constexpr int maxVoices = 8;

namespace pid
{
    inline constexpr const char* wave        = "wave";
    inline constexpr const char* detune      = "detune";
    inline constexpr const char* octave      = "octave";
    inline constexpr const char* stereo      = "stereo";
    inline constexpr const char* poly        = "poly";
    inline constexpr const char* glide       = "glide";

    inline constexpr const char* cutoff      = "cutoff";
    inline constexpr const char* resonance   = "resonance";
    inline constexpr const char* envAmount   = "env_amount";

    inline constexpr const char* attack      = "attack";
    inline constexpr const char* decay       = "decay";
    inline constexpr const char* sustain     = "sustain";
    inline constexpr const char* release     = "release";

    inline constexpr const char* lfoRate     = "lfo_rate";
    inline constexpr const char* lfoDepth    = "lfo_depth";

    inline constexpr const char* gateDepth   = "gate_depth";
    inline constexpr const char* gatePattern = "gate_pattern";
    inline constexpr const char* gateRate    = "gate_rate";
    inline constexpr const char* gateLength  = "gate_length";

    inline constexpr const char* delayTime   = "delay_time";
    inline constexpr const char* delayMix    = "delay_mix";
    inline constexpr const char* delayFeedback = "delay_feedback";

    inline constexpr const char* volume      = "volume";
}

/** Delay times as fractions of a quarter note, for the delay time switch. */
struct DelayDivision { const char* name; double beats; };

inline constexpr std::array<DelayDivision, 7> delayDivisions { {
    { "1/16",  0.25 },
    { "1/8T",  1.0 / 3.0 },
    { "1/8",   0.5 },
    { "3/16",  0.75 },
    { "1/4",   1.0 },
    { "3/8",   1.5 },
    { "1/2",   2.0 },
} };

/** Gate step lengths: steps per quarter note. */
inline constexpr std::array<int, 3> gateRates { 2, 4, 8 };

/** One factory sound. Values are in parameter units. */
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

const std::vector<Preset>& factoryPresets();

class TranscenderProcessor final : public juce::AudioProcessor
{
public:
    TranscenderProcessor();
    ~TranscenderProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return true; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 8.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override                         { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    /** The gate step now sounding, for the panel's step lights. */
    int getGateStep() const noexcept       { return gateStep.load (std::memory_order_relaxed); }
    int getActiveVoiceCount() const noexcept;

    /** Tempo used when the host gives none (and by the standalone app). */
    static constexpr double fallbackBpm = 138.0;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void handleMidi (const juce::MidiMessage&);
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void allNotesOff (bool allowTail);
    int  findVoiceFor (int note);

    void renderVoices (float* left, float* right, int numSamples);

    float value (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    std::array<Voice, maxVoices> voices;
    std::array<juce::uint32, maxVoices> voiceAge {};
    juce::uint32 ageCounter { 0 };

    /** Held keys in order, for mono last-note priority. */
    juce::Array<int> heldNotes;
    bool sustainPedal { false };
    juce::Array<int> sustainedNotes;
    int  lastNote { -1 };
    bool wasPoly { true };

    double pitchBend { 0.0 };    // semitones
    double modWheel  { 0.0 };    // 0 .. 1

    TranceGate    gate;
    PingPongDelay delay;
    Lfo           lfo;
    juce::Random  random;

    double freeRunningBeat { 0.0 };
    double currentSampleRate { 48000.0 };

    juce::SmoothedValue<float> outputGain;

    std::atomic<int> gateStep { 0 };
    int currentProgram { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscenderProcessor)
};

} // namespace transcender
