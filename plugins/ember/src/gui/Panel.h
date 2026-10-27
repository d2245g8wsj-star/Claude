#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Meters.h"

namespace ember::panel
{

// The plate: dark anodised graphite with warm engraving. Each band carries its
// own accent on the knob skirt and the saturation lamp, so the three can be
// told apart at a glance from across a room.
inline const juce::Colour plateTop    { 0xff2d3036 };
inline const juce::Colour plateBottom { 0xff1f2125 };
inline const juce::Colour engraving   { 0xffd8d1c2 };
inline const juce::Colour engravingDim{ 0xff8d877b };
inline const juce::Colour recess      { 0xff121316 };

inline const juce::Colour loAccent    { 0xffe0893a };
inline const juce::Colour midAccent   { 0xff52b59f };
inline const juce::Colour hiAccent    { 0xff8db7ea };
inline const juce::Colour trimAccent  { 0xffb9b3a6 };

juce::Font plateFont (float height, bool bold = true);

void drawTrackedLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                       float height, float tracking = 1.5f,
                       juce::Justification = juce::Justification::centred,
                       juce::Colour = engraving);

/** A milled group recess, the floor shaded top to bottom with a lit lower lip. */
void drawWell (juce::Graphics&, juce::Rectangle<float>, float corner = 8.0f);

/** A rack ear with two slotted mounting holes. */
void drawRackEar (juce::Graphics&, juce::Rectangle<float>);

/** A rotary with its own scale printed around it. */
class Knob final : public juce::Slider
{
public:
    enum class Size { large, small };

    /** Scale legends, each at the knob position of its parameter value. */
    using Legends = std::vector<std::pair<float, juce::String>>;

    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          juce::Colour accent, Size, Legends);

    void paint (juce::Graphics&) override;

    Size getSize() const noexcept { return size; }

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

/** The three mode buttons, which select one choice of a parameter. */
class ModeSelector final : public juce::Component
{
public:
    ModeSelector (juce::AudioProcessorValueTreeState&, const juce::String& parameterID);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int getSelected() const noexcept { return selected; }

private:
    juce::Rectangle<float> buttonArea (int index) const;

    juce::StringArray names;
    int selected { 0 };
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModeSelector)
};

/** A round lamp whose brightness follows how hard a band's curve is driven. */
class DriveLamp final : public juce::Component
{
public:
    explicit DriveLamp (juce::Colour);

    void paint (juce::Graphics&) override;

    /** Takes the band's drive since the last frame. */
    void push (float drive);

private:
    juce::Colour colour;
    float glow { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriveLamp)
};

/** Twelve segment peak meter: a signal lamp at the foot, clip at the head. */
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

} // namespace ember::panel
