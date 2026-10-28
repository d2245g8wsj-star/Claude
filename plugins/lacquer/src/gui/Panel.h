#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace lacquer::panel
{

/** The two finishes the hardware was built in. */
enum class Finish { white = 0, black };

/** The colours that change with the finish. Each band has its own ink,
    carried by the dots of its switches' scales and by its symbol, so a
    setting is read by position and colour the way it is on the hardware. */
enum class Ink { bass = 0, bassCut, midBoost, midCut, trebleBell, trebleShelf, trebleCut, neutral, numInks };

struct Palette
{
    juce::Colour plateTop, plateBottom;
    juce::Colour engraving, engravingDim;

    /** The lit wall of an engraved cut, drawn just below each mark. */
    juce::Colour lip;

    /** Specks of the coating, light and dark. */
    juce::Colour speckLight, speckDark;

    /** A highlight round the edge of a knob, so black knobs read on black. */
    juce::Colour knobRim;

    std::array<juce::Colour, (size_t) Ink::numInks> inks;

    juce::Colour ink (Ink i) const noexcept { return inks[(size_t) i]; }

    static const Palette& forFinish (Finish);
};

juce::Font plateFont (float height, bool bold = true);

void drawTrackedLabel (juce::Graphics&, const Palette&, const juce::String&, juce::Rectangle<float>,
                       float height, float tracking, juce::Justification, juce::Colour);

/** The symbols engraved in place of words. */
enum class Glyph
{
    lowShelfBoost, lowShelfCut, bellBoost, bellCut, trebleBoost, highShelfCut,
    width, shift, buffer, transformer, power, link, output
};

void drawGlyph (juce::Graphics&, const Palette&, Glyph, juce::Rectangle<float> area, juce::Colour);

/** A rack ear cut from the same plate: two slots, a screw in each. */
void drawRackEar (juce::Graphics&, const Palette&, juce::Rectangle<float>);

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
        Ink   ink;
        float size { 1.0f };      ///< relative dot diameter
        bool  bar  { false };     ///< a short engraved line instead of a dot
    };

    SteppedKnob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
                 Size, std::vector<Mark> marks);

    void paint (juce::Graphics&) override;

    /** Marks for an amount switch: dots that grow towards the end of travel. */
    static std::vector<Mark> rampMarks (int positions, Ink);

    /** Marks for a frequency switch: one colour, equal dots. */
    static std::vector<Mark> evenMarks (int positions, Ink);

    void setPalette (const Palette& p) { palette = &p; repaint(); }

private:
    const Palette* palette { &Palette::forFinish (Finish::white) };
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

    void setPalette (const Palette& p) { palette = &p; repaint(); }

private:
    const Palette* palette { &Palette::forFinish (Finish::white) };
    juce::AudioProcessorValueTreeState::ButtonAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toggle)
};

/** Two paint chips, white and black, for choosing the panel finish. The
    choice is kept in the plugin state rather than as an automatable
    parameter: it changes how the unit looks, not how it sounds. */
class FinishSelector final : public juce::Component,
                             public juce::SettableTooltipClient
{
public:
    FinishSelector();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    void setFinish (Finish);

    std::function<void (Finish)> onChange;

private:
    juce::Rectangle<float> chip (Finish) const;

    Finish finish { Finish::white };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FinishSelector)
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
