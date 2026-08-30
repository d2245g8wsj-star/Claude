#include "ToggleChip.h"

namespace crest::gui
{

ToggleChip::ToggleChip (juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& parameterID,
                        const juce::String& caption,
                        juce::Colour accentColour)
    : parameter (*apvts.getParameter (parameterID)),
      captionText (caption),
      accent (accentColour),
      attachment (parameter,
                  [this] (float newValue)
                  {
                      state = newValue >= 0.5f;
                      repaint();
                  },
                  nullptr)
{
    setTooltip (parameter.getName (64));
    attachment.sendInitialUpdate();
}

void ToggleChip::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (state ? 0.0f : 1.0f);
}

void ToggleChip::mouseEnter (const juce::MouseEvent&) { hover = true;  repaint(); }
void ToggleChip::mouseExit  (const juce::MouseEvent&) { hover = false; repaint(); }

void ToggleChip::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (state ? accent.withAlpha (0.14f)
                       : (hover ? theme::panelRaised : theme::panel));
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (state ? accent.withAlpha (0.65f) : theme::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    auto indicator = bounds.removeFromLeft (18.0f);
    const auto led = juce::Rectangle<float> (6.0f, 6.0f).withCentre (indicator.getCentre());

    if (state)
    {
        g.setColour (accent.withAlpha (0.25f));
        g.fillEllipse (led.expanded (3.0f));
    }

    g.setColour (state ? accent : theme::outline.brighter (0.15f));
    g.fillEllipse (led);

    g.setColour (state ? theme::text : theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    theme::drawTracked (g, captionText, bounds.withTrimmedRight (6.0f),
                        juce::Justification::left, 1.2f);
}

} // namespace crest::gui
