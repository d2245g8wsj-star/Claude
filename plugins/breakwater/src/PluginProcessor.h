#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/AnalogLimiter.h"
#include "dsp/Balance.h"
#include "dsp/Compressor.h"
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

namespace breakwater
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
    inline constexpr const char* window         = "window";
    inline constexpr const char* linkTransients = "link_transients";
    inline constexpr const char* linkRelease    = "link_release";
    inline constexpr const char* adaptiveLink   = "adaptive_link";

    inline constexpr const char* compOn         = "comp_on";
    inline constexpr const char* compThreshold  = "comp_threshold";
    inline constexpr const char* compRatio      = "comp_ratio";
    inline constexpr const char* compAttack     = "comp_attack";
    inline constexpr const char* compRelease    = "comp_release";
    inline constexpr const char* compHighPass   = "comp_highpass";
    inline constexpr const char* compMode       = "comp_mode";

    inline constexpr const char* transients     = "transients";
    inline constexpr const char* lowPunch       = "low_punch";
    inline constexpr const char* detail         = "detail";
    inline constexpr const char* balance        = "balance";
    inline constexpr const char* clip           = "clip";

    inline constexpr const char* oversampling   = "oversampling";
    inline constexpr const char* truePeak       = "true_peak";
    inline constexpr const char* dcFilter       = "dc_filter";
    inline constexpr const char* match          = "match";
    inline constexpr const char* delta          = "delta";
    inline constexpr const char* sidechain      = "sidechain";
    inline constexpr const char* target         = "target";
    inline constexpr const char* wordLength     = "word_length";
    inline constexpr const char* dither         = "dither";
    inline constexpr const char* bypass         = "bypass";
}

enum class Engine { modern = 0, analog };

/** Oversampling choices are powers of two, 1x to 32x. */
inline constexpr int numOversamplingChoices = Oversampler::maxStages + 1;

/** The Window choices: a quarter of the period of a C, from C0 (about 15 ms)
    up to C4, which is how far the detector's hold is stretched. */
struct WindowChoice { const char* name; double milliseconds; };

inline constexpr std::array<WindowChoice, 6> windowChoices {{
    { "Off", 0.0 },
    { "C4",  1000.0 / 261.6256 / 4.0 },
    { "C3",  1000.0 / 130.8128 / 4.0 },
    { "C2",  1000.0 /  65.4064 / 4.0 },
    { "C1",  1000.0 /  32.7032 / 4.0 },
    { "C0",  1000.0 /  16.3516 / 4.0 },
}};

/** Delivery loudness targets, for the meter's comparison. */
struct LoudnessTarget { const char* name; float lufs; };

inline constexpr std::array<LoudnessTarget, 8> loudnessTargets {{
    { "Off",                   0.0f },
    { "-9 LUFS Club",         -9.0f },
    { "-11 LUFS CD",         -11.0f },
    { "-14 LUFS Streaming",  -14.0f },
    { "-16 LUFS Apple Music", -16.0f },
    { "-18 LUFS Podcast",    -18.0f },
    { "-23 LUFS EBU R128",   -23.0f },
    { "-24 LKFS ATSC A/85",  -24.0f },
}};

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

    /** 0 VU, in dBFS. */
    inline constexpr float vuReferenceDb = -18.0f;
}

class BreakwaterProcessor final : public juce::AudioProcessor
{
public:
    BreakwaterProcessor();
    ~BreakwaterProcessor() override = default;

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
    void processChain (juce::AudioBuffer<float>&, const juce::AudioBuffer<float>* sidechainBuffer, int numChannels);
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
    juce::AudioParameterChoice* windowParam         { nullptr };
    juce::AudioParameterFloat*  linkTransientsParam { nullptr };
    juce::AudioParameterFloat*  linkReleaseParam    { nullptr };
    juce::AudioParameterBool*   adaptiveLinkParam   { nullptr };

    juce::AudioParameterBool*   compOnParam         { nullptr };
    juce::AudioParameterFloat*  compThresholdParam  { nullptr };
    juce::AudioParameterFloat*  compRatioParam      { nullptr };
    juce::AudioParameterFloat*  compAttackParam     { nullptr };
    juce::AudioParameterFloat*  compReleaseParam    { nullptr };
    juce::AudioParameterFloat*  compHighPassParam   { nullptr };
    juce::AudioParameterChoice* compModeParam       { nullptr };

    juce::AudioParameterFloat*  transientsParam     { nullptr };
    juce::AudioParameterFloat*  lowPunchParam       { nullptr };
    juce::AudioParameterFloat*  detailParam         { nullptr };
    juce::AudioParameterFloat*  balanceParam        { nullptr };
    juce::AudioParameterFloat*  clipParam           { nullptr };

    juce::AudioParameterChoice* oversamplingParam   { nullptr };
    juce::AudioParameterBool*   truePeakParam       { nullptr };
    juce::AudioParameterBool*   dcFilterParam       { nullptr };
    juce::AudioParameterBool*   matchParam          { nullptr };
    juce::AudioParameterBool*   deltaParam          { nullptr };
    juce::AudioParameterBool*   sidechainParam      { nullptr };
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

    Compressor compressor;
    Enhancer   enhancer;
    Leveller   leveller;

    std::array<Oversampler, numOversamplingChoices> oversamplers;
    std::array<Oversampler, numOversamplingChoices> sidechainOversamplers;
    SoftClipper   clipper;
    PeakLimiter   peakLimiter;
    AnalogLimiter analogLimiter;
    Balance       balance;

    std::array<TruePeakInterpolator, 2> finalDetectors;
    std::array<SampleDelay, 2>          finalAlign;
    PeakLimiter                         finalLimiter;

    std::array<TruePeakInterpolator, 2> outputTruePeak;
    std::array<Quantizer, 2>            quantizers;

    LoudnessMeter inputLoudness, outputLoudness;
    int samplesSinceLoudness { 0 };
    float truePeakMax { 0.0f };

    // Programme measurements: VU on the output, correlation on the input.
    float vuPower { 0.0f }, vuCoeff { 0.0f };
    float sumLR { 0.0f }, sumLL { 0.0f }, sumRR { 0.0f }, correlationCoeff { 0.0f };
    float linkInUse { 1.0f };

    using DryDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;
    DryDelay dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> sidechainScratch;

    std::vector<float> reductionTrace;

    bool smoothersSnapped { false };
    bool prepared { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakwaterProcessor)
};

} // namespace breakwater
