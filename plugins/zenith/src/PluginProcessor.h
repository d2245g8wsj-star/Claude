#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/AnalogLimiter.h"
#include "dsp/Enhancer.h"
#include "dsp/Leveller.h"
#include "dsp/Loudness.h"
#include "dsp/Meters.h"
#include "dsp/Oversampler.h"
#include "dsp/PeakLimiter.h"
#include "dsp/Quantizer.h"
#include "dsp/SoftClipper.h"
#include "dsp/Styles.h"
#include "dsp/TruePeak.h"

namespace zenith
{

namespace pid
{
    inline constexpr const char* gain           = "gain";
    inline constexpr const char* ceiling        = "ceiling";
    inline constexpr const char* engine         = "engine";
    inline constexpr const char* style          = "style";
    inline constexpr const char* lookahead      = "lookahead";
    inline constexpr const char* attack         = "attack";
    inline constexpr const char* release        = "release";
    inline constexpr const char* linkTransients = "link_transients";
    inline constexpr const char* linkRelease    = "link_release";
    inline constexpr const char* transients     = "transients";
    inline constexpr const char* detail         = "detail";
    inline constexpr const char* clip           = "clip";
    inline constexpr const char* oversampling   = "oversampling";
    inline constexpr const char* truePeak       = "true_peak";
    inline constexpr const char* dcFilter       = "dc_filter";
    inline constexpr const char* match          = "match";
    inline constexpr const char* delta          = "delta";
    inline constexpr const char* wordLength     = "word_length";
    inline constexpr const char* dither         = "dither";
    inline constexpr const char* bypass         = "bypass";
}

enum class Engine { modern = 0, analog };

/** Oversampling choices are powers of two, 1x to 16x. */
inline constexpr int numOversamplingChoices = 5;

namespace range
{
    inline constexpr float maxLookaheadMs = 10.0f;

    /** The final stage: a short fixed lookahead, enough for a smooth ramp at
        any sample rate, and the true peak interpolator's own delay. */
    inline constexpr double finalLookaheadMs = 1.5;
    inline constexpr float  finalReleaseMs   = 40.0f;

    /** Headroom kept under the ceiling in true peak mode, for what the
        interpolator cannot see. */
    inline constexpr float truePeakMarginDb = 0.05f;
}

class ZenithProcessor final : public juce::AudioProcessor
{
public:
    ZenithProcessor();
    ~ZenithProcessor() override = default;

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

    /** Everything that changes the latency, or the shape of the chain. */
    struct Structure
    {
        int    oversamplingIndex { -1 };
        int    lookaheadSamples  { -1 };   ///< at the host rate
        Engine engine            { Engine::modern };

        bool operator== (const Structure&) const = default;
    };

    Structure readStructure() const;
    void applyStructure (const Structure&);
    void updateSettings();
    void processChain (juce::AudioBuffer<float>&, int numChannels);
    void publishLoudness();

    template <typename Type>
    Type* findParameter (const juce::String& id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    // Parameters.
    juce::AudioParameterFloat*  gainParam           { nullptr };
    juce::AudioParameterFloat*  ceilingParam        { nullptr };
    juce::AudioParameterChoice* engineParam         { nullptr };
    juce::AudioParameterChoice* styleParam          { nullptr };
    juce::AudioParameterFloat*  lookaheadParam      { nullptr };
    juce::AudioParameterFloat*  attackParam         { nullptr };
    juce::AudioParameterFloat*  releaseParam        { nullptr };
    juce::AudioParameterFloat*  linkTransientsParam { nullptr };
    juce::AudioParameterFloat*  linkReleaseParam    { nullptr };
    juce::AudioParameterFloat*  transientsParam     { nullptr };
    juce::AudioParameterFloat*  detailParam         { nullptr };
    juce::AudioParameterFloat*  clipParam           { nullptr };
    juce::AudioParameterChoice* oversamplingParam   { nullptr };
    juce::AudioParameterBool*   truePeakParam       { nullptr };
    juce::AudioParameterBool*   dcFilterParam       { nullptr };
    juce::AudioParameterBool*   matchParam          { nullptr };
    juce::AudioParameterBool*   deltaParam          { nullptr };
    juce::AudioParameterChoice* wordLengthParam     { nullptr };
    juce::AudioParameterChoice* ditherParam         { nullptr };
    juce::AudioParameterBool*   bypassParameter     { nullptr };

    // The chain, in signal order.
    double hostRate  { 48000.0 };
    int    blockSize { 512 };

    Structure structure;
    int       latency { 0 };

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> ceilingGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> monitorGain { 1.0f };

    std::array<float, 2> dcState { };
    float dcCoeff { 0.0f };

    Enhancer enhancer;
    Leveller leveller;

    std::array<Oversampler, numOversamplingChoices> oversamplers;
    SoftClipper   clipper;
    PeakLimiter   peakLimiter;
    AnalogLimiter analogLimiter;

    std::array<TruePeakInterpolator, 2> finalDetectors;
    std::array<SampleDelay, 2>          finalAlign;
    PeakLimiter                         finalLimiter;

    std::array<TruePeakInterpolator, 2> outputTruePeak;
    std::array<Quantizer, 2>            quantizers;

    LoudnessMeter inputLoudness, outputLoudness;
    int samplesSinceLoudness { 0 };
    float truePeakMax { 0.0f };

    using DryDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;
    DryDelay dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer;

    // Per block scratch, so the display can see the driven input.
    juce::AudioBuffer<float> drivenBuffer;
    std::vector<float> reductionTrace;

    bool smoothersSnapped { false };
    bool prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZenithProcessor)
};

} // namespace zenith
