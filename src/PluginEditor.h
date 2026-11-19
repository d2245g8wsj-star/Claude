#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"
#include "indiewave/ui/Panel.h"

namespace crest
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1640;
inline constexpr int designHeight = 450;

class CrestEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit CrestEditor (CrestProcessor&);
    ~CrestEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using Knob = indiewave::ui::BakeliteKnob;
    using Key  = indiewave::ui::Key;

    /** A row of lit keys that picks one choice of a parameter. */
    class KeyRow final : public juce::Component
    {
    public:
        KeyRow (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, float keyWidth);

        void resized() override;
        void refresh();

        const float keyWidth;

    private:
        juce::RangedAudioParameter& parameter;
        std::vector<std::unique_ptr<Key>> keys;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyRow)
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (CrestProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        /** Meters, figures and lamps; called by the editor's timer. */
        void refresh (const juce::String& status);

    private:
        void paintScreen (juce::Graphics&, juce::Rectangle<float>, const juce::String&, float textHeight) const;

        CrestProcessor& processor;

        Knob inputKnob, characterKnob, kneeKnob, adaptKnob, biasKnob;
        Knob colourKnob, harmonicsKnob, seriesKnob, sidechainKnob, mixKnob;
        Knob ceilingKnob, outputKnob;

        indiewave::ui::LedLadder inputMeter, outputMeter;
        panel::Waveform waveform;
        panel::Scope    scope;

        KeyRow osKeys, wordKeys, ditherKeys;
        Key autoKey, tpSafeKey, deltaKey, linkKey, bypassKey;

        float inputHold { -100.0f }, outputHold { -100.0f };
        int   inputHoldTicks { 0 }, outputHoldTicks { 0 };
        float clipDb { 0.0f };
        juce::String figures[6];    // in, out, clip, momentary, short term, integrated
        juce::String statusText;
        bool powered { true };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    class Tooltips final : public juce::LookAndFeel_V4
    {
    public:
        Tooltips();
        void drawTooltip (juce::Graphics&, const juce::String&, int width, int height) override;
    };

    CrestProcessor&     processor;
    Tooltips            lookAndFeel;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;
    int                 ticks { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrestEditor)
};

} // namespace crest
