#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Shaper.h"
#include "dsp/ColorStage.h"
#include "dsp/Quantizer.h"
#include "dsp/LowSplit.h"
#include "dsp/ClipEnvelope.h"
#include "dsp/HarmonicStage.h"
#include "dsp/Meters.h"

namespace crest
{

/** Parameter identifiers, in one place so the editor and the processor cannot
    drift apart. */
namespace pid
{
    inline constexpr const char* input    = "input";
    inline constexpr const char* ceiling  = "ceiling";
    inline constexpr const char* mode     = "mode";
    inline constexpr const char* knee     = "knee";
    inline constexpr const char* adapt    = "adapt";
    inline constexpr const char* bias     = "bias";
    inline constexpr const char* colour   = "colour";
    inline constexpr const char* harmonics = "harmonics";
    inline constexpr const char* series    = "series";
    inline constexpr const char* sidechain = "sclpf";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
    inline constexpr const char* os       = "os";
    inline constexpr const char* word     = "word";
    inline constexpr const char* dither   = "dither";
    inline constexpr const char* autoGain = "autogain";
    inline constexpr const char* tpSafe   = "tpsafe";
    inline constexpr const char* delta    = "delta";
    inline constexpr const char* link     = "link";
    inline constexpr const char* bypass   = "bypass";
}

/** Oversampling factors offered by the "OS" selector, as powers of two. */
inline constexpr int maxOversamplingStages = 4;   // 1x, 2x, 4x, 8x, 16x

/** Below this the sidechain filter is switched out entirely rather than left
    running at a corner where it does nothing measurable. */
inline constexpr float sidechainOffBelowHz = 20.0f;

class CrestProcessor final : public juce::AudioProcessor
{
public:
    CrestProcessor();
    ~CrestProcessor() override = default;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
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

    /** Snapshot of the shaper as the audio thread currently has it, for the
        curve view. Written from the audio thread, read by the editor; both
        fields are plain floats so a torn read costs one wrong pixel. */
    std::atomic<float> displayThresholdPos { 1.0f };
    std::atomic<float> displayThresholdNeg { 1.0f };

    /** Observability hooks. Two stages decide for themselves whether to run,
        and both are inaudible when they get it right and hard to find when
        they get it wrong, so they report what they actually did. */
    std::atomic<bool>  dcBlockerRunning     { false };
    std::atomic<bool>  sidechainRunning     { false };

    /** What the adaptation is doing right now: 0 = treating the material as
        transient, 1 = as sustained. The scope reads the knee so the curve on
        screen is the curve being applied. */
    std::atomic<float> displayDensity { 0.0f };
    std::atomic<float> displayKnee    { 0.25f };
    std::atomic<float> appliedOversamplingGain { 1.0f };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    template <typename Type>
    Type* findParameter (const char* id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    void updateOversamplingLatency();

    /** Measures what a full up/down round trip does to the level, so switching
        oversampling factor does not also change the gain. */
    static float measureRoundTripGain (juce::dsp::Oversampling<float>&, int numChannels, int blockSize);

    juce::AudioParameterFloat*  inputParam    { nullptr };
    juce::AudioParameterFloat*  ceilingParam  { nullptr };
    juce::AudioParameterChoice* modeParam     { nullptr };
    juce::AudioParameterFloat*  kneeParam     { nullptr };
    juce::AudioParameterFloat*  adaptParam    { nullptr };
    juce::AudioParameterFloat*  biasParam     { nullptr };
    juce::AudioParameterFloat*  colourParam   { nullptr };
    juce::AudioParameterFloat*  harmonicsParam { nullptr };
    juce::AudioParameterFloat*  seriesParam    { nullptr };
    juce::AudioParameterFloat*  sidechainParam { nullptr };
    juce::AudioParameterFloat*  mixParam      { nullptr };
    juce::AudioParameterFloat*  outputParam   { nullptr };
    juce::AudioParameterChoice* osParam       { nullptr };
    juce::AudioParameterChoice* wordParam     { nullptr };
    juce::AudioParameterChoice* ditherParam   { nullptr };
    juce::AudioParameterBool*   autoGainParam { nullptr };
    juce::AudioParameterBool*   tpSafeParam   { nullptr };
    juce::AudioParameterBool*   deltaParam    { nullptr };
    juce::AudioParameterBool*   linkParam     { nullptr };
    juce::AudioParameterBool*   bypassParameter { nullptr };

    // One oversampler per factor, all prepared up front, so switching the
    // selector never allocates on the audio thread.
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, maxOversamplingStages + 1> oversamplers;
    int currentOversamplingIndex { 0 };

    /** Reciprocal passband gain of each oversampler, measured once at prepare
        time. Cascaded half band filters lose a few hundredths of a dB, and the
        loss grows with the factor; without this, comparing 4x against 16x is
        also comparing two different levels. */
    std::array<float, maxOversamplingStages + 1> oversamplingGain { };

    using DryDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>;
    DryDelay dryDelay { 1024 };

    Shaper                   shaper;
    std::array<ColorStage, 2> colour;
    std::array<LowSplit, 2>   lowSplit;
    std::array<ClipEnvelope, 2>  clipEnvelope;
    std::array<HarmonicStage, 2> harmonics;
    std::array<DCBlocker, 2>  dcBlockers;
    std::array<Quantizer, 2>  quantizers;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> preGain  { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> postGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> makeupGain { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> osCorrection { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         mixAmount { 1.0f };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> trimGain { 1.0f };

    juce::AudioBuffer<float> dryBuffer;

    bool dcBlockerActive { false };
    bool sidechainActive { false };

    double preparedSampleRate { 44100.0 };
    int    preparedBlockSize  { 512 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrestProcessor)
};

} // namespace crest
