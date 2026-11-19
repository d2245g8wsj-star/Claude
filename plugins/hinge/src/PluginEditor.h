#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Panel.h"

namespace hinge
{

/** Every coordinate in the editor is in these units; the face is scaled as one.
    Folded (stereo, linked) the unit is one row; unfolded (dual or mid/side)
    it carries a second row underneath. */
inline constexpr int designWidth        = 1640;
inline constexpr int designHeightFolded = 198;
inline constexpr int designHeightOpen   = 352;

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
    void applyFold();

    using Knob = indiewave::ui::BakeliteKnob;

    /** One channel's row: the low section, the high section, the dynamics,
        output, engage and the meter. */
    struct Strip
    {
        Strip (HingeProcessor&, int index);

        std::vector<juce::Component*> components();
        void layout (float rowCentre);

        Knob lowCut, lowShelf, lowLevel, highLevel, highShelf, highCut;
        Knob threshold, lowDyn, highDyn, output;
        indiewave::ui::Key engage;
        indiewave::ui::LedLadder meter;
        float movement[2] {};
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (HingeProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

        bool isOpen() const noexcept { return open; }
        std::function<void()> onFoldChanged;

    private:
        void setMode (int index);
        void paintRow (juce::Graphics&, int index, float rowCentre) const;

        HingeProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;
        std::array<indiewave::ui::Key, 3> modeKeys;
        indiewave::ui::FoldTab fold;

        bool open { false };
        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    HingeProcessor&     processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HingeEditor)
};

} // namespace hinge
