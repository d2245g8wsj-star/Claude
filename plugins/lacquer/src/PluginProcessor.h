#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Band.h"
#include "dsp/Colour.h"
#include "dsp/Voicing.h"

namespace lacquer
{

inline constexpr int numChannelStrips = 2;

/** Parameter identifiers. Every per channel control exists twice, prefixed
    "c1_" and "c2_", so the editor builds both channels from one list. */
namespace pid
{
    inline constexpr const char* lowBoostFreq      = "lb_freq";
    inline constexpr const char* lowBoostAmount    = "lb_amount";
    inline constexpr const char* lowCutFreq        = "lc_freq";
    inline constexpr const char* lowCutAmount      = "lc_amount";
    inline constexpr const char* midBoostFreq      = "mb_freq";
    inline constexpr const char* midBoostAmount    = "mb_amount";
    inline constexpr const char* midCutFreq        = "mc_freq";
    inline constexpr const char* midCutAmount      = "mc_amount";
    inline constexpr const char* trebleBoostFreq   = "tb_freq";
    inline constexpr const char* trebleQ           = "tb_q";
    inline constexpr const char* trebleBoostAmount = "tb_amount";
    inline constexpr const char* trebleCutFreq     = "tc_freq";
    inline constexpr const char* trebleCutAmount   = "tc_amount";
    inline constexpr const char* shift             = "shift";
    inline constexpr const char* buffer            = "buffer";
    inline constexpr const char* transformer       = "transformer";
    inline constexpr const char* eqIn              = "eq_in";

    inline constexpr const char* link   = "link";
    inline constexpr const char* output = "output";
    inline constexpr const char* bypass = "bypass";

    inline juce::String forStrip (int strip, const char* name)
    {
        return juce::String (strip == 0 ? "c1_" : "c2_") + name;
    }
}

/** The 1x rate band edge sits at 24 kHz on the top treble cut, and the
    transformer's resonance well above that; 4x gives both room at 44.1 kHz. */
inline constexpr int oversamplingStages = 2;

class LacquerProcessor final : public juce::AudioProcessor
{
public:
    LacquerProcessor();
    ~LacquerProcessor() override = default;

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

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct StripParameters
    {
        juce::AudioParameterChoice* lowBoostFreq      { nullptr };
        juce::AudioParameterInt*    lowBoostAmount    { nullptr };
        juce::AudioParameterChoice* lowCutFreq        { nullptr };
        juce::AudioParameterInt*    lowCutAmount      { nullptr };
        juce::AudioParameterChoice* midBoostFreq      { nullptr };
        juce::AudioParameterInt*    midBoostAmount    { nullptr };
        juce::AudioParameterChoice* midCutFreq        { nullptr };
        juce::AudioParameterInt*    midCutAmount      { nullptr };
        juce::AudioParameterChoice* trebleBoostFreq   { nullptr };
        juce::AudioParameterInt*    trebleQ           { nullptr };
        juce::AudioParameterInt*    trebleBoostAmount { nullptr };
        juce::AudioParameterChoice* trebleCutFreq     { nullptr };
        juce::AudioParameterInt*    trebleCutAmount   { nullptr };
        juce::AudioParameterBool*   shift             { nullptr };
        juce::AudioParameterBool*   buffer            { nullptr };
        juce::AudioParameterBool*   transformer       { nullptr };
        juce::AudioParameterBool*   eqIn              { nullptr };

        voicing::Settings settings() const;
    };

    /** The signal path of one channel, at the oversampled rate:
        input buffer, the seven filter sections, make up amp, transformer. */
    struct ChannelPath
    {
        ChannelPath();

        std::vector<Band> sections;

        colour::InputStage        input;
        colour::MakeupAmp         amp;
        colour::OutputTransformer transformer;

        using Fade = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;
        Fade bufferIn, transformerIn, eqIn;

        void prepare (double innerRate);
        void reset();
        void setTargets (const StripParameters&);
        void snapToTargets();
        float process (float x) noexcept;
    };

    template <typename Type>
    Type* findParameter (const juce::String& id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    std::array<StripParameters, numChannelStrips> strips;
    juce::AudioParameterBool*  linkParam       { nullptr };
    juce::AudioParameterFloat* outputParam     { nullptr };
    juce::AudioParameterBool*  bypassParameter { nullptr };

    std::array<ChannelPath, 2> paths;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         engaged    { 1.0f };

    using DryDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;
    DryDelay dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer;

    bool pathsSnapped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LacquerProcessor)
};

} // namespace lacquer
