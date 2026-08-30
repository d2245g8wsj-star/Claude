#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Shaper.h"
#include "../dsp/Meters.h"

namespace crest::panel
{

// Taken from the plate art, so anything drawn on top belongs to the same
// picture as the parts that are baked in.
inline const juce::Colour ink    { 0xff3a352e };
inline const juce::Colour cream  { 0xffe4dcc8 };
inline const juce::Colour amber  { 0xffc77b3c };
inline const juce::Colour recess { 0xff181510 };

juce::Font plateFont (float height, bool bold = true);

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

/** A switch in the plate's own idiom: cream face, amber when engaged. */
class Switch final : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    Switch (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
            const juce::String& caption);

    /** For the three whose shapes are already printed on the plate. */
    void setDrawFace (bool shouldDraw) { drawFace = shouldDraw; repaint(); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }

private:
    juce::RangedAudioParameter& parameter;
    juce::String captionText;
    bool on { false }, hover { false }, drawFace { true };

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

/** A row of segments for the short choice parameters. */
class Bank final : public juce::Component,
                   public juce::SettableTooltipClient
{
public:
    Bank (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          const juce::String& caption);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> strip() const;
    juce::Rectangle<float> cell (int) const;

    juce::RangedAudioParameter& parameter;
    juce::StringArray           choices;
    juce::String                captionText;
    int                         index { 0 };

    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Bank)
};

} // namespace crest::panel
