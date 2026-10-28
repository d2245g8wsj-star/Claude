#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lacquer::panel
{

// The plate: a white powder coated 5U face with engraved, colour filled
// markings and no numbers on it. Each band has its own colour, carried by the
// dots of its switches' scales, so a setting is read by position and colour
// the way it is on the hardware.
inline const juce::Colour plateTop     { 0xfff0eee7 };
inline const juce::Colour plateBottom  { 0xffe1ded5 };
inline const juce::Colour engraving    { 0xff2b2926 };
inline const juce::Colour engravingDim { 0xff8e8a82 };

inline const juce::Colour bassColour        { 0xffc3342c };
inline const juce::Colour midBoostColour    { 0xffe0961f };
inline const juce::Colour midCutColour      { 0xff3c955a };
inline const juce::Colour trebleBellColour  { 0xff2f5fb3 };
inline const juce::Colour trebleShelfColour { 0xff7a49ae };
inline const juce::Colour trebleCutColour   { 0xff23899a };
inline const juce::Colour neutralColour     { 0xff4a4743 };

juce::Font plateFont (float height, bool bold = true);

void drawTrackedLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                       float height, float tracking = 1.5f,
                       juce::Justification = juce::Justification::centred,
                       juce::Colour = engraving);

/** The symbols engraved in place of words. */
enum class Glyph
{
    lowShelfBoost, lowShelfCut, bellBoost, bellCut, trebleBoost, highShelfCut,
    width, shift, buffer, transformer, power, link, output
};

void drawGlyph (juce::Graphics&, Glyph, juce::Rectangle<float> area, juce::Colour);

/** A rack ear cut from the same plate: two slots, a screw in each. */
void drawRackEar (juce::Graphics&, juce::Rectangle<float>);

/** A countersunk panel screw. */
void drawScrew (juce::Graphics&, juce::Point<float> centre, float diameter);

/** A black stepped switch knob over a ring of coloured dots, one per position. */
class SteppedKnob final : public juce::Slider
{
public:
    enum class Size { large, medium, small };

    /** One position's mark on the plate. */
    struct Mark
    {
        juce::Colour colour;
        float size { 1.0f };      ///< relative dot diameter
        bool  bar  { false };     ///< a short engraved line instead of a dot
    };

    SteppedKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
                 Size, std::vector<Mark> marks);

    void paint (juce::Graphics&) override;

    /** Marks for an amount switch: dots that grow towards the end of travel. */
    static std::vector<Mark> rampMarks (int positions, juce::Colour);

    /** Marks for a frequency switch: one colour, equal dots. */
    static std::vector<Mark> evenMarks (int positions, juce::Colour);

private:
    Size size;
    std::vector<Mark> marks;

    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SteppedKnob)
};

/** A bat handle toggle in a chrome bushing; up is on. */
class Toggle final : public juce::Button
{
public:
    Toggle (juce::AudioProcessorValueTreeState&, const juce::String& parameterID);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toggle)
};

/** The red jewel pilot lamp. */
class PilotLamp final : public juce::Component
{
public:
    PilotLamp();

    void paint (juce::Graphics&) override;
    void setLit (bool);

private:
    bool lit { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PilotLamp)
};

} // namespace lacquer::panel
