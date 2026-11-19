#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Rendered.h"

namespace lantern
{

/**
    The rendered panel, in the range's Nazaré style (plugins/lantern/art/spec.py).
    Folded, one half and the centre for stereo; open, the side (or right) half
    beside it, on the side_* parameters. The response is drawn live behind the
    centre glass.
*/
class LanternEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit LanternEditor (LanternProcessor&);
    ~LanternEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The size in points of the face for the current state. */
    juce::Point<int> designSize() const;

private:
    void timerCallback() override;
    void splitClicked();
    bool wantsOpen() const;
    void showFace (bool open);
    std::vector<float> curveOf (bool secondSet) const;

    /** The response of the equaliser, from the same settings the audio runs
        on: the first channel, and the second when it is folded out. */
    class Curve final : public juce::Component
    {
    public:
        Curve() { setInterceptsMouseClicks (false, false); }
        void setCurves (std::vector<float> first, std::vector<float> second, float spanDb);
        void paint (juce::Graphics&) override;

        static constexpr int points = 120;
        static float frequencyAt (int point) noexcept;

    private:
        std::vector<float> first, second;
        float span { 16.0f };
    };

    using Face = indiewave::ui::rendered::Face;

    struct Extras
    {
        explicit Extras (Face&);

        indiewave::ui::rendered::Switch split;
        Curve curve;
    };

    LanternProcessor& processor;
    juce::TooltipWindow tooltips { this, 650 };
    Face folded, opened;
    Extras foldedExtras, openExtras;
    std::unique_ptr<indiewave::ui::rendered::Follow> folllowFolded, followOpen;

    bool open { false }, shownWant { false };
    int shownMode { -1 }, shownIron { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanternEditor)
};

} // namespace lantern
