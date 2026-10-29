#pragma once

#include "PluginProcessor.h"
#include "gui/Display.h"
#include "gui/Panel.h"

namespace breakwater
{

/** Every coordinate in the editor is in these units; the face is scaled as one.
    The simple view is the top part; the advanced panel extends it downwards. */
inline constexpr int designWidth          = 1200;
inline constexpr int designHeight         = 636;
inline constexpr int designHeightAdvanced = 1056;

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

    /** Shows or hides the advanced panel, keeping the current scale. */
    void setAdvanced (bool);

    class Face final : public juce::Component
    {
    public:
        explicit Face (BreakwaterProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();
        void showAdvanced (bool);

        panel::Toggle advanced { "Advanced" };

    private:
        juce::String caption() const;
        juce::String footer() const;

        std::vector<juce::Component*> advancedComponents();

        BreakwaterProcessor& processor;

        // Simple view.
        panel::Segmented engine, style;
        panel::Toggle    truePeak, bypass, match, delta;
        panel::Knob      gain, ceiling, transients, lowPunch, detail, target;

        display::Scope    scope;
        display::Meters   meters;
        display::Loudness loudness;

        // Advanced panel.
        panel::Segmented oversampling, compMode, wordLength, dither;
        panel::Toggle    adaptiveLink, compOn, dcFilter, sidechain;
        panel::Knob      lookahead, attack, release, window, linkTransients, linkRelease;
        panel::Knob      balance, clip;
        panel::Knob      compThreshold, compRatio, compAttack, compRelease, compHighPass;

        bool  advancedShown { false };
        int   shownEngine { -1 };
        bool  shownCompressor { false };
        float compressorReductionDb { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    BreakwaterProcessor& processor;
    juce::TooltipWindow  tooltips { this, 650 };
    Face                 face;

    std::unique_ptr<juce::PropertiesFile> userSettings;
    bool sizeRestored { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakwaterEditor)
};

} // namespace breakwater
