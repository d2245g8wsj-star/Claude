#pragma once

#include "PluginProcessor.h"
#include "gui/Modern.h"
#include "indiewave/ui/Panel.h"

namespace swell
{

/** Every coordinate in the editor is in these units; the face is scaled as one.
    Folded (stereo) the unit is one row; unfolded (dual or mid/side) it carries
    the second strip underneath. */
inline constexpr int designWidth        = 1640;
inline constexpr int designHeightFolded = 214;
inline constexpr int designHeightOpen   = 384;

class SwellEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit SwellEditor (SwellProcessor&);
    ~SwellEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void applyFold();

    /** One strip's row: input, the three bands, output, strip IN and the meter. */
    struct Strip
    {
        Strip (SwellProcessor&, int index);

        std::vector<juce::Component*> components();
        void layout (float rowCentre);

        modern::ArcKnob input, loLevel, loFreq, midLevel, midFreq, hiLevel, hiFreq, output;
        modern::Pill loIn, midIn, hiIn, active;
        modern::DriveLed loDrive, midDrive, hiDrive;
        modern::BarMeter meter;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (SwellProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();

        bool isOpen() const noexcept { return open; }
        std::function<void()> onFoldChanged;

    private:
        void setMode (StripMode);
        void paintRow (juce::Graphics&, int index, float rowCentre) const;

        SwellProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;
        modern::Segments modeSwitch { { "STEREO", "DUAL", "M/S" } };
        indiewave::ui::FoldTab fold;

        bool open { false };
        int shownMode { -1 };
        StripMode lastOpenMode { StripMode::dualMono };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    SwellProcessor&     processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellEditor)
};

} // namespace swell
