#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Shaper.h"
#include "../dsp/Meters.h"
#include "../dsp/WaveformTap.h"

namespace crest::panel
{

// Taken from the plate art, so anything drawn on top belongs to the same
// picture as the parts that are baked in.
inline const juce::Colour ink    { 0xff3a352e };
inline const juce::Colour cream  { 0xffe4dcc8 };
inline const juce::Colour amber  { 0xffc77b3c };
inline const juce::Colour recess { 0xff181510 };

/** The display window's own surface, sampled from the plate render so anything
    drawn into that window sits on the same colour the art printed. */
inline const juce::Colour field  { 0xfffaf7f0 };

juce::Font plateFont (float height, bool bold = true);

/** A group enclosure milled into the plate.

    Without one the controls sit on a texture rather than in a panel, which is
    the single biggest reason a laid-out set of knobs reads as loose parts
    instead of an instrument. Drawn behind the controls, so they sit in it.

    Drawn as a cut rather than an outline: a floor shaded from the top down,
    walls that fall away from the edge, a dark chamfer where the light does not
    reach and a lit one where it does.
*/
void drawGroupWell (juce::Graphics&, juce::Rectangle<float>, float corner = 7.0f);

/** Engraved lettering, for the labels the plate art does not carry. */
void drawPlateLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                     float height = 9.5f);

/** The same, letterspaced.

    The plate's own lettering is tracked out, so anything drawn next to it has
    to be as well or it reads as a different panel.
*/
void drawTrackedLabel (juce::Graphics&, const juce::String&, juce::Rectangle<float>,
                       float height, float tracking,
                       juce::Justification = juce::Justification::centred);

/** The transfer window inside the display.

    The plate used to print this at the right hand end of the display, which is
    where the waveform's newest column lands, so it covered material exactly as
    it arrived. It is drawn here instead, wherever the editor puts it.
*/
void drawDisplayInset (juce::Graphics&, juce::Rectangle<float>);

/** Loads a vertical filmstrip once and hands out frames.

    The frame count comes from the image rather than a constant, so replacing a
    48 frame test render with the final 128 frame one is a file copy.
*/
class Filmstrip
{
public:
    Filmstrip (const void* data, int size);

    void drawFrame (juce::Graphics&, juce::Rectangle<float> area, int frame) const;
    void drawProportion (juce::Graphics&, juce::Rectangle<float> area, float proportion) const;

    int getNumFrames() const noexcept { return numFrames; }
    bool isValid() const noexcept     { return image.isValid(); }

private:
    juce::Image image;
    int numFrames { 1 };
    int frameHeight { 1 };
};

/** A rotary painted from a filmstrip. Its legend is baked into the plate, so
    the component draws nothing but the knob itself. */
class Knob final : public juce::Slider
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          const Filmstrip& strip);

    void paint (juce::Graphics&) override;

    /** Maps parameter positions onto specific frames, for a switch whose
        detents do not run one to one with the parameter's choices. */
    void setFrameMap (juce::Array<int> frames) { frameMap = std::move (frames); }

private:
    const Filmstrip& filmstrip;
    juce::Array<int> frameMap;

    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A column of vintage LED segments over a recessed window. */
class LedMeter final : public juce::Component,
                       private juce::Timer
{
public:
    LedMeter (MeterSource&, bool readsOutput,
              const juce::Image& off, const juce::Image& green,
              const juce::Image& amberLed, const juce::Image& red);

    void paint (juce::Graphics&) override;

    /** The held peak, so a readout can show the figure the bar is drawing. */
    float getHoldDb() const noexcept { return hold; }

private:
    void timerCallback() override;

    MeterSource& meters;
    bool output;

    const juce::Image &offImage, &greenImage, &amberImage, &redImage;

    float level { -100.0f };
    float hold  { -100.0f };
    int   holdTicks { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LedMeter)
};

