#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Meters.h"

namespace breakwater::display
{

/** The main view: about ten seconds of what went in, what came out and what
    the limiter took, scrolling right to left.

    Input and output share one dB scale and are drawn as filled envelopes,
    output over input, so the grey left showing above the colour is the
    material the limiter removed. Gain reduction hangs from the top edge on
    the same scale, so a 3 dB bar is as tall as 3 dB of level.
*/
class Scope final : public juce::Component
{
public:
    explicit Scope (MeterSource&);

    void paint (juce::Graphics&) override;

    /** Pulls whatever the audio thread has written since the last call. */
    void refresh (float ceilingDb, float reductionNowDb, const juce::String& caption,
                  const juce::String& footer = {});

    static constexpr float rangeDb    = 30.0f;
    static constexpr int   numColumns = 1400;

private:
    float yForDb (float db, juce::Rectangle<float> area) const noexcept;

    MeterSource& source;
    std::vector<History::Column> columns;
    int readIndex { 0 };
    int newest    { 0 };   ///< index into columns of the most recent

    float ceilingDb { -1.0f };
    float reductionDb { 0.0f };
    float reductionHold { 0.0f };
    int   holdFrames { 0 };
    juce::String caption, footer;
};

/** Peak bars for input and output, a gain reduction bar, and their numbers. */
class Meters final : public juce::Component
{
public:
    explicit Meters (MeterSource&);

    void paint (juce::Graphics&) override;
    void refresh();

    float getReductionDb() const noexcept { return reductionDb; }

    static constexpr float floorDb = -48.0f;

private:
    struct Bar
    {
        float levelDb { -100.0f };
        float holdDb  { -100.0f };
        int   holdFrames { 0 };

        void push (float db);
    };

    void paintBar (juce::Graphics&, juce::Rectangle<float>, const Bar&, juce::Colour) const;

    MeterSource& source;
    std::array<Bar, 2> input, output;
    float reductionDb { 0.0f };
    float reductionHold { 0.0f };
    int   reductionHoldFrames { 0 };
    float inputMaxDb { -100.0f }, outputMaxDb { -100.0f };
};

/** LUFS momentary, short term and integrated, loudness range and true peak.
    A click anywhere on it resets the integrated measurements. */
class Loudness final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit Loudness (MeterSource&);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    /** @param targetLufs  the delivery target, or 0 for none */
    void refresh (float ceilingDb, float targetLufs, const juce::String& targetName);

private:
    MeterSource& source;
    float momentary { -100.0f }, shortTerm { -100.0f }, integrated { -100.0f };
    float range { 0.0f }, truePeakDb { -100.0f }, matchDb { 0.0f }, vu { -100.0f };
    float ceilingDb { -1.0f }, targetLufs { 0.0f };
    juce::String targetName;
};

} // namespace breakwater::display
