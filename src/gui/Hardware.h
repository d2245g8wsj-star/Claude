#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Shaper.h"
#include "../dsp/Meters.h"

namespace crest::hw
{

// A panel palette rather than a UI palette: these are materials, not roles.
inline const juce::Colour panelDark   { 0xff232120 };   ///< anodised main panel, warm graphite
inline const juce::Colour panelLight  { 0xff322e2b };   ///< brushed header band
inline const juce::Colour panelDeep   { 0xff151413 };   ///< recessed bottom rail
inline const juce::Colour engraved    { 0xffdcd5c6 };   ///< aged cream silkscreen
inline const juce::Colour engravedDim { 0xff9a9288 };
inline const juce::Colour knobBody    { 0xff0d0e0f };
inline const juce::Colour chrome      { 0xff938e85 };
inline const juce::Colour lampAmber   { 0xffffa42b };
inline const juce::Colour lampRed     { 0xffff4a38 };
inline const juce::Colour lampGreen   { 0xff86d46a };
inline const juce::Colour glass       { 0xff0d1012 };

juce::Font panelFont (float height, bool bold = true);

/** Vertical gradient plus fine horizontal streaks. Rendered once into an image
    by the caller, because generating the streaks every repaint is wasteful and
    a moving grain pattern reads as noise rather than metal. */
juce::Image renderBrushedPanel (int width, int height, juce::Colour base, int seed);

/** Engraved lettering: the glyph sits in shadow with a lit edge below it, which
    is what a milled channel in a painted panel actually looks like. */
void drawEngraved (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                   juce::Justification, float fontHeight, float tracking,
                   juce::Colour colour = engraved);

void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float angle);

/** A window milled into the panel: dark inside, shadow along the top edge,
    a lit edge along the bottom. */
void drawRecess (juce::Graphics&, juce::Rectangle<float>, float corner);

/** The opposite: something sitting proud of the panel. */
void drawRaised (juce::Graphics&, juce::Rectangle<float>, float corner, juce::Colour face);

// ---------------------------------------------------------------------------

/** A machined aluminium knob with a knurled rim, drawn rather than sampled.

    The scale ring around it is engraved into the panel and the pointer travels
    over it, so the control reads at a glance from across the room, which is the
    one thing hardware has always done better than software.
*/
class Knob final : public juce::Slider
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          const juce::String& caption, bool bipolar = false);

    void paint (juce::Graphics&) override;

    void setDiameter (float d) { diameter = d; }

private:
    juce::String captionText;
    bool  isBipolar { false };
    float diameter  { 78.0f };

    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A detented rotary switch for the character selector.

    A choice with five positions is a switch on real hardware, not a row of
    buttons, and the pointer position is readable without reading any text.
*/
class RotarySwitch final : public juce::Component,
                           public juce::SettableTooltipClient
{
public:
    RotarySwitch (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
                  const juce::String& caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void selectFromAngle (juce::Point<float>);
    float angleForIndex (int) const;

    juce::RangedAudioParameter& parameter;
    juce::StringArray           choices;
    juce::String                captionText;
    int                         index { 0 };

    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RotarySwitch)
};

/** A backlit pushbutton: dark lens, warm glow and a lit legend when engaged. */
class LampButton final : public juce::Component,
                         public juce::SettableTooltipClient
{
public:
    LampButton (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
                const juce::String& caption, juce::Colour lampColour = lampAmber);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    juce::RangedAudioParameter& parameter;
    juce::String captionText;
    juce::Colour lamp;
    bool on    { false };
    bool hover { false };

    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LampButton)
};

/** A row of small lamps for the short choice parameters. */
class LampBank final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    LampBank (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
              const juce::String& caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cellBounds (int) const;

    juce::RangedAudioParameter& parameter;
    juce::StringArray           choices;
    juce::String                captionText;
    int                         index { 0 };

    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LampBank)
};

/** LED ladder behind a recessed lens. */
class LadderMeter final : public juce::Component,
                          private juce::Timer
{
public:
    LadderMeter (MeterSource&, bool readsOutput, const juce::String& caption);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    MeterSource& meters;
    bool         output;
    juce::String captionText;

    struct Channel { float level { -100.0f }; float hold { -100.0f }; int holdTicks { 0 }; };
    Channel channels[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LadderMeter)
};

/** Gain reduction on a horizontal ladder, plus the sidechain indicator. */
class ClipMeter final : public juce::Component,
                        private juce::Timer
{
public:
    ClipMeter (juce::AudioProcessorValueTreeState&, MeterSource&,
               const std::atomic<bool>& sidechainFlag);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    juce::AudioProcessorValueTreeState& apvts;
    MeterSource&                        meters;
    const std::atomic<bool>&            sidechain;

    float reductionDb { 0.0f };
    float holdDb      { 0.0f };
    int   holdTicks   { 0 };
    bool  limited     { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClipMeter)
};

/** The transfer curve behind glass, drawn from the same Shaper as the audio. */
class ScopeWindow final : public juce::Component,
                          private juce::Timer
{
public:
    ScopeWindow (juce::AudioProcessorValueTreeState&, MeterSource&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuild();
    juce::Point<float> toPixels (float x, float y) const;

    juce::AudioProcessorValueTreeState& apvts;
    MeterSource&                        meters;

    Shaper     shaper;
    juce::Path curvePath;
    juce::Rectangle<float> plot;

    float lastKnee { -1.0f }, lastBias { -1.0f };
    int   lastMode { -1 };
    float drive    { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScopeWindow)
};

} // namespace crest::hw
