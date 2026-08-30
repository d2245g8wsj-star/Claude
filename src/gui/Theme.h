#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace crest::theme
{

// A deliberately narrow palette: two greys for structure, one warm accent for
// everything the clipper is doing to the signal, one cool accent for everything
// that leaves it alone.
inline const juce::Colour background   { 0xff121519 };
inline const juce::Colour panel        { 0xff191e24 };
inline const juce::Colour panelRaised  { 0xff20272e };
inline const juce::Colour outline      { 0xff2b343d };
inline const juce::Colour outlineSoft  { 0xff232a31 };
inline const juce::Colour text         { 0xffd3dae1 };
inline const juce::Colour textDim      { 0xff77848f };
inline const juce::Colour textFaint    { 0xff4d5862 };
inline const juce::Colour amber        { 0xffe9a03c };   ///< drive, clipping, harmonics
inline const juce::Colour teal         { 0xff3fc4b2 };   ///< clean signal, output
inline const juce::Colour red          { 0xffe5484d };   ///< over

juce::Font labelFont (float height);
juce::Font valueFont (float height);
juce::Font displayFont (float height);

/** Small caps style tracking used for the section captions. */
void drawTracked (juce::Graphics& g,
                  const juce::String& textToDraw,
                  juce::Rectangle<float> area,
                  juce::Justification justification,
                  float tracking);

/** Shared look and feel: dark tooltips and text editors that match the rest. */
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    void drawTooltip (juce::Graphics&, const juce::String&, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
};

} // namespace crest::theme
