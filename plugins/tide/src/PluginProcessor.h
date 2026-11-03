#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/ChannelEq.h"

namespace tide
{

inline constexpr int numChannelStrips = 2;

/** Parameter identifiers. Every per channel control exists twice, prefixed
    "a_" (left or mid) and "b_" (right or side), so the editor builds both
    halves of the panel from one list. */
namespace pid
{
    inline constexpr const char* lowFreq   = "lo_freq";
    inline constexpr const char* lowShelf  = "lo_shelf";
    inline constexpr const char* lowGain   = "lo_gain";
    inline constexpr const char* midFreq   = "mid_freq";
    inline constexpr const char* midGain   = "mid_gain";
    inline constexpr const char* highFreq  = "hi_freq";
    inline constexpr const char* highShelf = "hi_shelf";
    inline constexpr const char* highGain  = "hi_gain";
    inline constexpr const char* hpfIn     = "hpf_in";
    inline constexpr const char* hpfTimes5 = "hpf_x5";
    inline constexpr const char* hpfFreq   = "hpf_freq";
    inline constexpr const char* level     = "level";

    inline constexpr const char* eqIn      = "eq_in";
    inline constexpr const char* midSide   = "mid_side";
    inline constexpr const char* cue       = "cue";
    inline constexpr const char* link      = "link";
    inline constexpr const char* bypass    = "bypass";

    inline juce::String forStrip (int strip, const char* name)
    {
        return juce::String (strip == 0 ? "a_" : "b_") + name;
    }
}

/** Positions of the cue switch, left to right. */
enum class Cue { first = 0, off = 1, second = 2 };

class TideProcessor final : public juce::AudioProcessor
{
public:
    TideProcessor();
    ~TideProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                          { return true; }

    const juce::String getName() const override              { return JucePlugin_Name; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0.1; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    /** Samples between two control steps at this rate. */
    static int controlIntervalFor (double sampleRate) noexcept
    {
        return 16 * juce::jmax (1, juce::roundToInt (sampleRate / 48000.0));
    }

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct StripParameters
    {
        juce::AudioParameterChoice* lowFreq   { nullptr };
        juce::AudioParameterBool*   lowShelf  { nullptr };
        juce::AudioParameterChoice* lowGain   { nullptr };
        juce::AudioParameterChoice* midFreq   { nullptr };
        juce::AudioParameterChoice* midGain   { nullptr };
        juce::AudioParameterChoice* highFreq  { nullptr };
        juce::AudioParameterBool*   highShelf { nullptr };
        juce::AudioParameterChoice* highGain  { nullptr };
        juce::AudioParameterBool*   hpfIn     { nullptr };
        juce::AudioParameterBool*   hpfTimes5 { nullptr };
        juce::AudioParameterChoice* hpfFreq   { nullptr };
        juce::AudioParameterChoice* level     { nullptr };

        ChannelSettings read() const;
    };

    template <typename Type>
    Type* findParameter (const juce::String& id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    void process (double* left, double* right, int numChannels, int numSamples);

    std::array<StripParameters, numChannelStrips> strips;
    juce::AudioParameterBool*   eqInParam       { nullptr };
    juce::AudioParameterBool*   midSideParam    { nullptr };
    juce::AudioParameterChoice* cueParam        { nullptr };
    juce::AudioParameterBool*   linkParam       { nullptr };
    juce::AudioParameterBool*   bypassParameter { nullptr };

    std::array<ChannelEq, 2> channels;
    int controlInterval { 16 };
    int untilControl    { 0 };
    bool snapped        { false };

    /** The mode the filters are running in. A change of the M/S switch fades
        the EQ out, changes over with the filters cleared, and fades back in,
        rather than handing one signal's filter state to another. */
    bool runningMidSide { false };

    using Linear = juce::SmoothedValue<double, juce::ValueSmoothingTypes::Linear>;
    Linear engaged, cueFirst, cueSecond;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TideProcessor)
};

} // namespace tide
