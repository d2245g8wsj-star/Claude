#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace ember
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1400;
inline constexpr int designHeight = 340;

class EmberEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit EmberEditor (EmberProcessor&);
    ~EmberEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** One channel's row: input, the three bands, output, IN and the meter. */
    struct Strip
    {
        Strip (EmberProcessor&, int index);

        std::vector<juce::Component*> components();

        panel::Knob input, loLevel, loFreq, midLevel, midFreq, hiLevel, hiFreq, output;
        panel::Lamp loIn, midIn, hiIn, active;
        panel::DriveLamp loDrive, midDrive, hiDrive;
        panel::LedMeter meter;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (EmberProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        void layoutStrip (Strip&, float top);
        void paintStripLegends (juce::Graphics&, int index, float top) const;

        EmberProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;
        panel::ModeSelector mode;

        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    EmberProcessor&     processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberEditor)
};

} // namespace ember
