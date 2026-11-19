#pragma once

#include "PluginProcessor.h"
#include "gui/Display.h"
#include "indiewave/ui/Rendered.h"

namespace breakwater
{

/**
    The rendered panel, in the range's Nazaré style (plugins/breakwater/art/spec.py).
    The simple face carries the scope, the meters and the loudness behind glass
    and the main row; the advanced face adds the limiter, tone, converter and
    compressor rows underneath. The editor's size is remembered.
*/
class BreakwaterEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit BreakwaterEditor (BreakwaterProcessor&);
    ~BreakwaterEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** The size in points of the face on show. */
    juce::Point<int> designSize() const;

private:
    void timerCallback() override;
    void setAdvanced (bool);
    juce::String caption() const;
    juce::String footer() const;

    using Face = indiewave::ui::rendered::Face;

    /** One face and the live parts drawn into its windows. */
    struct Panel
    {
        Panel (BreakwaterProcessor&, const juce::String& name, bool advanced);

        Face face;
        indiewave::ui::rendered::Switch advanced;
        display::Scope    scope;
        display::Meters   meters;
        display::Loudness loudness;
        std::unique_ptr<juce::Label> compReduction;
    };

    BreakwaterProcessor& processor;
    juce::TooltipWindow  tooltips { this, 650 };
    Panel simple, full;
    bool advancedShown { false };
    float compressorReductionDb { 0.0f };
    int shownEngine { -1 };
    int shownCompressor { -1 };

    std::unique_ptr<juce::PropertiesFile> userSettings;
    bool sizeRestored { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BreakwaterEditor)
};

} // namespace breakwater
