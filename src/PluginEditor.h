#pragma once

#include "PluginProcessor.h"
#include "gui/Panel.h"
#include "indiewave/ui/Rendered.h"

namespace crest
{

/** The rendered panel, in the range's Nazaré style (art/spec.py). The display
    window and the four figures under it are drawn live into the render. */
class CrestEditor final : public juce::AudioProcessorEditor,
                          private juce::Timer
{
public:
    explicit CrestEditor (CrestProcessor&);
    ~CrestEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::Point<int> designSize();

private:
    void timerCallback() override;

    /** The waveform with the transfer curve in its lower right corner. */
    class Display final : public juce::Component
    {
    public:
        explicit Display (CrestProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        panel::Waveform waveform;
        panel::Scope    scope;
        juce::Rectangle<float> inset;
    };

    /** One lit figure in its window. */
    class Figure final : public juce::Component
    {
    public:
        Figure() { setInterceptsMouseClicks (false, false); }
        void setText (const juce::String&);
        void paint (juce::Graphics&) override;

    private:
        juce::String text { "--" };
    };

    CrestProcessor& processor;
    juce::TooltipWindow tooltips { this, 650 };
    indiewave::ui::rendered::Face face;
    Display display;
    std::array<Figure, 4> figures;
    indiewave::ui::rendered::VuNeedles meter;
    indiewave::ui::rendered::Lamp pilot;

    float clipDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CrestEditor)
};

} // namespace crest
