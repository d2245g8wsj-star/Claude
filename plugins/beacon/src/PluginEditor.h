#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"

namespace beacon
{

/** Every coordinate in the editor is in these units; the face is scaled as one. */
inline constexpr int designWidth  = 1180;
inline constexpr int designHeight = 510;

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

    /** The controls of one channel's signal path. There are two sets: the
        main one, which is the mid in mid/side mode, and the side's, shown in
        its place when the MID/SIDE switch is on SIDE. */
    struct ChannelControls
    {
        ChannelControls (BeaconProcessor&, bool side);

        std::vector<juce::Component*> components();

        panel::Knob input, threshold, attack, release, hpf, bell, thd, tube, mix, output;
        panel::Lamp dynLink, feedForward, limit, hpfIn, bellIn, listen, tubeIn;

        /** Dynamic link: input and threshold move together and the output
            the other way, so the drive changes and the level does not. On the
            main set, iron link: output and pad move together in RED mode. */
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
        void layoutChannel (ChannelControls&);
        std::vector<juce::Component*> sharedComponents();

        /** Shows the side's controls in place of the main ones, in mid/side
            mode with the switch on SIDE. */
        bool editingSide() const;

        /** THD stage drive to heater lamp brightness. */
        static const float heatScale;

        BeaconProcessor& processor;
        ChannelControls main, side;

        // Shared by both channels: the output transformer works on left and
        // right in every mode.
        panel::Knob ironPad;
        panel::Lamp iron, ironLink, bypass;
        panel::DriveLamp heat;
        panel::VuMeter meter;
        panel::ViewSwitch view;
        panel::ModeSelector mode;

        int shownMode { -1 };
        bool shownSide { true };  // so the first refresh sets the visibility up
        bool shownRed { true };   // and the PAD

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    BeaconProcessor&      processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeaconEditor)
};

} // namespace beacon
