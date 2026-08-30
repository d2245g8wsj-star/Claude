#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../dsp/Meters.h"
#include "Theme.h"

namespace crest::gui
{

/** Stereo peak meter with a scale, a peak hold line and an over indicator.

    Ballistics are done here rather than in the DSP: the audio thread only ever
    reports "loudest sample since you last asked", and the fall time, hold time
    and scale are a display decision.
*/
class LevelMeter final : public juce::Component,
                         private juce::Timer
{
public:
    LevelMeter (MeterSource& source, bool useOutputSlots, const juce::String& caption);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    static float dbToProportion (float db) noexcept;

    MeterSource& meters;
    bool         readsOutput;
    juce::String captionText;
    juce::Colour accent;

    struct Channel
    {
        float level    { -100.0f };   ///< dB, with fall time applied
        float hold     { -100.0f };
        int   holdTicks { 0 };
        bool  over      { false };
        int   overTicks { 0 };
    };

    Channel channels[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

/** Horizontal bar showing how much the clipper is taking off the peaks. */
class ReductionMeter final : public juce::Component,
                             private juce::Timer
{
public:
    explicit ReductionMeter (MeterSource& source);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    MeterSource& meters;
    float        reductionDb { 0.0f };
    float        holdDb      { 0.0f };
    int          holdTicks   { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ReductionMeter)
};

} // namespace crest::gui
