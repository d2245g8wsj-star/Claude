#pragma once

#include "PluginProcessor.h"
#include "indiewave/ui/Rendered.h"

namespace lacquer
{

/**
    The rendered panel, in the range's Nazaré style (plugins/lacquer/art/spec.py).
    Four faces: folded (stereo, one row) and open (dual or mid/side, two rows),
    each in Classic White and Lacquer Black. The mode selector and the finish
    switch choose; the editor's height follows the fold.
*/
class LacquerEditor final : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit LacquerEditor (LacquerProcessor&);
    ~LacquerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The size in points of the face for the current mode. */
    juce::Point<int> designSize() const;

private:
    void timerCallback() override;
    void setMode (int index);
    void show (bool open, int finish);

    using Face = indiewave::ui::rendered::Face;

    struct Panel
    {
        Panel (const indiewave::ui::rendered::Art&, LacquerProcessor&, const juce::String& name, int finish);

        Face face;
        indiewave::ui::rendered::Selector mode;
        indiewave::ui::rendered::Lamp pilot;
        indiewave::ui::rendered::Switch finishSwitch;
    };

    Panel& current();

    LacquerProcessor& processor;
    juce::TooltipWindow tooltips { this, 650 };
    std::array<std::unique_ptr<Panel>, 4> panels;   // folded/open x white/black

    bool open { false };
    int finish { -1 }, shownMode { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LacquerEditor)
};

} // namespace lacquer
