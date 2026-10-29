#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Meters.h"
#include "dsp/OutputStage.h"
#include "dsp/Tube.h"
#include "dsp/VariMu.h"

namespace beacon
{

/** Parameter identifiers. One set of controls drives both channels. */
namespace pid
{
    inline constexpr const char* input     = "input";
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* feedFwd   = "feed_forward";
    inline constexpr const char* limit     = "limit";
    inline constexpr const char* hpf       = "sc_hpf";
    inline constexpr const char* hpfIn     = "sc_hpf_in";
    inline constexpr const char* bell      = "sc_bell";
    inline constexpr const char* bellIn    = "sc_bell_in";
    inline constexpr const char* listen    = "sc_listen";
    inline constexpr const char* thd       = "thd";
    inline constexpr const char* tube      = "tube";
    inline constexpr const char* tubeIn    = "tube_in";
    inline constexpr const char* iron      = "iron";
    inline constexpr const char* ironPad   = "iron_pad";
    inline constexpr const char* ironLink  = "iron_link";
    inline constexpr const char* mix       = "mix";
    inline constexpr const char* output    = "output";
    inline constexpr const char* dynLink   = "dynamic_link";

    inline constexpr const char* mode      = "mode";
    inline constexpr const char* bypass    = "bypass";
}

/** How the controls are applied to the two channels. The settings are the
    same in every mode; what changes is what each detector listens to.

    dualMono: each channel compresses on its own detector.
    stereo:   one detector for both, so the image cannot shift.
    midSide:  mid and side each compress on their own detector. */
enum class ChannelMode { dualMono = 0, stereo, midSide };

namespace range
{
    inline constexpr float trimDb       = 15.0f;                         ///< input, tube and output, either way
    inline constexpr float thresholdMin = -40.0f;                        ///< dB below the +26 dBu ceiling
    inline constexpr float attackMin    = 0.5f,   attackMax    = 100.0f; ///< ms
    inline constexpr float releaseMin   = 0.1f,   releaseMax   = 3.0f;   ///< s
    inline constexpr float hpfMin       = 20.0f,  hpfMax       = 500.0f;
    inline constexpr float bellMin      = 2000.0f, bellMax     = 20000.0f;
    inline constexpr float thdMax       = 8.0f;                          ///< %
    inline constexpr float ironPadMax   = OutputStage::maxPadDb;

    /** The threshold reads in dB below the unit's +26 dBu ceiling, which is
        +8 dBFS here. At 0, fully clockwise, the compressor is off. */
    inline constexpr float ceilingDbfs = 26.0f + level::dbfsAtZeroDbu;

    inline float thresholdDbfs (float control) noexcept { return control + ceilingDbfs; }
}

/** Everything runs at 4x: the feedback loop wants a short loop delay, and the
    gain cell, the THD stage and the iron all generate harmonics. */
inline constexpr int oversamplingStages = 2;

class BeaconProcessor final : public juce::AudioProcessor
{
public:
    BeaconProcessor();
    ~BeaconProcessor() override = default;

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

    struct Parameters
    {
        juce::AudioParameterFloat* input     { nullptr };
        juce::AudioParameterFloat* threshold { nullptr };
        juce::AudioParameterFloat* attack    { nullptr };
        juce::AudioParameterFloat* release   { nullptr };
        juce::AudioParameterBool*  feedFwd   { nullptr };
        juce::AudioParameterBool*  limit     { nullptr };
        juce::AudioParameterFloat* hpf       { nullptr };
        juce::AudioParameterBool*  hpfIn     { nullptr };
        juce::AudioParameterFloat* bell      { nullptr };
        juce::AudioParameterBool*  bellIn    { nullptr };
        juce::AudioParameterBool*  listen    { nullptr };
        juce::AudioParameterFloat* thd       { nullptr };
        juce::AudioParameterFloat* tube      { nullptr };
        juce::AudioParameterBool*  tubeIn    { nullptr };
        juce::AudioParameterBool*  iron      { nullptr };
        juce::AudioParameterFloat* ironPad   { nullptr };
        juce::AudioParameterFloat* mix       { nullptr };
        juce::AudioParameterFloat* output    { nullptr };
    };

    /** The signal path of one channel, at the oversampled rate. */
    struct ChannelPath
    {
        Transformer      inputIron { Transformer::input };
        tube::Stage      thdStage;
        SidechainFilter  sidechain;
        varimu::Envelope envelope;
        varimu::Cell     cell;
        OutputStage      outputStage;

        using Multiplicative = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
        using Linear         = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

        Multiplicative inputGain { 1.0f }, outputGain { 1.0f }, tubeGain { 1.0f }, threshold { 1.0f };
        Linear thdDrive { 0.0f }, mix { 1.0f }, engaged { 1.0f };

        bool compressing { true }, feedForward { false }, limiting { false }, listening { false }, tubeOn { true };

        float lastCellOutput { 0.0f };

        void prepare (double innerRate, double hostRate);
        void reset();

        void setTargets (const Parameters&, bool engagedNow);
        void snapToTargets();
    };

    template <typename Type>
    Type* findParameter (const juce::String& id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    Parameters params;
    juce::AudioParameterChoice* modeParam       { nullptr };
    juce::AudioParameterBool*   bypassParameter { nullptr };

    std::array<ChannelPath, 2> paths;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    using DryDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;
    DryDelay dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer;

    bool pathsSnapped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeaconProcessor)
};

} // namespace beacon
