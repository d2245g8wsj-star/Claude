#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace crest
{

/** The plate is authored at 2600 x 728 for an editor of 1300 x 364, so every
    coordinate below is in plate points and the whole thing is scaled as one. */
inline constexpr int designWidth  = 1300;
inline constexpr int designHeight = 364;

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
        void setBlindText (const juce::String&);

        std::function<void()> onReveal;

        panel::Switch matchSwitch, blindSwitch;
        panel::Push   revealButton;

    private:
        CrestProcessor& processor;

        juce::Image plate;
        juce::Image ledOff, ledGreen, ledAmber, ledRed;

        panel::Filmstrip knobStrip, characterStrip;

        panel::Knob inputKnob, kneeKnob, adaptKnob, biasKnob;
        panel::Knob colourKnob, harmonicsKnob, seriesKnob, sidechainKnob, mixKnob;
        panel::Knob ceilingKnob, outputKnob, characterSwitch;

        panel::LedMeter inputMeter, outputMeter;
        panel::Scope    scope;

        panel::Bank   osBank, wordBank, ditherBank;
        panel::Switch autoSwitch, tpSafeSwitch, deltaSwitch, linkSwitch, bypassSwitch;

        juce::String statusText, blindText;

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

    int revealSecondsLeft { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrestEditor)
};

} // namespace crest
