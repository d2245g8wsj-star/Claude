#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Meters.h"

namespace swell::panel
{

// A painted steel front panel with silkscreened legends, the way outboard was
// finished before anyone rendered a knob: flat paint, one ink, a few colours
// kept for the parts that need telling apart.
inline const juce::Colour paint     { 0xff3f4a52 };   // blue grey enamel
inline const juce::Colour silk      { 0xffe8dfc8 };   // cream silkscreen
inline const juce::Colour silkDim   { 0xffb9b19d };
inline const juce::Colour bakelite  { 0xff1c1a18 };
inline const juce::Colour brass     { 0xffc3a467 };

// Knob cap inserts, one per band, faded the way coloured plastic goes.
inline const juce::Colour capLo     { 0xffa65a42 };   // oxide red
inline const juce::Colour capMid    { 0xffc59a42 };   // mustard
inline const juce::Colour capHi     { 0xff5d8796 };   // faded teal
inline const juce::Colour capTrim   { 0xffd9cfb6 };   // ivory

/** Narrow, heavy capitals, the face silkscreen legends were set in. */
juce::Font legendFont (float height);

/** The enamel, rendered once at the size it will be drawn. */
juce::Image renderPaint (int width, int height);

/** Silkscreened text: one flat ink, no shadow, letterspaced. */
void drawLegend (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float height,
                 float tracking = 1.2f, juce::Justification = juce::Justification::centred,
                 juce::Colour = silk);

/** A printed section box: a thin rule with its title set into a gap in the top line. */
void drawSection (juce::Graphics&, juce::Rectangle<float>, const juce::String& title);

void drawRackEar (juce::Graphics&, juce::Rectangle<float>);
void drawScrew (juce::Graphics&, juce::Point<float>, float radius, float slotAngle);

/** The engraved brass maker's plate. */
void drawNameplate (juce::Graphics&, juce::Rectangle<float>);

/** A fluted bakelite knob with a coloured cap and a printed scale around it. */
class Knob final : public juce::Slider
{
public:
    using Legends = std::vector<std::pair<float, juce::String>>;

    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          juce::Colour cap, float knobDiameter, Legends);

    void paint (juce::Graphics&) override;

private:
    juce::Colour cap;
    float diameter;
    Legends legends;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A bat handle toggle: lever up is IN, down is OUT. */
class Toggle final : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    Toggle (juce::AudioProcessorValueTreeState&, const juce::String& parameterID);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
    bool on { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toggle)
};

/** A faceted pilot lamp whose brightness follows how hard a band is driven. */
class JewelLamp final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    JewelLamp();

    void paint (juce::Graphics&) override;
    void push (float drive);

private:
    float glow { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JewelLamp)
};

/** A moving coil meter: cream face, black needle, red past zero. */
class VuMeter final : public juce::Component
{
public:
    VuMeter();

    void paint (juce::Graphics&) override;

    /** Takes the peak since the last frame, linear. */
    void push (float peak);

    void setCaption (const juce::String&);

private:
    float needle { -20.0f };    ///< in VU
    float peakLamp { 0.0f };
    juce::String caption;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeter)
};

/** A chicken head rotary switch over a choice parameter. */
class Selector final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    Selector (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, juce::StringArray positions);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int getSelected() const noexcept { return index; }

private:
    float angleFor (int position) const;
    juce::Rectangle<float> labelArea (int position) const;

    juce::RangedAudioParameter& parameter;
    juce::StringArray names;
    juce::ParameterAttachment attachment;
    int index { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Selector)
};

} // namespace swell::panel
