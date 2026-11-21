#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

namespace nazare
{

class Face;

/**
    The rendered unit: a front and a rear panel, each one plate image from the
    Blender scene with its moving parts drawn from filmstrips. Everything is
    laid out in the panel's own 1600 x 440 points and scaled as a whole.
*/
class NazareEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NazareEditor (NazareProcessor&);
    ~NazareEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void flip();
    bool showingRear() const noexcept { return rear; }

private:
    void timerCallback() override;
    void applyTransforms();

    NazareProcessor& processor;
    std::unique_ptr<Face> front, back;
    bool rear = false;
    double flipPhase = 0.0;          // 0..1 while turning over
    bool flipping = false;
    juce::ComponentBoundsConstrainer constrainer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NazareEditor)
};

} // namespace nazare
