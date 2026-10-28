#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Meters.h"

namespace swell::panel
{

// The IndieWave plate, the same colours Crest is printed in, so the two sit
// side by side in a session as one family.
inline const juce::Colour ink    { 0xff3a352e };
inline const juce::Colour cream  { 0xffe4dcc8 };
inline const juce::Colour amber  { 0xffc77b3c };
inline const juce::Colour recess { 0xff181510 };

juce::Font plateFont (float height, bool bold = true);

/** The brushed cream plate, rendered once at the size it will be drawn. */
juce::Image renderPlate (int width, int height);

/** A group enclosure milled into the plate, drawn as a cut rather than an outline. */
void drawGroupWell (juce::Graphics&, juce::Rectangle<float>, float corner = 7.0f);

/** Engraved lettering, letterspaced the way the plate's own lettering is. */
void drawTrackedLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                       float height, float tracking,
                       juce::Justification = juce::Justification::centred,
                       float alpha = 0.88f);

/** A slotted panel screw, the slot turned to a fixed angle per screw. */
void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float slotAngle);

/** Loads a vertical filmstrip once and hands out frames. */
class Filmstrip
{
public:
    Filmstrip (const void* data, int size);

    void drawProportion (juce::Graphics&, juce::Rectangle<float> area, float proportion) const;

private:
    juce::Image image;
    int numFrames { 1 };
    int frameHeight { 1 };
};

/** A rotary painted from Crest's knob filmstrip, with its value shown while in use. */
class Knob final : public juce::Slider
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, const Filmstrip&);

    void paint (juce::Graphics&) override;

private:
    const Filmstrip& filmstrip;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A latching plate switch: cream at rest, lit copper when engaged. */
class Switch final : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    Switch (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, juce::String caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    juce::RangedAudioParameter& parameter;
    juce::String caption;
    juce::ParameterAttachment attachment;
    bool on { false }, hover { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Switch)
};

/** A row of plate switches selecting one choice of a parameter. */
class Bank final : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    Bank (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, juce::String caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int getSelected() const noexcept { return index; }

private:
    juce::Rectangle<float> cell (int) const;

    juce::RangedAudioParameter& parameter;
    juce::String caption;
    juce::StringArray choices;
    juce::ParameterAttachment attachment;
    int index { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Bank)
};

/** The LED segment images Crest's meters are built from. */
struct LedImages
{
    LedImages();
    juce::Image off, green, amber, red;
};

/** A column of LED segments over a recessed window, fed a peak per frame. */
class LedMeter final : public juce::Component
{
public:
    explicit LedMeter (const LedImages&);

    void paint (juce::Graphics&) override;

    /** Takes the peak since the last frame, linear. */
    void push (float peak);

    float getHoldDb() const noexcept { return hold; }

private:
    const LedImages& leds;
    float level { -100.0f }, hold { -100.0f };
    int holdTicks { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LedMeter)
};

/** One amber segment that lights as a band's curve starts to bend. */
class DriveLamp final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    explicit DriveLamp (const LedImages&);

    void paint (juce::Graphics&) override;
    void push (float drive);

private:
    const LedImages& leds;
    float glow { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriveLamp)
};

/** A figure in a dark window, as under Crest's meters. */
class Readout final : public juce::Component
{
public:
    Readout() { setInterceptsMouseClicks (false, false); }

    void paint (juce::Graphics&) override;
    void setValue (const juce::String&);

private:
    juce::String value { "-inf" };
};

} // namespace swell::panel
