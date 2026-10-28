#pragma once

#include "PluginProcessor.h"
#include "gui/Display.h"
#include "gui/Panel.h"

namespace breakwater
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1200;
inline constexpr int designHeight = 868;

class BreakwaterEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit BreakwaterEditor (BreakwaterProcessor&);
    ~BreakwaterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    class Face final : public juce::Component
    {
    public:
        explicit Face (BreakwaterProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        juce::String caption() const;
        juce::String footer() const;

        BreakwaterProcessor& processor;

        panel::Segmented engine, oversampling, style, compMode, wordLength, dither;
        panel::Toggle    truePeak, bypass, adaptiveLink, compOn, dcFilter, match, delta, sidechain;

        panel::Knob gain, ceiling;
        panel::Knob lookahead, attack, release, window, linkTransients, linkRelease;
        panel::Knob transients, lowPunch, detail, balance, clip;
        panel::Knob compThreshold, compRatio, compAttack, compRelease, compHighPass;
        panel::Knob target;

        display::Scope    scope;
        display::Meters   meters;
        display::Loudness loudness;

        int   shownEngine { -1 };
        bool  shownCompressor { false };
        float compressorReductionDb { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    BreakwaterProcessor& processor;
    juce::TooltipWindow  tooltips { this, 650 };
    Face                 face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakwaterEditor)
};

} // namespace breakwater
