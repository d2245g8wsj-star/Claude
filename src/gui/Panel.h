#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Shaper.h"
#include "../dsp/Meters.h"
#include "../dsp/WaveformTap.h"

namespace crest::panel
{

/** The two live views in Crest's display. The rest of the panel is drawn
    with the range's shared parts (shared/indiewave/ui). */

inline const juce::Colour ink   { 0xff3a352e };
inline const juce::Colour amber { 0xffc77b3c };

juce::Font plateFont (float height, bool bold = true);

/** The transfer curve, drawn into the inset in the display. */
class Scope final : public juce::Component,
                    private juce::Timer
{
public:
    Scope (juce::AudioProcessorValueTreeState&, MeterSource&, const std::atomic<float>& liveKnee);

    /** Drawn on a dark screen: light lines and a brighter trace. */
    void setOnDark (bool dark) { onDark = dark; repaint(); }

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
    bool  onDark   { false };

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

    /** Drawn on a dark screen: light lines and a brighter trace. */
    void setOnDark (bool dark) { onDark = dark; repaint(); }

    /** A region inside this one that belongs to something else. */
    void setExcluded (juce::Rectangle<int> area) { excluded = area; repaint(); }

private:
    void timerCallback() override;

    const WaveformTap& tap;
    juce::Rectangle<int> excluded;
    int lastSeen { 0 };
    bool onDark { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Waveform)
};

} // namespace crest::panel
