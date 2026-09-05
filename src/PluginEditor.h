#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace crest
{

/** The plate is authored at 2600 x 728 for an editor of 1300 x 364, so every
    coordinate below is in plate points and the whole thing is scaled as one. */
inline constexpr int designWidth  = 1400;
inline constexpr int designHeight = 470;

class CrestEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit CrestEditor (CrestProcessor&);
    ~CrestEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    class Face final : public juce::Component
    {
    public:
        explicit Face (CrestProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void setStatusText (const juce::String&);

        /** The figures on the always-on plates, refreshed by the editor timer. */
        void updateReadouts (float clipDb);


    private:
        CrestProcessor& processor;

        juce::Image plate;
        juce::Image ledOff, ledGreen, ledAmber, ledRed;

        panel::Filmstrip largeStrip, smallStrip, characterStrip;

        panel::Knob inputKnob, kneeKnob, adaptKnob, biasKnob;
        panel::Knob colourKnob, harmonicsKnob, seriesKnob, sidechainKnob, mixKnob;
        panel::Knob ceilingKnob, outputKnob, characterSwitch;

        panel::LedMeter  inputMeter, outputMeter;
        panel::Waveform  waveform;
        panel::Scope     scope;

        panel::Bank   osBank, wordBank, ditherBank;
        panel::Switch autoSwitch, tpSafeSwitch, deltaSwitch, linkSwitch, bypassSwitch;

        panel::Readout inputPeak, outputPeak;
        panel::Readout clipAmount, momentary, shortTerm, integrated;

        juce::String statusText;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    class Tooltips final : public juce::LookAndFeel_V4
    {
    public:
        Tooltips();
        void drawTooltip (juce::Graphics&, const juce::String&, int width, int height) override;
    };

    CrestProcessor&     processor;
    Tooltips            lookAndFeel;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrestEditor)
};

} // namespace crest
