#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Rendered.h"

namespace beacon
{

/**
    The rendered panel, in the range's Nazaré style (plugins/beacon/art/spec.py).
    Folded, one row of controls; open, a second row on the side_* parameters,
    for mid/side or a split dual mono. The SPLIT switch folds and unfolds.
*/
class BeaconEditor final : public juce::AudioProcessorEditor,
                           private juce::Timer
{
public:
    explicit BeaconEditor (BeaconProcessor&);
    ~BeaconEditor() override;

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
        Extras (Face&, int rows);

        indiewave::ui::rendered::Switch split;
        std::vector<std::unique_ptr<indiewave::ui::rendered::VuNeedles>> meters;
    };

    BeaconProcessor& processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face folded, opened;
    Extras foldedExtras, openExtras;

    std::vector<std::unique_ptr<indiewave::ui::rendered::Follow>> links;

    bool open { false }, shownWant { false };
    int shownMode { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeaconEditor)
};

} // namespace beacon
