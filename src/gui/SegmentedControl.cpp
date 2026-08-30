#include "SegmentedControl.h"

namespace crest::gui
{

namespace
{
    constexpr float captionWidth = 46.0f;
    constexpr float corner       = 3.0f;
}

SegmentedControl::SegmentedControl (juce::AudioProcessorValueTreeState& state,
                                    const juce::String& parameterID,
                                    const juce::String& caption,
                                    juce::Colour accentColour)
    : parameter (*state.getParameter (parameterID)),
      captionText (caption),
      accent (accentColour),
      attachment (parameter,
                  [this] (float newValue)
                  {
                      // ParameterAttachment hands over the denormalised value,
                      // which for a choice parameter is the index itself.
                      selectedIndex = juce::roundToInt (newValue);
                      repaint();
                  },
                  nullptr)
{
    if (auto* choiceParameter = dynamic_cast<juce::AudioParameterChoice*> (&parameter))
        choices = choiceParameter->choices;

    setTooltip (parameter.getName (64));
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> SegmentedControl::stripArea() const
{
    auto area = getLocalBounds().toFloat();

    if (captionText.isNotEmpty())
        area.removeFromLeft (captionWidth);

    return area;
}

juce::Rectangle<float> SegmentedControl::segmentBounds (int index) const
{
    const auto strip = stripArea();
    const auto count = juce::jmax (1, choices.size());
    const auto width = strip.getWidth() / (float) count;

    return { strip.getX() + width * (float) index, strip.getY(), width, strip.getHeight() };
}

int SegmentedControl::segmentAt (juce::Point<float> position) const
{
    const auto strip = stripArea();

    if (! strip.contains (position) || choices.isEmpty())
        return -1;

    const auto width = strip.getWidth() / (float) choices.size();

    return juce::jlimit (0, choices.size() - 1,
                         (int) ((position.x - strip.getX()) / width));
}

void SegmentedControl::mouseDown (const juce::MouseEvent& event)
{
    const auto index = segmentAt (event.position);

    if (index < 0 || index == selectedIndex)
        return;

    attachment.setValueAsCompleteGesture ((float) index);
}

void SegmentedControl::mouseMove (const juce::MouseEvent& event)
{
    const auto index = segmentAt (event.position);

    if (index != hoverIndex)
    {
        hoverIndex = index;
        repaint();
    }
}

void SegmentedControl::mouseExit (const juce::MouseEvent&)
{
    hoverIndex = -1;
    repaint();
}

void SegmentedControl::paint (juce::Graphics& g)
{
    if (captionText.isNotEmpty())
    {
        auto captionArea = getLocalBounds().toFloat().removeFromLeft (captionWidth);

        g.setColour (theme::textDim);
        g.setFont (theme::labelFont (10.0f));
        theme::drawTracked (g, captionText, captionArea, juce::Justification::left, 1.4f);
    }

    const auto strip = stripArea();

    g.setColour (theme::panel);
    g.fillRoundedRectangle (strip, corner);

    for (int i = 0; i < choices.size(); ++i)
    {
        const auto segment  = segmentBounds (i);
        const auto selected = i == selectedIndex;

        if (selected)
        {
            g.setColour (accent.withAlpha (0.16f));
            g.fillRoundedRectangle (segment.reduced (1.5f), corner - 1.0f);

            g.setColour (accent);
            g.fillRoundedRectangle (segment.reduced (1.5f).removeFromBottom (2.0f), 1.0f);
        }
        else if (i == hoverIndex)
        {
            g.setColour (theme::panelRaised);
            g.fillRoundedRectangle (segment.reduced (1.5f), corner - 1.0f);
        }

        g.setColour (selected ? accent : theme::textDim);
        g.setFont (theme::labelFont (10.0f));
        g.drawText (choices[i], segment, juce::Justification::centred, false);

        if (i > 0 && ! selected && i - 1 != selectedIndex)
        {
            g.setColour (theme::outlineSoft);
            g.drawVerticalLine (juce::roundToInt (segment.getX()),
                                segment.getY() + 5.0f, segment.getBottom() - 5.0f);
        }
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (strip.reduced (0.5f), corner, 1.0f);
}

} // namespace crest::gui