/** The transfer curve, drawn into the window that is etched into the plate. */
class Scope final : public juce::Component,
                    private juce::Timer
{
public:
    Scope (juce::AudioProcessorValueTreeState&, MeterSource&, const std::atomic<float>& liveKnee);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuild();
    juce::Point<float> toPixels (float x, float y) const;

    juce::AudioProcessorValueTreeState& apvts;
    MeterSource&                        meters;
    const std::atomic<float>&           adaptedKnee;

    Shaper     shaper, baseShaper;
    juce::Path curvePath, basePath;
    juce::Rectangle<float> plot;

    float lastKnee { -1.0f }, lastBias { -1.0f }, lastLive { -1.0f };
    int   lastMode { -1 };
    float drive    { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Scope)
};

/** The scrolling picture of what the clipper is doing.

    Two envelopes, drawn one over the other: what went into the shaper and what
    came out. The gap between them is the material that was removed, so the
    clipping is shown rather than described. Scrolls right to left.
*/
class Waveform final : public juce::Component,
                       private juce::Timer
{
public:
    explicit Waveform (const WaveformTap&);

    void paint (juce::Graphics&) override;

    /** A region inside this one that belongs to something else. */
    void setExcluded (juce::Rectangle<int> area) { excluded = area; repaint(); }

private:
    void timerCallback() override;

    const WaveformTap& tap;
    juce::Rectangle<int> excluded;
    int lastSeen { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Waveform)
};

/** What is over the display.

    A pane of glass, drawn on top of everything in the window: a specular band
    across it, the shade the top edge casts, and the line of light the bottom
    inner edge catches. Nothing is behind glass on a flat bake, and it is most
    of what separates a photograph of an instrument from a drawing of one.
*/
class Glass final : public juce::Component
{
public:
    Glass();

    void paint (juce::Graphics&) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Glass)
};

/** A switch in the plate's own idiom: cream face, amber when engaged. */
class Switch final : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    Switch (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
            const juce::String& caption);

    /** For the ones whose shape and legend are already printed on the plate:
        only the engaged state is drawn. */
    void setDrawFace (bool shouldDraw) { drawFace = shouldDraw; repaint(); }
    void setDrawLegend (bool shouldDraw) { drawLegendText = shouldDraw; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    juce::RangedAudioParameter& parameter;
    juce::String captionText;
    bool on { false }, hover { false }, drawFace { true }, drawLegendText { true };

    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Switch)
};

/** A momentary version of the same, for actions rather than states. */
class Push final : public juce::Component
{
public:
    Push (const juce::String& caption, std::function<void()> onClick);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { held = false; repaint(); }

    void setLit (bool shouldBeLit)      { if (lit != shouldBeLit) { lit = shouldBeLit; repaint(); } }
    void setDrawFace (bool shouldDraw)  { drawFace = shouldDraw; repaint(); }

private:
    juce::String          captionText;
    std::function<void()> callback;
    bool held { false }, lit { false }, drawFace { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Push)
};

/** A recessed plate carrying one figure, with its name engraved above it.

    A control surface that only shows a value while you are touching it reads
    as a mockup. These stay on.
*/
class Readout final : public juce::Component
{
public:
    explicit Readout (const juce::String& caption);

    void setValue (const juce::String&);
    void paint (juce::Graphics&) override;

private:
    juce::String captionText, valueText { "--" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Readout)
};

/** A row of segments for the short choice parameters. */
class Bank final : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    Bank (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          const juce::String& caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** The wells and the heading are printed on the plate, so only the value
        text and the engaged segment are drawn. */
    void setWellsAreBaked (bool baked) { bakedWells = baked; repaint(); }

private:
    juce::Rectangle<float> strip() const;
    juce::Rectangle<float> cell (int) const;

    juce::RangedAudioParameter& parameter;
    juce::StringArray           choices;
    juce::String                captionText;
    int                         index { 0 };
    bool                        bakedWells { false };

    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Bank)
};

} // namespace crest::panel
