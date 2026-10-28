#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Meters.h"

namespace hinge::panel
{

// The plate: warm ivory enamel with dark engraving, black knobs. The low
// shelf is brick, the high shelf steel blue, the dynamics olive, so the
// sections read apart from across a room.
inline const juce::Colour plateTop     { 0xffece6d9 };
inline const juce::Colour plateBottom  { 0xffd8d0bf };
inline const juce::Colour engraving    { 0xff2b2825 };
inline const juce::Colour engravingDim { 0xff746b5f };
inline const juce::Colour recess       { 0xff1a1918 };

inline const juce::Colour lowAccent    { 0xffb4532e };
inline const juce::Colour highAccent   { 0xff2d6e8e };
inline const juce::Colour dynAccent    { 0xff7d8b34 };
inline const juce::Colour trimAccent   { 0xff5a554e };

juce::Font plateFont (float height, bool bold = true);

void drawTrackedLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                       float height, float tracking = 1.5f,
                       juce::Justification = juce::Justification::centred,
                       juce::Colour = engraving);

/** A shallow pressed panel, shaded so the upper edge sits in shadow. */
void drawWell (juce::Graphics&, juce::Rectangle<float>, float corner = 8.0f);

/** A rack ear with two slotted mounting holes. */
void drawRackEar (juce::Graphics&, juce::Rectangle<float>);

/** A rotary with its own scale printed around it. A parameter with steps gets
    a tick at every detent; a continuous one gets a fine scale. */
class Knob final : public juce::Slider
{
public:
    enum class Size { large, small, compact };

    /** Scale legends, each at the knob position of its parameter value. */
    using Legends = std::vector<std::pair<float, juce::String>>;

    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          juce::Colour accent, Size, Legends);

    void paint (juce::Graphics&) override;

private:
    juce::Colour accent;
    Size size;
    Legends legends;

    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A latching square push button with a lamp in its cap. */
class Lamp final : public juce::Button
{
public:
    Lamp (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          juce::Colour lamp, juce::String legend);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour lampColour;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Lamp)
};

/** A round lamp whose brightness follows how far the dynamics have moved a
    shelf, so the side chain can be set by eye. */
class MovementLamp final : public juce::Component
{
public:
    explicit MovementLamp (juce::Colour);

    void paint (juce::Graphics&) override;

    /** Takes the movement since the last frame, in dB. */
    void push (float movementDb);

private:
    juce::Colour colour;
    float glow { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MovementLamp)
};

/** Twelve segment peak meter, clip at the head. */
class LedMeter final : public juce::Component
{
public:
    static constexpr int numSegments = 12;

    /** Lower edge of each segment, in dBFS. */
    static constexpr std::array<float, numSegments> thresholds {
        -48.0f, -36.0f, -30.0f, -24.0f, -18.0f, -12.0f, -9.0f, -6.0f, -4.0f, -2.0f, -1.0f, 0.0f
    };

    LedMeter();

    void paint (juce::Graphics&) override;

    /** Takes the peak since the last frame, linear. */
    void push (float peak);

private:
    float levelDb { -100.0f };
    float clipHold { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LedMeter)
};

} // namespace hinge::panel
