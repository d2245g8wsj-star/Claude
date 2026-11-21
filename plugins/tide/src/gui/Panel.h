#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace tide::panel
{

// A matte black anodised plate with white screen printing, black bakelite
// knobs, chrome toggles and lit push buttons.
inline const juce::Colour plateTop    { 0xff1d1d1e };
inline const juce::Colour plateBottom { 0xff141415 };
inline const juce::Colour ink         { 0xffeceae4 };
inline const juce::Colour inkDim      { 0xffa4a29c };
inline const juce::Colour rule        { 0xff8d8b86 };
inline const juce::Colour recess      { 0xff070707 };
inline const juce::Colour cueGreen    { 0xff4dff6a };

juce::Font plateFont (float height, bool bold = false);

void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float height,
                juce::Justification = juce::Justification::centred, juce::Colour = ink,
                bool bold = false, float tracking = 0.0f);

/** A rack ear with two long rounded mounting slots. */
void drawRackEar (juce::Graphics&, juce::Rectangle<float>, bool leftSide);

/** A small black panel screw. */
void drawScrew (juce::Graphics&, juce::Point<float> centre, float diameter);

/** Small curve glyphs printed beside the shape switches. */
void drawShelfGlyph (juce::Graphics&, juce::Rectangle<float>, bool lowShelf, juce::Colour);
void drawPeakGlyph  (juce::Graphics&, juce::Rectangle<float>, juce::Colour);

/**
    A stepped rotary under a black bakelite knob: a phenolic skirt with a
    fluted grip, a polished domed cap and a white index line, with the
    switch's own scale printed around it.
*/
class BakeliteKnob final : public juce::Slider
{
public:
    /** A printed legend at the position of a parameter value (choice index). */
    struct Legend
    {
        float value;
        juce::String text;
    };

    BakeliteKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
                  float knobRadius, float startDegrees, float endDegrees,
                  std::vector<Legend>, float legendRadius, float legendHeight);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

    /** A unit printed one step past the end of the scale, e.g. "Hz". */
    void setUnit (juce::String text) { unit = std::move (text); repaint(); }

private:
    float radius, legendRadius, legendHeight;
    std::vector<Legend> legends;
    juce::String unit;

    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BakeliteKnob)
};

/** A two position chrome bat toggle. Up is on. */
class Toggle final : public juce::Button
{
public:
    Toggle (juce::AudioProcessorValueTreeState&, const juce::String& parameterID);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toggle)
};

/** A square latching push button whose cap lights when it is in. */
class LitButton final : public juce::Button
{
public:
    LitButton (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, juce::String legend,
               float legendHeight);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    float legendHeight;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LitButton)
};

/** The three position cue switch, a bat toggle thrown left, centre or
    right. Click a side to throw it there; click the side it is on, or the
    middle, to centre it. */
class CueSwitch final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    CueSwitch (juce::AudioProcessorValueTreeState&, const juce::String& parameterID);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int getPosition() const noexcept { return position; }

private:
    juce::RangedAudioParameter& parameter;
    juce::ParameterAttachment attachment;
    int position { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CueSwitch)
};

/** A small round indicator. */
class Led final : public juce::Component
{
public:
    explicit Led (juce::Colour);

    void paint (juce::Graphics&) override;
    void setLit (bool);

private:
    juce::Colour colour;
    bool lit { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Led)
};

} // namespace tide::panel
