#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/Console.h"

#include <atomic>

namespace nazare
{

namespace pid
{
    // front panel
    inline constexpr const char* console  = "console";
    inline constexpr const char* drive    = "drive";
    inline constexpr const char* spread   = "spread";
    inline constexpr const char* output   = "output";
    inline constexpr const char* bypass   = "bypass";
    // rear panel
    inline constexpr const char* age      = "age";
    inline constexpr const char* noise    = "noise";
    inline constexpr const char* oversampling = "oversampling";
    inline constexpr const char* cal      = "cal";
    inline constexpr const char* mode     = "mode";
    inline constexpr const char* autoGain = "auto_gain";
    inline constexpr const char* matched  = "matched";
    inline constexpr const char* channel  = "channel";
}

/** The seven drive zones, evenly spaced along the knob. */
inline const char* zoneName (double knob)
{
    static const char* names[] = { "PRISTINE", "CLEAN", "SILK", "WARM", "RICH", "SATURATED", "DROWNED" };
    return names[juce::jlimit (0, 6, (int) (knob * 7.0))];
}

class NazareProcessor final : public juce::AudioProcessor
{
public:
    NazareProcessor();
    ~NazareProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.2; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }

    /** What the engine runs with right now (for the offline checks). */
    Settings currentSettings() const;

    juce::AudioProcessorValueTreeState apvts;

    /** Mean square per channel over the last block, and the block's peak; for the VU meter. */
    std::atomic<float> meterL { 0 }, meterR { 0 }, peak { 0 };

    /** Settings outside the parameters: which panel is showing, the editor's size. */
    juce::ValueTree uiState { "UI" };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    template <typename T> void run (juce::AudioBuffer<T>&);
    void processChunk (double* l, double* r, int n);

    Console console;
    std::vector<double> bufL, bufR;
    std::vector<double> dryL, dryR;      // dry path, delayed by the oversampler's latency
    int dryWrite = 0, dryLength = 1;

    juce::AudioParameterChoice* consoleParam { nullptr };
    juce::AudioParameterChoice* osParam      { nullptr };
    juce::AudioParameterChoice* calParam     { nullptr };
    juce::AudioParameterChoice* modeParam    { nullptr };
    juce::AudioParameterBool*   bypassParam  { nullptr };
    juce::AudioParameterBool*   autoParam    { nullptr };
    juce::AudioParameterBool*   matchedParam { nullptr };
    juce::AudioParameterInt*    channelParam { nullptr };
    std::atomic<float>* drive { nullptr }; std::atomic<float>* spreadP { nullptr }; std::atomic<float>* outputP { nullptr };
    std::atomic<float>* ageP { nullptr };  std::atomic<float>* noiseP { nullptr };

    double fs = 48000;
    int runningModel = -1, runningOrder = -1, pendingModel = -1;
    double wet = 1.0;                    // 1 = console, 0 = dry; fades on bypass and console changes
    double smoothDrive = 6.0, smoothAuto = 0.0;
    bool smoothed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NazareProcessor)
};

} // namespace nazare
