#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Meters.h"
#include "dsp/OutputStage.h"
#include "dsp/Tube.h"
#include "dsp/Vca.h"

namespace anchor
{

/** Parameter identifiers. One set of controls drives both channels unless
    the unit is split (dual mono) or in mid/side, where the second set, under
    the side_ prefix, drives the right channel or the side. */
namespace pid
{
    inline constexpr const char* input     = "input";
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* ratio     = "ratio";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* feedback  = "feedback";
    inline constexpr const char* scFilter  = "sc_filter";
    inline constexpr const char* listen    = "sc_listen";
    inline constexpr const char* thd       = "thd";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* dynLink   = "dynamic_link";

    /** The detectors listen to the external sidechain input instead of the
        programme. One switch for the unit: there is one sidechain input. */
    inline constexpr const char* external  = "sc_external";

    inline constexpr const char* iron      = "iron";
    inline constexpr const char* ironPad   = "iron_pad";
    inline constexpr const char* ironLink  = "iron_link";

    inline constexpr const char* mode      = "mode";
    inline constexpr const char* dualSplit = "dual_split";
    inline constexpr const char* bypass    = "bypass";

    inline juce::String side (const char* name) { return juce::String ("side_") + name; }
}

/** dualMono: each channel compresses on its own detector.
    stereo:   one detector for both, so the image cannot shift.
    midSide:  mid and side each compress on their own detector. */
enum class ChannelMode { dualMono = 0, stereo, midSide };

namespace range
{
    inline constexpr float trimDb       = 15.0f;    ///< input and output, either way
    inline constexpr float thresholdMin = -20.0f;   ///< dBu
    inline constexpr float thresholdMax = 20.0f;    ///< dBu
    inline constexpr float thdMax       = 8.0f;     ///< %
    inline constexpr float ironPadMax   = OutputStage::maxPadDb;

    inline float thresholdDbfs (float dbu) noexcept { return dbu + level::dbfsAtZeroDbu; }
}

inline constexpr int oversamplingStages = 2;

class AnchorProcessor final : public juce::AudioProcessor
{
public:
    AnchorProcessor();
    ~AnchorProcessor() override = default;

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
    double getTailLengthSeconds() const override             { return 0.0; }

    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }

    /** True while the host feeds the sidechain input. */
    bool hasExternalSidechain() const noexcept { return externalConnected.load (std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;
    MeterSource                        meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct Parameters
    {
        juce::AudioParameterFloat*  input     { nullptr };
        juce::AudioParameterFloat*  threshold { nullptr };
        juce::AudioParameterChoice* ratio     { nullptr };
        juce::AudioParameterChoice* attack    { nullptr };
        juce::AudioParameterChoice* release   { nullptr };
        juce::AudioParameterBool*   feedback  { nullptr };
        juce::AudioParameterChoice* scFilter  { nullptr };
        juce::AudioParameterBool*   listen    { nullptr };
        juce::AudioParameterFloat*  thd       { nullptr };
        juce::AudioParameterFloat*  mix       { nullptr };
        juce::AudioParameterFloat*  output    { nullptr };
    };

    struct ChannelPath
    {
        tube::Stage           thdStage;
        SidechainFilter       sidechain;
        vca::Ballistics       ballistics;
        vca::Cell             cell;
        OutputStage           outputStage;

        using Multiplicative = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
        using Linear         = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

        Multiplicative inputGain { 1.0f }, outputGain { 1.0f };
        Linear thresholdDb { 0.0f }, thdDrive { 0.0f }, mix { 1.0f }, engaged { 1.0f };

        float slope { 0.75f };
        bool feedback { false }, listening { false };
        float lastWetOutput { 0.0f };
        float maxFeedbackCoeff { 0.08f };

        void prepare (double innerRate, double hostRate);
        void reset();
        void setTargets (const Parameters&, bool ironOn, float ironPadDb, bool engagedNow);
        void snapToTargets();
    };

    template <typename Type>
    Type* findParameter (const juce::String& id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    Parameters params, sideParams;
    juce::AudioParameterBool*   externalParam   { nullptr };
    juce::AudioParameterBool*   ironParam       { nullptr };
    juce::AudioParameterFloat*  ironPadParam    { nullptr };
    juce::AudioParameterChoice* modeParam       { nullptr };
    juce::AudioParameterBool*   dualSplitParam  { nullptr };
    juce::AudioParameterBool*   bypassParameter { nullptr };

    std::array<ChannelPath, 2> paths;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler, sidechainOversampler;

    using DryDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;
    DryDelay dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer, keyBuffer;

    std::atomic<bool> externalConnected { false };
    bool pathsSnapped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnchorProcessor)
};

} // namespace anchor
