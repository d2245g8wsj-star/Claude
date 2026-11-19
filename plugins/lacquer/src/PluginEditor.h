#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"
#include "indiewave/ui/Panel.h"

namespace lacquer
{

/** Every coordinate in the editor is in these units; the face is scaled as one.
    Folded (stereo, linked) the unit is one channel over the centre bar;
    unfolded (dual or mid/side) the second channel hangs below the bar. */
inline constexpr int designWidth        = 1400;
inline constexpr int designHeightFolded = 376;
inline constexpr int designHeightOpen   = 648;

class LacquerEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit LacquerEditor (LacquerProcessor&);
    ~LacquerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyFold();

    /** One channel: six bands left to right, frequency above amount, and the
        four switches at the right hand end. */
    struct Strip
    {
        Strip (LacquerProcessor&, int index);

        std::vector<juce::Component*> components();

        panel::SteppedKnob lowBoostFreq, lowBoostAmount, lowCutFreq, lowCutAmount;
        panel::SteppedKnob midBoostFreq, midBoostAmount, midCutFreq, midCutAmount;
        panel::SteppedKnob trebleBoostFreq, trebleQ, trebleBoostAmount, trebleCutFreq, trebleCutAmount;
        panel::Toggle shift, buffer, transformer, eqIn;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (LacquerProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

        bool isOpen() const noexcept { return open; }
        std::function<void()> onFoldChanged;

    private:
        void layoutStrip (Strip&, float top);
        void applyFinish (panel::Finish);
        void setMode (int index);
        void paintStripMarkings (juce::Graphics&, int index, float top) const;

        LacquerProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;

        std::array<indiewave::ui::Key, 3> modeKeys;
        indiewave::ui::FoldTab fold;
        panel::SteppedKnob output;
        panel::PilotLamp   pilot;
        panel::FinishSelector finishSelector;

        panel::Finish         finish  { panel::Finish::white };
        const panel::Palette* palette { &panel::Palette::forFinish (panel::Finish::white) };

        bool open { false };
        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    LacquerProcessor&   processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LacquerEditor)
};

} // namespace lacquer
