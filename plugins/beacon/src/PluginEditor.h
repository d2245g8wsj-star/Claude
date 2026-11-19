#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Panel.h"

namespace beacon
{

/** Every coordinate in the editor is in these units; the face is scaled as one.
    Folded, the unit is one channel panel and the centre; unfolded (dual mono
    split, or mid/side) it carries a second channel panel to the right. */
inline constexpr int designHeight      = 450;
inline constexpr int designWidthFolded = 1000;
inline constexpr int designWidthOpen   = 1640;

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
        Follow (juce::Slider& source, juce::RangedAudioParameter* enable,
                std::vector<std::pair<juce::RangedAudioParameter*, float>> followers,
                std::function<bool()> extraCondition = {});

    private:
        void begin();
        void update();
        void end();

        juce::Slider& source;
        juce::RangedAudioParameter* enable;
        std::vector<std::pair<juce::RangedAudioParameter*, float>> followers;
        std::function<bool()> extraCondition;

        bool dragging { false };
        float sourceStart { 0.0f };
        std::vector<float> followerStart;
    };

    using Knob = indiewave::ui::RingKnob;
    using Key  = indiewave::ui::Key;

    /** One channel panel: the controls of one signal path. The first panel
        holds the main set (left, mid, or both in stereo); the second the
        second set (right when dual mono is split, side in mid/side). */
    struct ChannelControls
    {
        ChannelControls (BeaconProcessor&, bool second);

        std::vector<juce::Component*> components();
        void layout (float left);

        Knob threshold, mix, attack, input, release, thd, tube, output, hpf, bell;
        Key limit, feedForward, dynLink, tubeIn, hpfIn, bellIn, listen;
        indiewave::ui::VuMeter meter;
        indiewave::ui::LedLadder inLadder, outLadder;

        std::vector<std::unique_ptr<Follow>> links;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (BeaconProcessor&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void refresh();

        bool isOpen() const noexcept { return open; }
        std::function<void()> onFoldChanged;

    private:
        ChannelMode currentMode() const;
        bool wantsOpen() const;
        void foldTabClicked();
        void paintChannel (juce::Graphics&, float left, const juce::String& title, bool second);

        BeaconProcessor& processor;
        ChannelControls main, second;

        Knob ironPad;
        Key ironLink, iron, bypass;
        indiewave::ui::ChoiceKeys mode;
        indiewave::ui::Display display;
        indiewave::ui::FoldTab fold;

        float heat[2] {};
        bool open { false };
        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    void applyFold();

    BeaconProcessor&    processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BeaconEditor)
};

} // namespace beacon
