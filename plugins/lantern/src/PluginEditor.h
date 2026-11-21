#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Panel.h"

namespace lantern
{

/** Every coordinate in the editor is in these units; the face is scaled as one.
    Folded, the unit is one channel panel and the centre; unfolded (dual mono
    split, or mid/side) it carries a second channel panel to the right. */
inline constexpr int designHeight      = 520;
inline constexpr int designWidthFolded = 1140;
inline constexpr int designWidthOpen   = 1908;

class LanternEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit LanternEditor (LanternProcessor&);
    ~LanternEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    /** Moves the IRON PAD with the RED output knob while a hand is on it, in
        RED with the iron link in. */
    class Follow
    {
    public:
        Follow (juce::Slider& source, juce::RangedAudioParameter* enable,
                juce::RangedAudioParameter* follower, std::function<bool()> extraCondition);

    private:
        juce::Slider& source;
        juce::RangedAudioParameter* enable;
        juce::RangedAudioParameter* follower;
        std::function<bool()> extraCondition;

        bool dragging { false };
        float sourceStart { 0.0f }, followerStart { 0.0f };
    };

    /** The response of the equaliser, drawn from the same settings the audio
        runs on: the first channel, and the second when it is folded out. */
    class Curve final : public juce::Component
    {
    public:
        void setCurves (std::vector<float> first, std::vector<float> second, float spanDb);
        void paint (juce::Graphics&) override;

        static constexpr int points = 120;
        static float frequencyAt (int point) noexcept;

    private:
        std::vector<float> first, second;
        float span { 16.0f };
    };

    using Knob = indiewave::ui::RingKnob;
    using Key  = indiewave::ui::Key;

    struct BandControls
    {
        BandControls (LanternProcessor&, int band, bool second);

        std::unique_ptr<Knob> gain, freq, q;
        std::unique_ptr<Key> in, shelf, listen;
    };

    struct ChannelControls
    {
        ChannelControls (LanternProcessor&, bool second);

        std::vector<juce::Component*> components();
        void layout (float left);
        void showStage (bool iron);

        /** The LISTEN keys reflect one choice parameter: which band, if any,
            is in focus. */
        void refreshListen();

        juce::RangedAudioParameter* listenParameter { nullptr };
        std::vector<std::unique_ptr<BandControls>> bands;
        Knob hpf, lpf, thd, output, outputIron;
        Key hpfIn, hpfSteep, lpfIn, lpfSteep, wide, proportional, mute;
        indiewave::ui::LedLadder outLadder;
    };

    class Face final : public juce::Component
    {
    public:
        explicit Face (LanternProcessor&);

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
        std::vector<float> curveOf (bool second) const;

        LanternProcessor& processor;
        ChannelControls main, second;

        Knob ironPad;
        Key ironLink, iron, bypass;
        indiewave::ui::ChoiceKeys mode;
        Curve curve;
        indiewave::ui::FoldTab fold;

        std::unique_ptr<Follow> ironFollow;

        float heat[2] {};
        bool open { false };
        bool shownIron { false };
        int shownMode { -1 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Face)
    };

    void applyFold();

    LanternProcessor&   processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face                face;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanternEditor)
};

} // namespace lantern
