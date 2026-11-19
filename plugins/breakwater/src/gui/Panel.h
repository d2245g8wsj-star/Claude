#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace breakwater::panel
{

// A dark instrument face with one cool accent for what the limiter is doing to
// the signal and one warm one for what it takes away. Everything else stays in
// greys, so the two colours carry the meaning.
inline const juce::Colour faceTop      { 0xff1d2127 };
inline const juce::Colour faceBottom   { 0xff14171b };
inline const juce::Colour well         { 0xff0d0f12 };
inline const juce::Colour wellEdge     { 0xff2a3039 };
inline const juce::Colour text         { 0xffe3e7ec };
inline const juce::Colour textDim      { 0xff8a939e };
inline const juce::Colour textFaint    { 0xff505864 };
inline const juce::Colour accent       { 0xff46d1c4 };   ///< output, active controls
inline const juce::Colour reduction    { 0xffff7a45 };   ///< gain reduction
inline const juce::Colour inputShade   { 0xff5d6876 };
inline const juce::Colour warning      { 0xffff4d5e };

juce::Font uiFont (float height, bool bold = false);

void drawLabel (juce::Graphics&, const juce::String& label, juce::Rectangle<float>, float height,
                juce::Colour = textDim, juce::Justification = juce::Justification::centred,
                bool bold = true, float tracking = 1.2f);

/** A recessed panel: dark floor, a hairline rim, a soft inner shadow. */
void drawWell (juce::Graphics&, juce::Rectangle<float>, float corner = 6.0f);

/** A rotary with an arc track, its name above and its value below. */
class Knob final : public juce::Slider
{
public:
    enum class Size { large, small };

    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
          const juce::String& name, Size, bool bipolar = false);

    void paint (juce::Graphics&) override;

private:
    juce::String name;
    Size size;
    bool bipolar;
    juce::RangedAudioParameter& parameter;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A row of buttons selecting one choice of a parameter. */
class Segmented final : public juce::Component,
                        public juce::SettableTooltipClient
{
public:
    Segmented (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
               juce::StringArray labels = {});

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

    int getSelected() const noexcept { return selected; }

    /** Hover text per choice. */
    void setDescriptions (juce::StringArray);

private:
    juce::Rectangle<float> segment (int index) const;
    void mouseMove (const juce::MouseEvent&) override;

    juce::StringArray labels, descriptions;
    int selected { 0 };
    juce::ParameterAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Segmented)
};

/** A latching pill with a lamp, for an on/off parameter. */
class Toggle final : public juce::Button
{
public:
    Toggle (juce::AudioProcessorValueTreeState&, const juce::String& parameterID,
            const juce::String& legend, juce::Colour lamp = accent);

    /** A pill that belongs to the editor rather than to a parameter. */
    explicit Toggle (const juce::String& legend, juce::Colour lamp = accent);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour lamp;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Toggle)
};

} // namespace breakwater::panel
