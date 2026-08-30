#pragma once

#include "PluginProcessor.h"
#include "gui/Theme.h"
#include "gui/Knob.h"
#include "gui/MeterView.h"
#include "gui/CurveView.h"
#include "gui/SegmentedControl.h"
#include "gui/ToggleChip.h"

namespace crest
{

/** Design size. Everything is laid out against these numbers and then scaled as
    a whole, so the proportions survive any window size the host allows. */
inline constexpr int designWidth  = 880;
inline constexpr int designHeight = 640;

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

    /** Holds every control at design scale; the editor only scales this. */
    class Content final : public juce::Component
    {
    public:
        explicit Content (CrestProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void setStatusText (const juce::String& text);

    private:
        CrestProcessor& processor;

        gui::LevelMeter     inputMeter;
        gui::LevelMeter     outputMeter;
        gui::ReductionMeter reductionMeter;
        gui::CurveView      curve;

        gui::Knob inputKnob, ceilingKnob;
        gui::Knob kneeKnob, biasKnob, colourKnob, mixKnob, outputKnob;

        gui::SegmentedControl modeStrip, osStrip, wordStrip, ditherStrip;

        gui::ToggleChip autoChip, tpSafeChip, deltaChip, linkChip, bypassChip;

        juce::String statusText;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Content)
    };

    CrestProcessor&      processor;
    theme::LookAndFeel   lookAndFeel;
    juce::TooltipWindow  tooltips { this, 600 };
    Content              content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrestEditor)
};

} // namespace crest
