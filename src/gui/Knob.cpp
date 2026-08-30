#include "Knob.h"

namespace crest::gui
{

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;
}

Knob::Knob (juce::AudioProcessorValueTreeState& state,
            const juce::String& parameterID,
            const juce::String& caption,
            juce::Colour accentColour,
            bool bipolar)
    : captionText (caption),
      accent (accentColour),
      isBipolar (bipolar),
      attachment (state, parameterID, *this)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (startAngle, endAngle, true);
    setVelocityBasedMode (false);
    setMouseDragSensitivity (180);
    setWantsKeyboardFocus (false);

    if (auto* parameter = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, state.getParameterRange (parameterID)
                                             .convertFrom0to1 (parameter->getDefaultValue()));
        setTooltip (parameter->getName (64));
    }
}

void Knob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    const auto captionHeight = 13.0f;
    const auto valueHeight   = 15.0f;

    auto captionArea = bounds.removeFromTop (captionHeight);
    auto valueArea   = bounds.removeFromBottom (valueHeight);

    // ------------------------------------------------------------- caption
    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    theme::drawTracked (g, captionText.toUpperCase(), captionArea,
                        juce::Justification::horizontallyCentred, 1.4f);

    // ---------------------------------------------------------------- dial
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 2.0f;
    const auto dial     = juce::Rectangle<float> (diameter, diameter).withCentre (bounds.getCentre());
    const auto centre   = dial.getCentre();
    const auto radius   = diameter * 0.5f;

    const auto trackRadius = radius - (large ? 5.0f : 4.0f);
    const auto trackWidth  = large ? 5.0f : 4.0f;

    const auto proportion  = static_cast<float> (valueToProportionOfLength (getValue()));
    const auto valueAngle  = startAngle + proportion * (endAngle - startAngle);
    const auto anchorAngle = isBipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    // Body: a shallow radial gradient reads as a machined dial without
    // pretending to be a photograph of one.
    juce::ColourGradient body (theme::panelRaised.brighter (0.10f),
                               centre.translated (0.0f, -radius * 0.5f),
                               theme::panel.darker (0.35f),
                               centre.translated (0.0f, radius),
                               true);
    g.setGradientFill (body);
    g.fillEllipse (dial.reduced (large ? 9.0f : 7.5f));

    g.setColour (theme::outline);
    g.drawEllipse (dial.reduced (large ? 9.0f : 7.5f), 1.0f);

    // Track
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                         startAngle, endAngle, true);

    g.setColour (theme::outlineSoft);
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc
    if (std::abs (valueAngle - anchorAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, trackRadius, trackRadius, 0.0f,
                             juce::jmin (anchorAngle, valueAngle),
                             juce::jmax (anchorAngle, valueAngle), true);

        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Pointer
    juce::Path pointer;
    const auto pointerLength = radius * (large ? 0.46f : 0.44f);
    const auto pointerWidth  = large ? 2.6f : 2.2f;

    pointer.addRoundedRectangle (-pointerWidth * 0.5f, -radius * 0.72f,
                                 pointerWidth, pointerLength, pointerWidth * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (valueAngle)
                                .translated (centre.x, centre.y));

    g.setColour (theme::text);
    g.fillPath (pointer);

    // Centre cap
    g.setColour (theme::panel.darker (0.2f));
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (centre));

    // ---------------------------------------------------------------- value
    g.setColour (isMouseOverOrDragging() ? accent : theme::text);
    g.setFont (theme::valueFont (12.0f));
    g.drawText (getTextFromValue (getValue()), valueArea, juce::Justification::centred, false);
}

} // namespace crest::gui
