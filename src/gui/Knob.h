#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace crest::gui
{

/** The plugin's only continuous control.

    A rotary with the value arc drawn from the left for unipolar parameters and
    from the centre for bipolar ones, so a glance is enough to tell how far a
    control sits from neutral. Painted rather than skinned: no bitmaps, no
    filmstrips, so it stays sharp at any host scaling.
*/
class Knob final : public juce::Slider
{
public:
    Knob (juce::AudioProcessorValueTreeState& state,
          const juce::String& parameterID,
          const juce::String& caption,
          juce::Colour accentColour,
          bool bipolar = false);

    void paint (juce::Graphics&) override;

    /** Draws the dial larger, for the two main gain stages. */
    void setLarge (bool shouldBeLarge)  { large = shouldBeLarge; repaint(); }

private:
    juce::String captionText;
    juce::Colour accent;
    bool         isBipolar { false };
    bool         large     { false };

    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

} // namespace crest::gui
