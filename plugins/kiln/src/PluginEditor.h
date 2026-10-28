#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace kiln
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1600;
inline constexpr int designHeight = 340;

class KilnEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit KilnEditor (KilnProcessor&);
    ~KilnEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** One channel's row: input, compressor, sidechain, colour, mix, output,
        IN and the gain reduction meter. */
    struct Strip
    {
        Strip (KilnProcessor&, int index);

        std::vector<juce::Component*> components();

        panel::Knob input, threshold, attack, release, scHpf, scHf, thd, mix, output;
        panel::Lamp link, feedForward, limit, listen, scHfIn, tube, ironPad, active;
        panel::DriveLamp heat;
        panel::VuMeter meter;

    private:
        /** With I/O LINK in, a turn of the input moves the output the other
            way, so the level stays put while the drive changes. Only a hand on
            the knob does this: automation and recalled state set each
            parameter as they were saved. */
        void followInput();

        juce::RangedAudioParameter* outputParameter { nullptr };
        juce::RangedAudioParameter* linkParameter { nullptr };
        float lastInput { 0.0f };
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (KilnProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        void layoutStrip (Strip&, float top);
        void paintStripLegends (juce::Graphics&, int index, float top) const;

        /** THD stage drive to heater lamp brightness. */
        static const float heatScale;

        KilnProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;
        panel::ModeSelector mode;

        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    KilnProcessor&      processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KilnEditor)
};

} // namespace kiln
