#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Rendered.h"

namespace hinge
{

/**
    The rendered panel, in the range's Nazaré style (plugins/hinge/art/spec.py).
    Two faces: folded, one row for stereo; open, a second row for dual or
    mid/side. The mode selector chooses, and the editor's height follows.
*/
class HingeEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit HingeEditor (HingeProcessor&);
    ~HingeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The size in points of the face for the current mode. */
    juce::Point<int> designSize() const;

private:
    void timerCallback() override;
    void setMode (int index);
    void showFace (bool open);

    using Face = indiewave::ui::rendered::Face;

    struct Extras
    {
        Extras (Face&, bool open);

        indiewave::ui::rendered::Selector mode;
        std::array<std::unique_ptr<indiewave::ui::rendered::Lamp>, 2> lamps;
        std::array<std::unique_ptr<indiewave::ui::rendered::VuNeedles>, 2> meters;
    };

    HingeProcessor& processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face folded, opened;
    Extras foldedExtras, openExtras;

    bool open { false };
    int shownMode { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HingeEditor)
};

} // namespace hinge
