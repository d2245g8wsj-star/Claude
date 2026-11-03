#include "Panel.h"

namespace tide::panel
{

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;

    float radians (float degrees) { return juce::degreesToRadians (degrees); }

    juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle)
    {
        return centre.getPointOnCircumference (radius, angle);
    }
}

juce::Font plateFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                juce::Justification justification, juce::Colour colour, bool bold, float tracking)
{
    auto font = plateFont (height, bold);

    if (tracking > 0.0f)
        font.setExtraKerningFactor (tracking / height);

    g.setFont (font);
    g.setColour (colour);
    g.drawText (text, area, justification, false);
}

void drawRackEar (juce::Graphics& g, juce::Rectangle<float> area, bool leftSide)
{
    // The ear is the same plate, folded: a faint highlight where the bend is.
    const auto bendX = leftSide ? area.getRight() - 1.0f : area.getX();
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (juce::Rectangle<float> (bendX, area.getY(), 1.0f, area.getHeight()));

    for (auto top : { true, false })
    {
        const auto slot = juce::Rectangle<float> (area.getWidth() * 0.62f, area.getHeight() * 0.085f)
                              .withCentre ({ area.getCentreX(),
                                             top ? area.getY() + area.getHeight() * 0.115f
                                                 : area.getBottom() - area.getHeight() * 0.115f });
        const auto corner = slot.getHeight() * 0.40f;

        // Through the slot: the rack rail behind, in shadow.
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff050505), slot.getX(), slot.getY(),
                                                 juce::Colour (0xff1c1c1c), slot.getX(), slot.getBottom(), false));
        g.fillRoundedRectangle (slot, corner);

        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.drawRoundedRectangle (slot.translated (0.0f, 0.8f), corner, 0.8f);
    }
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float diameter)
{
    const auto head = juce::Rectangle<float> (diameter, diameter).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillEllipse (head.translated (0.0f, 1.0f).expanded (0.8f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4a4b), head.getX(), head.getY(),
                                             juce::Colour (0xff0d0d0d), head.getRight(), head.getBottom(), false));
    g.fillEllipse (head);

    // A hex socket.
    juce::Path socket;
    socket.addPolygon (centre, 6, diameter * 0.22f, 0.0f);
    g.setColour (juce::Colours::black);
    g.fillPath (socket);
}

