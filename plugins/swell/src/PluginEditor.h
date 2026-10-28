#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace swell
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1480;
inline constexpr int designHeight = 340;

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

    /** One channel's row: input, the three bands, output, IN and the meter. */
    struct Strip
    {
        Strip (SwellProcessor&, int index);

        std::vector<juce::Component*> controls();

        panel::Knob input, loLevel, loFreq, midLevel, midFreq, hiLevel, hiFreq, output;
        panel::Toggle loIn, midIn, hiIn, active;
        panel::JewelLamp loDrive, midDrive, hiDrive;
        panel::VuMeter meter;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (SwellProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        void layoutStrip (Strip&, float top);
        void paintStrip (juce::Graphics&, int index, float top) const;

        SwellProcessor& processor;
        juce::Image enamel;

        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;
        panel::Selector mode;

        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    SwellProcessor&     processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellEditor)
};

} // namespace swell
