#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Rendered.h"

namespace tide
{

/** The rendered panel, in the range's Nazaré style. Its size in points comes
    from the art (plugins/tide/art/spec.py); the face is scaled as one. */
class TideEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit TideEditor (TideProcessor&);
    ~TideEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The panel's size in points. */
    static juce::Point<int> designSize();

private:
    void timerCallback() override;

    TideProcessor& processor;
    juce::TooltipWindow tooltips { this, 650 };
    indiewave::ui::rendered::Face face;

    int shownLink { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TideEditor)
};

} // namespace tide
