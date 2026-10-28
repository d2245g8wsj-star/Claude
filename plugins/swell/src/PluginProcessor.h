#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/HarmonicBand.h"
#include "dsp/Meters.h"

namespace swell
{

inline constexpr int numChannelStrips = 2;

/** Parameter identifiers. Every per channel control exists twice, prefixed
    "a_" and "b_", so the editor builds both strips from one list. */
namespace pid
{
    inline constexpr const char* input   = "input";
    inline constexpr const char* loGain  = "lo_gain";
    inline constexpr const char* loFreq  = "lo_freq";
    inline constexpr const char* loIn    = "lo_in";
    inline constexpr const char* midGain = "mid_gain";
    inline constexpr const char* midFreq = "mid_freq";
    inline constexpr const char* midIn   = "mid_in";
    inline constexpr const char* hiGain  = "hi_gain";
    inline constexpr const char* hiFreq  = "hi_freq";
    inline constexpr const char* hiIn    = "hi_in";
    inline constexpr const char* output  = "output";
    inline constexpr const char* active  = "in";

    inline constexpr const char* mode    = "mode";
    inline constexpr const char* bypass  = "bypass";

    inline juce::String forStrip (int strip, const char* name)
    {
        return juce::String (strip == 0 ? "a_" : "b_") + name;
    }
}

/** How the two strips are applied to the two channels. */
enum class StripMode { dualMono = 0, stereo, midSide };

/** Frequency ranges of the three bands, end to end. */
namespace range
{
    inline constexpr float loMin  =    20.0f, loMax  =   420.0f;
    inline constexpr float midMin =   200.0f, midMax =  6200.0f;
    inline constexpr float hiMin  =  5000.0f, hiMax  = 25000.0f;
    inline constexpr float trimDb = 20.0f;    ///< input and output, either way
}

/** Everything runs at 4x: the HI band reaches 25 kHz, past Nyquist at 44.1 and
    48 kHz, and the harmonics the curves generate need room above the band. */
inline constexpr int oversamplingStages = 2;

class SwellProcessor final : public juce::AudioProcessor
{
public:
    SwellProcessor();
    ~SwellProcessor() override = default;

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
        juce::AudioParameterFloat* input   { nullptr };
        juce::AudioParameterFloat* loGain  { nullptr };
        juce::AudioParameterFloat* loFreq  { nullptr };
        juce::AudioParameterBool*  loIn    { nullptr };
        juce::AudioParameterFloat* midGain { nullptr };
        juce::AudioParameterFloat* midFreq { nullptr };
        juce::AudioParameterBool*  midIn   { nullptr };
        juce::AudioParameterFloat* hiGain  { nullptr };
        juce::AudioParameterFloat* hiFreq  { nullptr };
        juce::AudioParameterBool*  hiIn    { nullptr };
        juce::AudioParameterFloat* output  { nullptr };
        juce::AudioParameterBool*  active  { nullptr };
    };

    /** The signal path of one channel, at the oversampled rate. */
    struct ChannelPath
    {
        HarmonicBand lo  { HarmonicBand::Shape::lowShelf };
        HarmonicBand mid { HarmonicBand::Shape::peak };
        HarmonicBand hi  { HarmonicBand::Shape::highShelf };

        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain  { 1.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain { 1.0f };
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>         engaged    { 1.0f };

        /** The bands run at the oversampled rate; the IN crossfade runs at the
            host rate, against a delayed copy of the input, so a strip
            switched out is the input exactly rather than the input through
            the oversampling filters. */
        void prepare (double innerRate, double hostRate);
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

    bool      pathsSnapped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellProcessor)
};

} // namespace swell
