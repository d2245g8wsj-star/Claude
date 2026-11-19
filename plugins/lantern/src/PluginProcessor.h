#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Eq.h"
#include "dsp/Meters.h"
#include "dsp/OutputStage.h"
#include "dsp/Tube.h"

namespace lantern
{

/** Parameter identifiers. One set of controls drives both channels unless
    the unit is split (dual mono) or in mid/side, where the second set, under
    the side_ prefix, drives the right channel or the side. */
namespace pid
{
    inline constexpr const char* hpf       = "hpf";
    inline constexpr const char* hpfIn     = "hpf_in";
    inline constexpr const char* hpfSteep  = "hpf_24";
    inline constexpr const char* lpf       = "lpf";
    inline constexpr const char* lpfIn     = "lpf_in";
    inline constexpr const char* lpfSteep  = "lpf_24";

    /** The four bands, low to high. Each has <band>_freq, _gain and _in;
        the outer two a _bell switch (off is a shelf), the inner two _hiq. */
    inline constexpr std::array<const char*, eq::numBands> bands { "lf", "lmf", "hmf", "hf" };

    inline juce::String band (int index, const char* what) { return juce::String (bands[(size_t) index]) + "_" + what; }
    inline bool isOuter (int index) noexcept { return index == 0 || index == eq::numBands - 1; }

    inline constexpr const char* thd       = "thd";
    inline constexpr const char* output    = "output";

    inline constexpr const char* gainRange = "range";
    inline constexpr const char* qMode     = "q_mode";

    inline constexpr const char* iron      = "iron";
    inline constexpr const char* ironPad   = "iron_pad";
    inline constexpr const char* ironLink  = "iron_link";

    inline constexpr const char* mode      = "mode";
    inline constexpr const char* dualSplit = "dual_split";
    inline constexpr const char* bypass    = "bypass";

    inline juce::String side (const juce::String& name) { return "side_" + name; }

    /** Every setting a channel panel has its own of. */
    inline juce::StringArray perChannel()
    {
        juce::StringArray ids { hpf, hpfIn, hpfSteep, lpf, lpfIn, lpfSteep, thd, output };
        for (int b = 0; b < eq::numBands; ++b)
        {
            ids.add (band (b, "freq"));
            ids.add (band (b, "gain"));
            ids.add (band (b, "in"));
            ids.add (band (b, isOuter (b) ? "bell" : "hiq"));
        }
        return ids;
    }
}

enum class ChannelMode { dualMono = 0, stereo, midSide };
enum class GainRange   { moderate = 0, wide };
enum class QMode       { proportional = 0, constant };

namespace range
{
    inline constexpr float trimDb     = 15.0f;
    inline constexpr float hpfMin     = 12.0f,   hpfMax = 300.0f;
    inline constexpr float lpfMin     = 6000.0f, lpfMax = 50000.0f;
    inline constexpr float thdMax     = 100.0f;      ///< steps
    inline constexpr float thdTopPercent = 8.0f;     ///< THD at +4 dBu with the knob fully up
    inline constexpr float ironPadMax = OutputStage::maxPadDb;

    /** The THD knob's hundred steps follow a square law to 8 % at the
        operating level: 50 is 2 %, 25 is 0.5 %. */
    inline float thdPercent (float steps) noexcept
    {
        const auto t = juce::jlimit (0.0f, 1.0f, steps / thdMax);
        return thdTopPercent * t * t;
    }
}

inline constexpr int oversamplingStages = 2;

class LanternProcessor final : public juce::AudioProcessor
{
public:
    LanternProcessor();
    ~LanternProcessor() override = default;

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

    /** What the equaliser of the first or second channel is set to, with the
        range and Q switches applied: what the audio runs on, and what the
        panel's curve is drawn from. */
    eq::Settings settingsFor (bool second) const;

    juce::AudioProcessorValueTreeState apvts;
    MeterSource                        meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    struct Parameters
    {
        juce::AudioParameterFloat* hpf { nullptr }, *lpf { nullptr };
        juce::AudioParameterBool*  hpfIn { nullptr }, *hpfSteep { nullptr }, *lpfIn { nullptr }, *lpfSteep { nullptr };

        std::array<juce::AudioParameterChoice*, eq::numBands> freq {};
        std::array<juce::AudioParameterFloat*,  eq::numBands> gain {};
        std::array<juce::AudioParameterBool*,   eq::numBands> in {};
        std::array<juce::AudioParameterBool*,   eq::numBands> variant {};   ///< bell (outer) or HI-Q (inner)

        juce::AudioParameterFloat* thd { nullptr }, *output { nullptr };
    };

    struct ChannelPath
    {
        eq::Channel equaliser;
        tube::Stage thdStage;
        OutputStage outputStage;

        using Multiplicative = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>;
        using Linear         = juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>;

        Multiplicative outputGain { 1.0f };
        Linear thdDrive { 0.0f }, engaged { 1.0f };

        void prepare (double innerRate, double hostRate);
        void reset();
    };

    template <typename Type>
    Type* findParameter (const juce::String& id) const
    {
        auto* p = dynamic_cast<Type*> (apvts.getParameter (id));
        jassert (p != nullptr);
        return p;
    }

    Parameters params, sideParams;
    juce::AudioParameterChoice* rangeParam      { nullptr };
    juce::AudioParameterChoice* qModeParam      { nullptr };
    juce::AudioParameterBool*   ironParam       { nullptr };
    juce::AudioParameterFloat*  ironPadParam    { nullptr };
    juce::AudioParameterChoice* modeParam       { nullptr };
    juce::AudioParameterBool*   dualSplitParam  { nullptr };
    juce::AudioParameterBool*   bypassParameter { nullptr };

    std::array<ChannelPath, 2> paths;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

    using DryDelay = juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None>;
    DryDelay dryDelay { 1 };
    juce::AudioBuffer<float> dryBuffer;

    bool pathsSnapped { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanternProcessor)
};

} // namespace lantern
