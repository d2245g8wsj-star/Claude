#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace hinge
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1620;
inline constexpr int designHeight = 340;

class HingeEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit HingeEditor (HingeProcessor&);
    ~HingeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** One channel's row: the low section, the high section, the dynamics,
        output, engage and the meter. */
    struct Strip
    {
        Strip (HingeProcessor&, int index);

        std::vector<juce::Component*> components();

        panel::Knob lowCut, lowShelf, lowLevel, highLevel, highShelf, highCut;
        panel::Knob threshold, lowDyn, highDyn, output;
        panel::Lamp engage;
        panel::MovementLamp lowMovement, highMovement;
        panel::LedMeter meter;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (HingeProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        void layoutStrip (Strip&, float top);
        void paintStripLegends (juce::Graphics&, int index, float top) const;

        HingeProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;
        panel::Lamp link, midSide;

        int shownLink { -1 }, shownMidSide { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    HingeProcessor&     processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HingeEditor)
};

} // namespace hinge
