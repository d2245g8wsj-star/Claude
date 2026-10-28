#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace kiln::panel
{

// The plate: dark anodised graphite with warm engraving, as on the rest of the
// range. Each section of a strip carries its own accent on the knob skirts and
// lamps: copper for the compressor, slate for the sidechain, and the orange of
// a lit heater for the tube colour.
inline const juce::Colour plateTop    { 0xff2d3036 };
inline const juce::Colour plateBottom { 0xff1f2125 };
inline const juce::Colour engraving   { 0xffd8d1c2 };
inline const juce::Colour engravingDim{ 0xff8d877b };
inline const juce::Colour recess      { 0xff121316 };

inline const juce::Colour compAccent  { 0xffd0884a };
inline const juce::Colour scAccent    { 0xff8aa3c2 };
inline const juce::Colour heatAccent  { 0xffe8663a };
inline const juce::Colour trimAccent  { 0xffb9b3a6 };
inline const juce::Colour limitAccent { 0xffff5a3c };

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

/** A moving coil gain reduction meter, as on the hardware: the needle rests
    at 0 on the right and falls to the left as the tubes are pushed towards
    cutoff. VU ballistics, about 300 ms to settle. */
class VuMeter final : public juce::Component
{
public:
    VuMeter();

    void paint (juce::Graphics&) override;

    /** Takes the largest reduction since the last frame, in dB, and the frame
        interval in seconds. */
    void push (float reductionDb, float frameSeconds = 1.0f / 30.0f);

    void setLegend (juce::String newLegend);

private:
    /** Needle position, 0 at the rest stop on the right, 1 at full scale. */
    static float proportionFor (float reductionDb) noexcept;

    juce::String legend { "GAIN REDUCTION" };
    float shownDb { 0.0f };
    float velocity { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeter)
};

} // namespace kiln::panel
