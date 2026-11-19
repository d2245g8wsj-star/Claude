#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Rendered.h"

namespace anchor
{

/**
    The rendered panel, in the range's Nazaré style (plugins/anchor/art/spec.py).
    Folded, one half and the centre for stereo; open, the side (or right) half
    beside it, on the side_* parameters, for mid/side or a split dual mono.
*/
class AnchorEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit AnchorEditor (AnchorProcessor&);
    ~AnchorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The size in points of the face for the current state. */
    juce::Point<int> designSize() const;

private:
    void timerCallback() override;
    void splitClicked();
    bool wantsOpen() const;
    void showFace (bool open);

    using Face = indiewave::ui::rendered::Face;

    struct Extras
    {
        Extras (Face&, int halves);

        indiewave::ui::rendered::Switch split;
        std::vector<std::unique_ptr<indiewave::ui::rendered::VuNeedles>> meters;
    };

    AnchorProcessor& processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face folded, opened;
    Extras foldedExtras, openExtras;
    std::vector<std::unique_ptr<indiewave::ui::rendered::Follow>> links;

    bool open { false }, shownWant { false };
    int shownMode { -1 }, shownIron { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnchorEditor)
};

} // namespace anchor
