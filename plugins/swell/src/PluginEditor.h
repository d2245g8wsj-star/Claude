#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace swell
{

/** Every coordinate in the editor is in these units; the face is scaled as one.
    The same design size as Crest, so the two open at the same size. */
inline constexpr int designWidth  = 1400;
inline constexpr int designHeight = 470;

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

    /** One channel's row: input, the three bands, output and IN. */
    struct Strip
    {
        Strip (SwellProcessor&, int index, const panel::Filmstrip& large,
               const panel::Filmstrip& small, const panel::LedImages&);

        std::vector<juce::Component*> controls();

        panel::Knob input, loLevel, loFreq, midLevel, midFreq, hiLevel, hiFreq, output;
        panel::Switch loIn, midIn, hiIn, active;
        panel::DriveLamp loDrive, midDrive, hiDrive;
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

        juce::Image plate;
        panel::Filmstrip largeStrip, smallStrip;
        panel::LedImages leds;

        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;
        std::array<std::unique_ptr<panel::LedMeter>, 2> meters;
        std::array<panel::Readout, 2> peaks;

        panel::Bank   mode;
        panel::Switch bypass;

        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    SwellProcessor&     processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SwellEditor)
};

} // namespace swell
