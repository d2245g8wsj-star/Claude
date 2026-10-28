#include "Panel.h"

namespace breakwater::panel
{

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;

    juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle)
    {
        return centre.getPointOnCircumference (radius, angle);
    }
}

juce::Font uiFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

void drawLabel (juce::Graphics& g, const juce::String& label, juce::Rectangle<float> area, float height,
                juce::Colour colour, juce::Justification justification, bool bold, float tracking)
{
    auto font = uiFont (height, bold);
    font.setExtraKerningFactor (tracking / juce::jmax (1.0f, height));

    g.setColour (colour);
    g.setFont (font);
    g.drawText (label, area, justification, false);
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    g.setColour (well);
    g.fillRoundedRectangle (area, corner);

    // Inner shadow along the top edge, where the light would not reach.
    juce::ColourGradient shade (juce::Colours::black.withAlpha (0.45f), area.getX(), area.getY(),
                                juce::Colours::transparentBlack, area.getX(), area.getY() + 10.0f, false);
    g.setGradientFill (shade);
    g.fillRoundedRectangle (area.withHeight (juce::jmin (12.0f, area.getHeight())), corner);

    g.setColour (wellEdge);
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
}

// ---------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
            const juce::String& n, Size s, bool isBipolar)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      name (n), size (s), bipolar (isBipolar),
      parameter (*state.getParameter (parameterID)),
      attachment (state, parameterID, *this)
{
    setRotaryParameters (startAngle, endAngle, true);
    setMouseDragSensitivity (s == Size::large ? 260 : 200);
    setVelocityBasedMode (false);
    setDoubleClickReturnValue (true, (double) parameter.convertFrom0to1 (parameter.getDefaultValue()));
    setPopupDisplayEnabled (false, false, nullptr);
    setTooltip (parameter.getName (64));
}

void Knob::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto large  = size == Size::large;

    const auto labelHeight = large ? 15.0f : 13.0f;
    const auto valueHeight = large ? 18.0f : 15.0f;

    auto dial = bounds.withTrimmedTop (labelHeight + 2.0f).withTrimmedBottom (valueHeight + 2.0f);
    const auto side   = juce::jmin (dial.getWidth(), dial.getHeight());
    const auto centre = dial.getCentre();
    const auto radius = side * 0.5f - 4.0f;

    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto angle      = startAngle + proportion * (endAngle - startAngle);
    const auto enabled    = isEnabled();
    const auto tint       = enabled ? accent : textFaint;

    drawLabel (g, name.toUpperCase(), bounds.withHeight (labelHeight), large ? 11.0f : 9.5f,
               enabled ? textDim : textFaint);

    // Track.
    const auto trackWidth = large ? 4.0f : 3.0f;
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
    g.setColour (juce::Colour (0xff2b313a));
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Value arc, from the bottom of the travel or from the centre for a bipolar control.
    const auto from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (tint);
        g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Body.
    const auto bodyRadius = radius - (large ? 9.0f : 7.0f);
    const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillEllipse (body.translated (0.0f, 2.0f).expanded (1.0f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a414b), body.getX(), body.getY(),
                                             juce::Colour (0xff1f2329), body.getX(), body.getBottom(), false));
    g.fillEllipse (body);

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawEllipse (body.reduced (0.5f), 1.0f);

    // Pointer.
    g.setColour (enabled ? text : textFaint);
    g.drawLine ({ onCircle (centre, bodyRadius * 0.35f, angle), onCircle (centre, bodyRadius * 0.92f, angle) },
                large ? 2.6f : 2.0f);

    // Value.
    const auto valueText = parameter.getCurrentValueAsText();
    drawLabel (g, valueText, bounds.withTrimmedTop (bounds.getHeight() - valueHeight), large ? 13.0f : 11.0f,
               enabled ? text : textFaint, juce::Justification::centred, false, 0.2f);
}

// ---------------------------------------------------------------------------

Segmented::Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                      juce::StringArray l)
    : labels (std::move (l)),
      attachment (*state.getParameter (parameterID),
                  [this] (float value)
                  {
                      selected = juce::roundToInt (value);
                      repaint();
                  })
{
    auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterID));
    jassert (choice != nullptr);

    if (labels.isEmpty() && choice != nullptr)
        labels = choice->choices;

    attachment.sendInitialUpdate();
}

void Segmented::setDescriptions (juce::StringArray d)
{
    descriptions = std::move (d);
}

juce::Rectangle<float> Segmented::segment (int index) const
{
    const auto width = (float) getWidth() / (float) labels.size();
    return { width * (float) index, 0.0f, width, (float) getHeight() };
}

void Segmented::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto enabled = isEnabled();

    drawWell (g, bounds, 5.0f);

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto area = segment (i).reduced (2.0f);
        const auto on = i == selected;

        if (on)
        {
            g.setColour ((enabled ? accent : textFaint).withAlpha (enabled ? 0.18f : 0.12f));
            g.fillRoundedRectangle (area, 4.0f);
            g.setColour ((enabled ? accent : textFaint).withAlpha (0.8f));
            g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);
        }

        const auto colour = ! enabled ? textFaint : (on ? text : textDim);
        drawLabel (g, labels[i].toUpperCase(), area, juce::jmin (11.0f, area.getHeight() * 0.42f), colour,
                   juce::Justification::centred, on, 0.8f);

        if (i > 0 && i != selected && i - 1 != selected)
        {
            g.setColour (wellEdge);
            g.fillRect (segment (i).getX(), bounds.getY() + 7.0f, 1.0f, bounds.getHeight() - 14.0f);
        }
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    for (int i = 0; i < labels.size(); ++i)
    {
        if (segment (i).contains (e.position))
        {
            attachment.setValueAsCompleteGesture ((float) i);
            return;
        }
    }
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    if (descriptions.isEmpty())
        return;

    for (int i = 0; i < labels.size(); ++i)
        if (segment (i).contains (e.position))
            setTooltip (descriptions[i]);
}

// ---------------------------------------------------------------------------

Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                const juce::String& legend, juce::Colour lampColour)
    : juce::Button (legend),
      lamp (lampColour),
      attachment (state, parameterID, *this)
{
    setClickingTogglesState (true);
    setTooltip (state.getParameter (parameterID)->getName (64));
}

void Toggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    const auto on = getToggleState();

    drawWell (g, bounds, bounds.getHeight() * 0.5f);

    if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.08f : 0.04f));
        g.fillRoundedRectangle (bounds, bounds.getHeight() * 0.5f);
    }

    const auto lampSize = bounds.getHeight() * 0.32f;
    const auto lampArea = juce::Rectangle<float> (lampSize, lampSize)
                              .withCentre ({ bounds.getX() + bounds.getHeight() * 0.55f, bounds.getCentreY() });

    if (on)
    {
        g.setColour (lamp.withAlpha (0.35f));
        g.fillEllipse (lampArea.expanded (3.0f));
    }

    g.setColour (on ? lamp : juce::Colour (0xff2f353e));
    g.fillEllipse (lampArea);

    drawLabel (g, getButtonText().toUpperCase(),
               bounds.withTrimmedLeft (bounds.getHeight() * 0.9f).withTrimmedRight (6.0f),
               juce::jmin (10.5f, bounds.getHeight() * 0.42f), on ? panel::text : textDim,
               juce::Justification::centredLeft, true, 0.8f);
}

} // namespace breakwater::panel
