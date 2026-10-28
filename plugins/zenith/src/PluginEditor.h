#pragma once

#include "PluginProcessor.h"
#include "gui/Display.h"
#include "gui/Panel.h"

namespace zenith
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1200;
inline constexpr int designHeight = 660;

class ZenithEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit ZenithEditor (ZenithProcessor&);
    ~ZenithEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    class Face final : public juce::Component
    {
    public:
        explicit Face (ZenithProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        juce::String caption() const;

        ZenithProcessor& processor;

        panel::Segmented engine, oversampling, style, wordLength, dither;
        panel::Toggle    truePeak, bypass, dcFilter, match, delta;

        panel::Knob gain, ceiling;
        panel::Knob lookahead, attack, release, linkTransients, linkRelease;
        panel::Knob transients, detail, clip;

        display::Scope    scope;
        display::Meters   meters;
        display::Loudness loudness;

        int shownEngine { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    ZenithProcessor&    processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZenithEditor)
};

} // namespace zenith
