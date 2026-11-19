#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/ChannelEq.h"
#include "dsp/Meters.h"

namespace hinge
{

inline constexpr int numChannelStrips = 2;

/** Parameter identifiers. Every per channel control exists twice, prefixed
    "a_" and "b_", so the editor builds both rows from one list. */
namespace pid
{
    inline constexpr const char* lowCut     = "lo_cut";
    inline constexpr const char* lowShelf   = "lo_shelf";
    inline constexpr const char* lowLevel   = "lo_level";
    inline constexpr const char* highLevel  = "hi_level";
    inline constexpr const char* highShelf  = "hi_shelf";
    inline constexpr const char* highCut    = "hi_cut";
    inline constexpr const char* output     = "output";
    inline constexpr const char* engage     = "engage";
    inline constexpr const char* threshold  = "dyn_threshold";
    inline constexpr const char* lowDyn     = "lo_dyn";
    inline constexpr const char* highDyn    = "hi_dyn";

    inline constexpr const char* link       = "link";
    inline constexpr const char* midSide    = "mid_side";
    inline constexpr const char* bypass     = "bypass";

    inline juce::String forStrip (int strip, const char* name)
    {
        return juce::String (strip == 0 ? "a_" : "b_") + name;
    }
}

namespace range
{
    inline constexpr float thresholdMin = -48.0f, thresholdMax = 0.0f;
    inline constexpr float dynDb = 5.0f;     ///< either way
}

class HingeProcessor final : public juce::AudioProcessor
{
public:
    HingeProcessor();
    ~HingeProcessor() override = default;

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
    double getTailLengthSeconds() const override             { return 0.2; }

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
    MeterSource                        meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct StripParameters
    {
        juce::AudioParameterChoice* lowCut    { nullptr };
        juce::AudioParameterChoice* lowShelf  { nullptr };
        juce::AudioParameterFloat*  lowLevel  { nullptr };
        juce::AudioParameterFloat*  highLevel { nullptr };
        juce::AudioParameterChoice* highShelf { nullptr };
        juce::AudioParameterChoice* highCut   { nullptr };
        juce::AudioParameterInt*    output    { nullptr };
        juce::AudioParameterBool*   engage    { nullptr };
        juce::AudioParameterFloat*  threshold { nullptr };
        juce::AudioParameterFloat*  lowDyn    { nullptr };
        juce::AudioParameterFloat*  highDyn   { nullptr };

        ChannelSettings read (bool bypassed) const;
    };

    template <typename Type>
    Type* findParameter (const juce::String& id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    std::array<StripParameters, numChannelStrips> strips;
    juce::AudioParameterBool* linkParam       { nullptr };
    juce::AudioParameterBool* midSideParam    { nullptr };
    juce::AudioParameterBool* bypassParameter { nullptr };

    std::array<ChannelEq, 2> channels;
    int controlInterval { 16 };
    int untilControl    { 0 };
    bool snapped        { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HingeProcessor)
};

} // namespace hinge
