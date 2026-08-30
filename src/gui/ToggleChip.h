#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace crest::gui
{

/** A latching switch with an indicator, for the boolean parameters.

    Reads as on or off from across the room, which matters most for DELTA and
    BYPASS: both change what you are listening to, and neither should ever be
    left on by accident.
*/
class ToggleChip final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    ToggleChip (juce::AudioProcessorValueTreeState& state,
                const juce::String& parameterID,
                const juce::String& caption,
                juce::Colour accentColour);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& parameter;
    juce::String                captionText;
    juce::Colour                accent;
    bool                        state { false };
    bool                        hover { false };

    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToggleChip)
};

} // namespace crest::gui
