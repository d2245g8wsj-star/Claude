#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace crest::gui
{

/** A choice parameter as a row of segments.

    Everything the plugin switches between is short and mutually exclusive, so a
    row of segments beats a drop-down: the alternatives stay visible and picking
    one is a single click instead of click, read, click.
*/
class SegmentedControl final : public juce::Component,
                              public juce::SettableTooltipClient
{
public:
    SegmentedControl (juce::AudioProcessorValueTreeState& state,
                      const juce::String& parameterID,
                      const juce::String& caption,
                      juce::Colour accentColour);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> segmentBounds (int index) const;
    int segmentAt (juce::Point<float> position) const;
    juce::Rectangle<float> stripArea() const;

    juce::RangedAudioParameter& parameter;
    juce::StringArray           choices;
    juce::String                captionText;
    juce::Colour                accent;
    int                         selectedIndex { 0 };
    int                         hoverIndex    { -1 };

    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SegmentedControl)
};

} // namespace crest::gui
