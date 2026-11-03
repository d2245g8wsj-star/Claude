#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    Swell's controls: satin graphite, turned aluminium knobs inside a coloured
    value arc, pill switches and LED bar meters. Drawn in design units, like
    the rest of the range; the editor scales the whole face.
*/
namespace swell::modern
{

namespace colour
{
    const juce::Colour plateTop    { 0xff24272b };
    const juce::Colour plateBottom { 0xff1a1c1f };
    const juce::Colour section     { 0xff262a2f };
    const juce::Colour track       { 0xff34383d };
    const juce::Colour screen      { 0xff0c0e10 };
    const juce::Colour ink         { 0xffe7eaec };
    const juce::Colour dim         { 0xff8b9298 };
    const juce::Colour lo          { 0xffff6b4f };
    const juce::Colour mid         { 0xffffb23a };
    const juce::Colour hi          { 0xff3fc9e8 };
}

juce::Font sans (float height, bool bold = true);
juce::Font mono (float height);

void drawText (juce::Graphics&, const juce::String&, juce::Rectangle<float>, juce::Font, juce::Colour,
               juce::Justification = juce::Justification::centred, float tracking = 0.0f);

/** A raised, rounded section behind a band. */
void drawSection (juce::Graphics&, juce::Rectangle<float>);

/** An aluminium knob in a value arc, its name above and its value below. */
class ArcKnob final : public juce::Slider
{
public:
    struct Style
    {
        float radius = 26.0f;
        juce::Colour colour = colour::ink;
        bool bipolar = false;              ///< the arc grows from the middle
        juce::String name;
        std::function<juce::String (double)> format;
    };

    ArcKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, Style);

    void paint (juce::Graphics&) override;

    /** Bounds that put the knob's centre at `centre`. */
    static juce::Rectangle<int> boundsAt (juce::Point<float> centre, float radius);

private:
    Style style;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ArcKnob)
};

/** A rounded pill switch, lit in its colour when on. */
class Pill final : public juce::Button
{
public:
    Pill (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, juce::Colour, juce::String label);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour colour;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pill)
};

/** A small LED that lights with how hard a band's curve is driven. */
class DriveLed final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit DriveLed (juce::Colour);

    void paint (juce::Graphics&) override;

    /** Called at the editor's frame rate with the latest drive, 0 to 1. */
    void push (float drive);

private:
    juce::Colour colour;
    float level { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DriveLed)
};

/** Output level as horizontal LED bars, one per channel, with a held peak
    readout and a scale underneath. */
class BarMeter final : public juce::Component
{
public:
    BarMeter();

    void paint (juce::Graphics&) override;

    /** Which channels to show, and the letter beside each bar. */
    void setChannels (juce::StringArray letters);

    /** Linear peaks, one per channel shown, at the editor's frame rate. */
    void push (const std::vector<float>& peaks);

private:
    static float position (float db);

    juce::StringArray letters { "L", "R" };
    std::vector<float> levels { -100.0f, -100.0f };
    float held { -100.0f };
    int heldFor { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BarMeter)
};

/** Three segments in a capsule; the lit one is the choice. */
class Segments final : public juce::Component
{
public:
    explicit Segments (juce::StringArray labels);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void setSelected (int);
    int  getSelected() const noexcept { return selected; }

    std::function<void (int)> onChange;

private:
    juce::StringArray labels;
    int selected { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segments)
};

} // namespace swell::modern
