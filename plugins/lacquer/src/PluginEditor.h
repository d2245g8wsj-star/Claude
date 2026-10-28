#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace lacquer
{

/** Every coordinate in the editor is in these units; the face is scaled as one.
    The proportions are those of a 5U, 19 inch front panel. */
inline constexpr int designWidth  = 1400;
inline constexpr int designHeight = 640;

class LacquerEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit LacquerEditor (LacquerProcessor&);
    ~LacquerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** One channel: six bands left to right, frequency above amount, and the
        four switches at the right hand end. */
    struct Strip
    {
        Strip (LacquerProcessor&, int index);

        std::vector<juce::Component*> components();

        panel::SteppedKnob lowBoostFreq, lowBoostAmount, lowCutFreq, lowCutAmount;
        panel::SteppedKnob midBoostFreq, midBoostAmount, midCutFreq, midCutAmount;
        panel::SteppedKnob trebleBoostFreq, trebleQ, trebleBoostAmount, trebleCutFreq, trebleCutAmount;
        panel::Toggle shift, buffer, transformer, eqIn;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (LacquerProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        void layoutStrip (Strip&, float top);
        void paintStripMarkings (juce::Graphics&, int index, float top) const;

        LacquerProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;

        panel::Toggle      link;
        panel::SteppedKnob output;
        panel::PilotLamp   pilot;

        int shownLink { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    LacquerProcessor&   processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LacquerEditor)
};

} // namespace lacquer
