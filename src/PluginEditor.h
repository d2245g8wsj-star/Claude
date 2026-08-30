#pragma once

#include "PluginProcessor.h"
#include "gui/Hardware.h"

namespace crest
{

/** Design size. A 3:1 panel, laid out once and scaled as a whole, so the
    proportions of a piece of rack hardware survive any window size. */
inline constexpr int designWidth  = 1380;
inline constexpr int designHeight = 460;

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

    /** Everything at design scale; the editor only scales this. */
    class Panel final : public juce::Component
    {
    public:
        explicit Panel (CrestProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void setStatusText (const juce::String&);

    private:
        CrestProcessor& processor;

        juce::Image faceplate, headerPlate, railPlate;

        hw::LadderMeter inputMeter, outputMeter;
        hw::ScopeWindow scope;
        hw::ClipMeter   clipMeter;

        hw::Knob inputKnob, ceilingKnob, outputKnob;
        hw::Knob kneeKnob, biasKnob, colourKnob, sidechainKnob, mixKnob;

        hw::RotarySwitch characterSwitch;
        hw::LampBank     osBank, wordBank, ditherBank;
        hw::LampButton   autoLamp, tpSafeLamp, deltaLamp, linkLamp, bypassLamp;

        juce::String statusText;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
    };

    /** Only exists to keep the tooltips in the same material as the panel. */
    class Tooltips final : public juce::LookAndFeel_V4
    {
    public:
        Tooltips();
        void drawTooltip (juce::Graphics&, const juce::String&, int width, int height) override;
    };

    CrestProcessor&     processor;
    Tooltips            lookAndFeel;
    juce::TooltipWindow tooltips { this, 650 };
    Panel               panel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrestEditor)
};

} // namespace crest