void drawShelfGlyph (juce::Graphics& g, juce::Rectangle<float> r, bool lowShelf, juce::Colour colour)
{
    juce::Path p;
    const auto hi = r.getY() + r.getHeight() * 0.2f, lo = r.getBottom() - r.getHeight() * 0.2f;

    if (lowShelf)
    {
        p.startNewSubPath (r.getX(), hi);
        p.lineTo (r.getX() + r.getWidth() * 0.30f, hi);
        p.cubicTo (r.getCentreX(), hi, r.getCentreX(), lo, r.getX() + r.getWidth() * 0.70f, lo);
        p.lineTo (r.getRight(), lo);
    }
    else
    {
        p.startNewSubPath (r.getX(), lo);
        p.lineTo (r.getX() + r.getWidth() * 0.30f, lo);
        p.cubicTo (r.getCentreX(), lo, r.getCentreX(), hi, r.getX() + r.getWidth() * 0.70f, hi);
        p.lineTo (r.getRight(), hi);
    }

    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void drawPeakGlyph (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour colour)
{
    juce::Path p;
    const auto hi = r.getY() + r.getHeight() * 0.1f, lo = r.getBottom() - r.getHeight() * 0.2f;

    p.startNewSubPath (r.getX(), lo);
    p.cubicTo (r.getX() + r.getWidth() * 0.35f, lo, r.getX() + r.getWidth() * 0.38f, hi, r.getCentreX(), hi);
    p.cubicTo (r.getRight() - r.getWidth() * 0.38f, hi, r.getRight() - r.getWidth() * 0.35f, lo, r.getRight(), lo);

    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ---------------------------------------------------------------------------

BakeliteKnob::BakeliteKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                            float knobRadius, float startDegrees, float endDegrees,
                            std::vector<Legend> scaleLegends, float legendDistance, float textHeight)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      radius (knobRadius),
      legendRadius (legendDistance),
      legendHeight (textHeight),
      legends (std::move (scaleLegends)),
      attachment (state, parameterID, *this)
{
    setRotaryParameters (radians (startDegrees) + twoPi, radians (endDegrees) + twoPi, true);
    setPopupDisplayEnabled (true, true, nullptr, 1200);
    setMouseDragSensitivity (160);
    setVelocityBasedMode (false);
    setScrollWheelEnabled (true);

    if (auto* parameter = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        setTooltip (parameter->getName (64));
    }
}

void BakeliteKnob::paint (juce::Graphics& g)
{
    const auto centre = getLocalBounds().toFloat().getCentre();
    const auto params = getRotaryParameters();
    const auto start  = params.startAngleRadians;
    const auto end    = params.endAngleRadians;

    auto angleOf = [this, start, end] (double value)
    {
        return start + (float) valueToProportionOfLength (value) * (end - start);
    };

    const auto angle = angleOf (getValue());

    // The scale: a dot at every switch position, the legend beyond it.
    const auto steps = juce::roundToInt ((getMaximum() - getMinimum()) / juce::jmax (getInterval(), 1.0e-9)) + 1;
    const auto dotRadius = radius + 7.0f;

    for (int i = 0; i < steps; ++i)
    {
        const auto a = angleOf (getMinimum() + i * getInterval());
        g.setColour (ink);
        g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre (onCircle (centre, dotRadius, a)));
    }

    for (const auto& legend : legends)
    {
        const auto a  = angleOf (legend.value);
        const auto at = onCircle (centre, legendRadius, a);
        drawLabel (g, legend.text, juce::Rectangle<float> (40.0f, legendHeight + 3.0f).withCentre (at), legendHeight);
    }

    if (unit.isNotEmpty())
    {
        const auto step = (end - start) / (float) juce::jmax (1, steps - 1);
        const auto at   = onCircle (centre, legendRadius, end + step * 0.75f);
        drawLabel (g, unit, juce::Rectangle<float> (30.0f, legendHeight + 3.0f).withCentre (at), legendHeight);
    }

    // Shadow cast on the plate, long and soft towards the bottom.
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f + (float) i * 2.5f, radius * 2.0f + (float) i * 2.5f)
                           .withCentre (centre.translated (0.0f, (float) i * 1.2f)));
    }

    // The skirt: black phenolic, with a fluted grip that turns with the knob.
    const auto skirt = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3b), skirt.getX(), skirt.getY(),
                                             juce::Colour (0xff050505), skirt.getRight(), skirt.getBottom(), false));
    g.fillEllipse (skirt);

    const int flutes = juce::jmax (24, juce::roundToInt (radius * 1.5f));

    for (int i = 0; i < flutes; ++i)
    {
        const auto a = angle + (float) i * twoPi / (float) flutes;

        // Lit from the upper left: ridges facing it catch the light.
        const auto facing = 0.5f + 0.5f * std::cos (a - radians (-45.0f));
        g.setColour (juce::Colours::white.withAlpha (0.03f + 0.20f * facing * facing));
        g.drawLine ({ onCircle (centre, radius * 0.80f, a), onCircle (centre, radius * 0.985f, a) }, 1.1f);

        g.setColour (juce::Colours::black.withAlpha (0.55f));
        const auto b = a + twoPi / (float) flutes * 0.5f;
        g.drawLine ({ onCircle (centre, radius * 0.80f, b), onCircle (centre, radius * 0.985f, b) }, 1.0f);
    }

    g.setColour (juce::Colours::black);
    g.drawEllipse (skirt, 1.0f);

    // A bevel down onto the cap.
    const auto capRadius = radius * 0.78f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff000000), cap.getX(), cap.getY(),
                                             juce::Colour (0xff3c3c3d), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap.expanded (1.6f));

    // The polished dome: deep black, a broad sheen and a sharp highlight.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2e2e30), cap.getX() + capRadius * 0.55f, cap.getY() + capRadius * 0.45f,
                                             juce::Colour (0xff050505), cap.getRight(), cap.getBottom(), true));
    g.fillEllipse (cap);

    {
        const auto sheen = juce::Rectangle<float> (capRadius * 1.25f, capRadius * 0.75f)
                               .withCentre (centre.translated (-capRadius * 0.22f, -capRadius * 0.42f));
        juce::ColourGradient glow (juce::Colours::white.withAlpha (0.22f), sheen.getCentreX(), sheen.getCentreY(),
                                   juce::Colours::white.withAlpha (0.0f), sheen.getRight(), sheen.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillEllipse (sheen);

        const auto spot = juce::Rectangle<float> (capRadius * 0.32f, capRadius * 0.18f)
                              .withCentre (centre.translated (-capRadius * 0.38f, -capRadius * 0.52f));
        juce::ColourGradient highlight (juce::Colours::white.withAlpha (0.75f), spot.getCentreX(), spot.getCentreY(),
                                        juce::Colours::white.withAlpha (0.0f), spot.getRight(), spot.getCentreY(), true);
        g.setGradientFill (highlight);
        g.fillEllipse (spot);
    }

    // The engraved index line, filled white, from the middle of the dome
    // across the skirt.
    const auto from = onCircle (centre, capRadius * 0.18f, angle);
    const auto to   = onCircle (centre, radius * 0.97f, angle);
    const auto width = juce::jmax (2.0f, radius * 0.085f);

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.drawLine ({ from.translated (0.0f, 0.8f), to.translated (0.0f, 0.8f) }, width + 1.2f);
    g.setColour (juce::Colour (0xfff7f5ef));
    g.drawLine ({ from, to }, width);

    if (! isEnabled())
    {
        g.setColour (plateBottom.withAlpha (0.55f));
        g.fillEllipse (skirt.expanded (2.0f));
    }
}

