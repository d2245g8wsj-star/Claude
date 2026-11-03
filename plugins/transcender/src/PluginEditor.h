#pragma once

#include "PluginProcessor.h"

namespace transcender
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1000;
inline constexpr int designHeight = 580;

class TranscenderLook final : public juce::LookAndFeel_V4
{
public:
    TranscenderLook();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float position,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool down, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
};

/** A knob with its name above and its value below. */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, juce::String label);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::Slider slider;
    juce::Label  valueLabel;
    juce::String label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A drop down for a choice parameter, with its name above. */
class Choice final : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, juce::String label);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    juce::ComboBox box;
    juce::String label;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Choice)
};

/** The sixteen steps of the gate pattern, lit as the gate plays them. */
class StepLights final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void set (int pattern, int step, bool enabled);

private:
    int pattern { 0 }, step { -1 };
    bool enabled { false };
};

class TranscenderEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit TranscenderEditor (TranscenderProcessor&);
    ~TranscenderEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    class Face final : public juce::Component
    {
    public:
        explicit Face (TranscenderProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

    private:
        void choosePreset (int index);

        TranscenderProcessor& processor;

        juce::ComboBox     presets;
        juce::TextButton   previous { "<" }, next { ">" };

        Choice wave;
        Knob   detune, octave, stereo;
        juce::ToggleButton poly { "Poly" };
        juce::AudioProcessorValueTreeState::ButtonAttachment polyAttachment;
        Knob   glide;
        Knob   cutoff, resonance, envAmount;
        Knob   attack, decay, sustain, release;
        Knob   lfoRate, lfoDepth;
        Knob   volume;
        Knob   gateDepth, gateLength;
        Choice gateRate, gatePattern;
        StepLights steps;
        Choice delayTime;
        Knob   delayMix, delayFeedback;

        int shownProgram { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    TranscenderProcessor& processor;
    TranscenderLook       look;
    juce::TooltipWindow   tooltips { this, 650 };
    Face                  face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TranscenderEditor)
};

} // namespace transcender
