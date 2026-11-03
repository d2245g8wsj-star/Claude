#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace tide
{

/** Every coordinate in the editor is in these units, the proportions of a
    2U rack panel; the face is scaled as one. */
inline constexpr int designWidth  = 1640;
inline constexpr int designHeight = 300;

class TideEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TideEditor (TideProcessor&);
    ~TideEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** One half of the panel: left or mid, right or side. Four columns:
        LOW, MID, HIGH, and the high pass over the output LEVEL. */
    struct Strip
    {
        Strip (TideProcessor&, int index);

        std::vector<juce::Component*> components();

        panel::BakeliteKnob lowFreq, midFreq, highFreq, hpfFreq;
        panel::BakeliteKnob lowGain, midGain, highGain, level;
        panel::Toggle lowShelf, highShelf, hpfIn, hpfTimes5;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (TideProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        void layoutStrip (Strip&, float firstColumn);
        void paintStripLegends (juce::Graphics&, float firstColumn, bool greyed) const;

        TideProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;

        panel::LitButton eqIn, midSide, link;
        panel::CueSwitch cue;
        panel::Led cueFirst, cueSecond;

        int shownLink { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    TideProcessor&      processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TideEditor)
};

} // namespace tide