bool BakeliteKnob::hitTest (int x, int y)
{
    // Only the knob and its scale, not the corners of the square it sits in,
    // which overlap the switches beside it.
    return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) < radius + 12.0f;
}

// ---------------------------------------------------------------------------

Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID)
    : juce::Button (parameterID),
      attachment (state, parameterID, *this)
{
    setClickingTogglesState (true);

    if (auto* parameter = state.getParameter (parameterID))
        setTooltip (parameter->getName (64));
}

namespace
{
    /** Chrome hex nut, bushing and a bat lever leaning towards direction. */
    void paintBat (juce::Graphics& g, juce::Point<float> centre, float nut, juce::Point<float> direction)
    {
        juce::Path hex;
        hex.addPolygon (centre, 6, nut * 0.5f, juce::MathConstants<float>::pi / 6.0f);

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillPath (hex, juce::AffineTransform::translation (0.8f, 1.6f));

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe9e9e7), centre.x - nut * 0.4f, centre.y - nut * 0.4f,
                                                 juce::Colour (0xff616365), centre.x + nut * 0.4f, centre.y + nut * 0.5f, false));
        g.fillPath (hex);
        g.setColour (juce::Colour (0xff3b3c3d));
        g.strokePath (hex, juce::PathStrokeType (0.7f));

        const auto bushing = juce::Rectangle<float> (nut * 0.55f, nut * 0.55f).withCentre (centre);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffcfd0d1), bushing.getX(), bushing.getY(),
                                                 juce::Colour (0xff4a4c4e), bushing.getRight(), bushing.getBottom(), false));
        g.fillEllipse (bushing);

        const auto length = nut * 1.25f;
        const auto tip    = centre + direction * length;
        const auto across = juce::Point<float> (-direction.y, direction.x);
        const auto baseW  = nut * 0.17f;
        const auto tipW   = nut * 0.24f;

        juce::Path lever;
        lever.startNewSubPath (centre + across * baseW);
        lever.lineTo (tip + across * tipW);
        lever.lineTo (tip - across * tipW);
        lever.lineTo (centre - across * baseW);
        lever.closeSubPath();

        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillPath (lever, juce::AffineTransform::translation (1.5f, 2.5f));

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), tip.x - tipW, tip.y - tipW,
                                                 juce::Colour (0xff6b6d6f), tip.x + tipW, tip.y + tipW, false));
        g.fillPath (lever);

        const auto ball = juce::Rectangle<float> (tipW * 2.3f, tipW * 2.3f).withCentre (tip);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), ball.getX() + ball.getWidth() * 0.3f, ball.getY() + ball.getHeight() * 0.3f,
                                                 juce::Colour (0xff5d5f61), ball.getRight(), ball.getBottom(), true));
        g.fillEllipse (ball);
    }
}

void Toggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto nut    = juce::jmin (bounds.getWidth(), bounds.getHeight() * 0.5f) * 0.80f;

    paintBat (g, bounds.getCentre(), nut, { 0.0f, getToggleState() ? -1.0f : 1.0f });

    if (highlighted)
    {
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillEllipse (bounds.withSizeKeepingCentre (nut * 1.6f, nut * 1.6f));
    }

    if (! isEnabled())
    {
        g.setColour (plateBottom.withAlpha (0.55f));
        g.fillRect (bounds);
    }
}

