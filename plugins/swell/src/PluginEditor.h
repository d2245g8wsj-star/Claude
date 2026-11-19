#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Rendered.h"

namespace swell
{

/**
    The rendered panel, in the range's Nazaré style (plugins/swell/art/spec.py).
    Folded for stereo (one strip for both channels), open for dual or mid/side.
*/
class SwellEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SwellEditor (SwellProcessor&);
    ~SwellEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The size in points of the face for the current mode. */
    juce::Point<int> designSize() const;

private:
    void timerCallback() override;
    void showFace (bool open);

    using Face = indiewave::ui::rendered::Face;

    struct Extras
    {
        Extras (Face&, int rows);

        std::vector<std::unique_ptr<indiewave::ui::rendered::Lamp>> drive;     // row * 3 + band
        std::vector<std::unique_ptr<indiewave::ui::rendered::VuNeedles>> meters;
    };

    SwellProcessor& processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face folded, opened;
    Extras foldedExtras, openExtras;

    bool open { false };
    int shownMode { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellEditor)
};

} // namespace swell
