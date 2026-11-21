#pragma once

/*
    The IndieWave panel kit: what every unit in the range shares.

    The maker's mark (a sine in a ring, as on Tide), rack ears, screws and the
    controls a hardware panel is made of. Each plugin picks its own plate and
    accent colours; the shapes stay the same, so the range reads as one family.
*/

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace indiewave::ui
{

juce::Font font (float height, bool bold = true);

void drawLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>, float height,
                juce::Colour, juce::Justification = juce::Justification::centred,
                bool bold = true, float tracking = 0.0f);

/** A matte anodised plate, shaded top to bottom, with a faint brushed grain
    seeded so it is the same plate every time it is drawn. */
void drawPlate (juce::Graphics&, juce::Rectangle<float>, juce::Colour top, juce::Colour bottom,
                juce::uint32 seed = 0x494457);

/** A rack ear: the plate folded, with two long rounded mounting slots. */
void drawRackEar (juce::Graphics&, juce::Rectangle<float>, bool leftSide);

void drawScrew (juce::Graphics&, juce::Point<float> centre, float diameter);

/** The maker's mark: a sine wave in a ring, INDIEWAVE beneath. `scale` 1 is
    a 40 unit ring. */
void drawLogo (juce::Graphics&, juce::Point<float> centre, float scale, juce::Colour ink,
               juce::Colour dim, bool withName = true);

/** A raised sub panel with rounded corners, a little lighter than the plate. */
void drawSubpanel (juce::Graphics&, juce::Rectangle<float>, float corner,
                   juce::Colour top, juce::Colour bottom);

/** A round indicator. `brightness` 0 is off, 1 fully lit. */
void drawLed (juce::Graphics&, juce::Point<float> centre, float radius, juce::Colour, float brightness);

// ---------------------------------------------------------------------------

/**
    A knurled black aluminium knob with a machined top and a white pointer,
    in a ring of LEDs that light up to its position: from the start, or from
    the middle for controls that cut and boost.
*/
class RingKnob final : public juce::Slider
{
public:
    struct Style
    {
        float knobRadius = 30.0f;
        int   leds = 21;
        float ledRadius = 2.1f;
        bool  bipolar = false;
        std::function<juce::Colour (float position)> ledColour;  ///< position 0..1 along the ring
        juce::Colour ink, dim;
        juce::String lowLegend, highLegend, name, subName;
        float nameHeight = 12.0f, legendHeight = 9.5f;
    };

    RingKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, Style);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

    /** The square a knob of this style needs, centred on its spindle. */
    static juce::Rectangle<int> boundsAt (juce::Point<float> centre, const Style&);

private:
    Style style;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RingKnob)
};

/**
    A square push button. White keys light their cap; dark keys are small
    black squares with an ON lamp above. Either can be tied to a parameter or
    left free with an onClick.
*/
class Key final : public juce::Button
{
public:
    enum class Kind { white, dark };

    struct Style
    {
        Kind kind = Kind::white;
        juce::Colour lit { 0xfffffdf4 };
        juce::Colour ink, dim;
        juce::String above, below, below2;   ///< legends printed on the plate
        bool onLamp = false;                  ///< the small ON lamp of a dark key
        float capSize = 30.0f;
        float capWidth = 0.0f;                ///< wider than tall when set
        juce::String capLabel;                ///< printed on the cap itself
        juce::Colour capLabelOn { 0xff1a1206 }, capLabelOff { 0xff2b2b2b };
        float legendHeight = 9.0f;
    };

    Key (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, Style);
    explicit Key (Style);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Style style;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Key)
};

/**
    Tide's black bakelite knob: a fluted phenolic skirt, a polished dome and a
    white index line, with a printed dot at every step (or along the travel)
    and legends at the values they name.
*/
class BakeliteKnob final : public juce::Slider
{
public:
    struct Style
    {
        float radius = 26.0f;
        int dots = 0;                       ///< 0: one per step of the parameter
        juce::Colour dotColour, ink, dim;
        std::vector<std::pair<float, juce::String>> legends;  ///< parameter value, text
        float legendRadius = 0.0f;          ///< 0: radius + 18
        float legendHeight = 8.5f;
        juce::String name;
        float nameHeight = 9.5f, nameGap = 12.0f;
        juce::Colour cap {};                ///< a coloured cap, or transparent for black
    };

    BakeliteKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, Style);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

    static juce::Rectangle<int> boundsAt (juce::Point<float> centre, float radius);

private:
    Style style;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BakeliteKnob)
};

/** A chrome bat toggle on a hex nut. Up (or right) is on. */
class BatToggle final : public juce::Button
{
public:
    BatToggle (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, bool horizontal = false);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    bool horizontal;
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BatToggle)
};

/** A row of white keys that picks one choice of a parameter, each key's name
    above it. */
class ChoiceKeys final : public juce::Component
{
public:
    ChoiceKeys (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
                juce::StringArray labels, std::vector<int> order, juce::Colour ink);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int getSelected() const noexcept { return selected; }
    std::function<void (int)> onChange;

private:
    juce::Rectangle<float> keyArea (int slot) const;

    juce::StringArray labels;
    std::vector<int> order;   ///< parameter index shown in each slot
    juce::Colour ink;
    int selected { 0 };
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceKeys)
};

/** A vertical LED ladder with its scale beside it, fed a level in dBFS. */
class LedLadder final : public juce::Component
{
public:
    LedLadder (std::vector<float> stepsDb, juce::StringArray labels, bool labelsLeft, juce::Colour ink);

    void push (float levelDb);
    void paint (juce::Graphics&) override;

private:
    std::vector<float> steps;
    juce::StringArray labels;
    bool labelsLeft;
    juce::Colour ink;
    float shown { -100.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LedLadder)
};

/** A moving coil meter with a lamp behind a cream face. */
class VuMeter final : public juce::Component
{
public:
    /** `marks`: scale position 0..1 and its legend. `toPosition` maps the
        pushed value to a scale position. */
    VuMeter (std::vector<std::pair<float, juce::String>> marks, std::function<float (float)> toPosition,
             juce::String legend, float redFrom = 2.0f);

    void push (float value);
    void setLegend (juce::String);
    void paint (juce::Graphics&) override;

private:
    std::vector<std::pair<float, juce::String>> marks;
    std::function<float (float)> toPosition;
    juce::String legend;
    float redFrom;
    float needle { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VuMeter)
};

/** A small backlit display of one or two lines. */
class Display final : public juce::Component
{
public:
    explicit Display (juce::Colour glow = juce::Colour (0xff3dff9a));
    void setLines (juce::String top, juce::String bottom);
    void paint (juce::Graphics&) override;

private:
    juce::Colour glow;
    juce::String top, bottom;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Display)
};

/** The handle that folds a unit out to its second channel and back. */
class FoldTab final : public juce::Button
{
public:
    enum class Edge { right, bottom };

    FoldTab (Edge, juce::Colour ink, juce::Colour fill);
    void setOpen (bool);
    void setCaption (juce::String closedText, juce::String openText);
    void setColours (juce::Colour ink, juce::Colour fill);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    Edge edge;
    juce::Colour ink, fill;
    bool open { false };
    juce::String closedText { juce::String (juce::CharPointer_UTF8 ("M/S · DUAL")) }, openText { "FOLD" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FoldTab)
};

} // namespace indiewave::ui