// ---------------------------------------------------------------------------

LitButton::LitButton (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, juce::String legend,
                      float textHeight)
    : juce::Button (legend),
      legendHeight (textHeight),
      attachment (state, parameterID, *this)
{
    setClickingTogglesState (true);

    if (auto* parameter = state.getParameter (parameterID))
        setTooltip (parameter->getName (64));
}

void LitButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto on     = getToggleState();

    // The bezel.
    g.setColour (juce::Colour (0xff2c2c2d));
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    auto cap = bounds.reduced (bounds.getWidth() * 0.09f);

    if (down)
        cap = cap.translated (0.0f, 1.0f);

    if (on)
    {
        // Lit from behind: a warm white diffuser.
        g.setColour (juce::Colour (0xfffff4d8).withAlpha (0.20f));
        g.fillRoundedRectangle (bounds.expanded (5.0f), 7.0f);

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), cap.getCentreX(), cap.getCentreY(),
                                                 juce::Colour (0xfff2e7cc), cap.getRight(), cap.getBottom(), true));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffcfccc4), cap.getCentreX(), cap.getY(),
                                                 juce::Colour (0xff9d9a93), cap.getCentreX(), cap.getBottom(), false));
    }

    g.fillRoundedRectangle (cap, 2.0f);
    g.setColour (juce::Colours::black.withAlpha (highlighted ? 0.45f : 0.30f));
    g.drawRoundedRectangle (cap, 2.0f, 0.8f);

    drawLabel (g, getButtonText(), cap, legendHeight, juce::Justification::centred,
               juce::Colour (on ? 0xff2a2826 : 0xff4b4843));
}

// ---------------------------------------------------------------------------

CueSwitch::CueSwitch (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID)
    : parameter (*state.getParameter (parameterID)),
      attachment (parameter, [this] (float value)
                  {
                      position = juce::roundToInt (value);
                      repaint();
                  })
{
    setTooltip (parameter.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void CueSwitch::mouseDown (const juce::MouseEvent& e)
{
    const auto third = (float) getWidth() / 3.0f;
    const auto x = (float) e.position.x;
    const auto clicked = x < third ? 0 : x > 2.0f * third ? 2 : 1;

    attachment.setValueAsCompleteGesture ((float) (clicked == position ? 1 : clicked));
}

void CueSwitch::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto nut    = juce::jmin (bounds.getHeight(), bounds.getWidth() * 0.5f) * 0.80f;

    const auto direction = position == 0 ? juce::Point<float> (-0.80f, -0.60f)
                         : position == 2 ? juce::Point<float> ( 0.80f, -0.60f)
                                         : juce::Point<float> ( 0.0f,  -0.25f);

    paintBat (g, bounds.getCentre(), nut, direction);
}

// ---------------------------------------------------------------------------

Led::Led (juce::Colour c) : colour (c)
{
    setInterceptsMouseClicks (false, false);
}

void Led::setLit (bool shouldBeLit)
{
    if (lit != shouldBeLit)
    {
        lit = shouldBeLit;
        repaint();
    }
}

void Led::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto lens   = bounds.withSizeKeepingCentre (bounds.getWidth() * 0.45f, bounds.getHeight() * 0.45f);

    if (lit)
    {
        juce::ColourGradient halo (colour.withAlpha (0.55f), lens.getCentreX(), lens.getCentreY(),
                                   colour.withAlpha (0.0f), bounds.getRight(), lens.getCentreY(), true);
        g.setGradientFill (halo);
        g.fillEllipse (bounds);
    }

    g.setColour (juce::Colour (0xff3a3a3a));
    g.fillEllipse (lens.expanded (1.5f));

    g.setColour (lit ? colour : colour.darker (2.2f).withMultipliedSaturation (0.5f));
    g.fillEllipse (lens);

    g.setColour (juce::Colours::white.withAlpha (lit ? 0.7f : 0.18f));
    g.fillEllipse (lens.withSizeKeepingCentre (lens.getWidth() * 0.35f, lens.getHeight() * 0.35f)
                       .translated (-lens.getWidth() * 0.15f, -lens.getHeight() * 0.15f));
}

} // namespace tide::panel
