#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Meters.h"
#include "dsp/OutputStage.h"
#include "dsp/Tube.h"
#include "dsp/VariMu.h"

namespace kiln
{

inline constexpr int numChannelStrips = 2;

/** Parameter identifiers. Every per channel control exists twice, prefixed
    "a_" and "b_", so the editor builds both strips from one list. */
namespace pid
{
    inline constexpr const char* input     = "input";
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* feedFwd   = "feed_forward";
    inline constexpr const char* limit     = "limit";
    inline constexpr const char* scHpf     = "sc_hpf";
    inline constexpr const char* scHf      = "sc_hf";
    inline constexpr const char* scHfIn    = "sc_hf_in";
    inline constexpr const char* listen    = "sc_listen";
    inline constexpr const char* thd       = "thd";
    inline constexpr const char* tube      = "tube_out";
    inline constexpr const char* ironPad   = "iron_pad";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* active    = "in";
    inline constexpr const char* link      = "io_link";

    inline constexpr const char* mode      = "mode";
    inline constexpr const char* bypass    = "bypass";

    inline juce::String forStrip (int strip, const char* name)
    {
        return juce::String (strip == 0 ? "a_" : "b_") + name;
    }
}

/** How the two strips are applied to the two channels. */
enum class StripMode { dualMono = 0, stereo, midSide };

namespace range
{
    inline constexpr float trimDb       = 20.0f;
    inline constexpr float thresholdMin = -40.0f, thresholdMax = 0.0f;   ///< dBFS
    inline constexpr float attackMin    = 0.5f,   attackMax    = 100.0f; ///< ms
    inline constexpr float releaseMin   = 0.1f,   releaseMax   = 3.0f;   ///< s
    inline constexpr float hpfMin       = 20.0f,  hpfMax       = 500.0f;
    inline constexpr float hfMin        = 2000.0f, hfMax       = 20000.0f;
    inline constexpr float thdMax       = 8.0f;                          ///< %
}

/** Everything runs at 4x: the feedback loop wants a short loop delay, and the
    gain cell, the THD stage and the iron all generate harmonics. */
inline constexpr int oversamplingStages = 2;

class KilnProcessor final : public juce::AudioProcessor
{
public:
    KilnProcessor();
    ~KilnProcessor() override = default;

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

    juce::AudioProcessorValueTreeState apvts;
    MeterSource                        meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct StripParameters
    {
        juce::AudioParameterFloat* input     { nullptr };
        juce::AudioParameterFloat* threshold { nullptr };
        juce::AudioParameterFloat* attack    { nullptr };
        juce::AudioParameterFloat* release   { nullptr };
        juce::AudioParameterBool*  feedFwd   { nullptr };
        juce::AudioParameterBool*  limit     { nullptr };
        juce::AudioParameterFloat* scHpf     { nullptr };
        juce::AudioParameterFloat* scHf      { nullptr };
        juce::AudioParameterBool*  scHfIn    { nullptr };
        juce::AudioParameterBool*  listen    { nullptr };
        juce::AudioParameterFloat* thd       { nullptr };
        juce::AudioParameterBool*  tube      { nullptr };
        juce::AudioParameterBool*  ironPad   { nullptr };
        juce::AudioParameterFloat* mix       { nullptr };
        juce::AudioParameterFloat* output    { nullptr };
        juce::AudioParameterBool*  active    { nullptr };
    };

    /** The signal path of one channel, at the oversampled rate. */
    struct ChannelPath
    {
        SidechainFilter  sidechain;
        varimu::Envelope envelope;
        varimu::Cell     cell;
        tube::Stage      thdStage;
        OutputStage      outputStage;

        using Multiplicative = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
        using Linear         = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

        Multiplicative inputGain { 1.0f }, outputGain { 1.0f }, threshold { 0.1f };
        Linear thdDrive { 0.0f }, mix { 1.0f }, engaged { 1.0f };

        bool feedForward { false }, limiting { false }, listening { false };

        float lastCellOutput { 0.0f };

        void prepare (double innerRate, double hostRate);
        void reset();
        void setTargets (const StripParameters&, bool engagedNow);
        void snapToTargets();
    };

    template <typename Type>
    Type* findParameter (const juce::String& id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    std::array<StripParameters, numChannelStrips> strips;
    juce::AudioParameterChoice* modeParam       { nullptr };
    juce::AudioParameterBool*   bypassParameter { nullptr };

    std::array<ChannelPath, 2> paths;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    using DryDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;
    DryDelay dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer;

    bool pathsSnapped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KilnProcessor)
};

} // namespace kiln
