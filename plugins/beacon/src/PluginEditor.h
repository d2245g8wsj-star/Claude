#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace beacon
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1280;
inline constexpr int designHeight = 844;

class BeaconEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit BeaconEditor (BeaconProcessor&);
    ~BeaconEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** Moves other parameters along with a knob while a hand is on it, as the
        unit's linking does. Each follower moves by the knob's travel since
        the drag began, times its sign, so stepped targets such as the IRON
        PAD land on whole steps rather than losing small moves to rounding.
        Automation and recalled state set each parameter as it was saved. */
    class Follow
    {
    public:
        Follow (panel::Knob& source, juce::RangedAudioParameter* enable,
                std::vector<std::pair<juce::RangedAudioParameter*, float>> followers,
                std::function<bool()> extraCondition = {});

    private:
        void begin();
        void update();
        void end();

        panel::Knob& source;
        juce::RangedAudioParameter* enable;
        std::vector<std::pair<juce::RangedAudioParameter*, float>> followers;
        std::function<bool()> extraCondition;

        bool dragging { false };
        float sourceStart { 0.0f };
        std::vector<float> followerStart;
    };

    /** One channel: input, compressor, sidechain, saturation, output stage,
        IN and the gain reduction meter. */
    struct Strip
    {
        Strip (BeaconProcessor&, int index);

        std::vector<juce::Component*> components();

        panel::Knob input, threshold, attack, release, hpf, bell, thd, tube, mix, output, ironPad;
        panel::Lamp dynLink, feedForward, limit, hpfIn, bellIn, listen, tubeIn, iron, ironLink, active;
        panel::DriveLamp heat;
        panel::VuMeter meter;

        /** Dynamic link: input and threshold move together and the output
            the other way, so the drive changes and the level does not. Iron
            link: output and pad move together in RED mode. */
        std::vector<std::unique_ptr<Follow>> links;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (BeaconProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

    private:
        /** Each channel is a column: its meter at the top, then the
            compressor, sidechain and saturation, and output stage, one
            section to a row. left is the column's left edge. */
        void layoutStrip (Strip&, float left);
        void paintStripLegends (juce::Graphics&, int index, float left) const;

        /** THD stage drive to heater lamp brightness. */
        static const float heatScale;

        BeaconProcessor& processor;
        std::array<std::unique_ptr<Strip>, numChannelStrips> strips;
        panel::ModeSelector mode;

        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    BeaconProcessor&      processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeaconEditor)
};

} // namespace beacon
