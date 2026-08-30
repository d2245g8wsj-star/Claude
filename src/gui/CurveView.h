#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Shaper.h"
#include "../dsp/Meters.h"
#include "Theme.h"

namespace crest::gui
{

/** The transfer curve, drawn from the same Shaper the audio thread uses.

    Because the curve and the DSP share one implementation there is no way for
    the picture to describe something the plugin is not doing. The shaded area
    between the diagonal and the curve is exactly the material the clipper is
    removing, and the marker rides the curve at the level currently driving it.
*/
class CurveView final : public juce::Component,
                        private juce::Timer
{
public:
    CurveView (juce::AudioProcessorValueTreeState& state, MeterSource& source);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildCurve();

    juce::Point<float> toPixels (float x, float y) const;

    juce::AudioProcessorValueTreeState& apvts;
    MeterSource&                        meters;

    Shaper       shaper;
    juce::Path   curvePath;
    juce::Path   shadedPath;
    juce::String characterName { "SLICE" };

    juce::Rectangle<float> plotArea;

    float lastKnee   { -1.0f };
    float lastBias   { -1.0f };
    int   lastMode   { -1 };
    float driveLevel { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveView)
};

} // namespace crest::gui
