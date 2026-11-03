#include "Panel.h"

namespace indiewave::ui
{

namespace
{
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    float radians (float degrees) { return juce::degreesToRadians (degrees); }

    // Every rotary sweeps 270 degrees, from seven o'clock to five.
    constexpr float sweepStart = 225.0f, sweepEnd = 495.0f;

    juce::Point<float> onCircle (juce::Point<float> c, float r, float angle) { return c.getPointOnCircumference (r, angle); }
}

juce::Font font (float height, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

void drawLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                juce::Colour colour, juce::Justification justification, bool bold, float tracking)
{
    auto f = font (height, bold);
    if (tracking > 0.0f)
        f.setExtraKerningFactor (tracking / height);
    g.setFont (f);
    g.setColour (colour);
    g.drawText (text, area, justification, false);
}

void drawPlate (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour top, juce::Colour bottom, juce::uint32 seed)
{
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRect (r);

    juce::Random grain ((juce::int64) seed);
    for (int i = 0; i < 360; ++i)
    {
        const auto y = r.getY() + grain.nextFloat() * r.getHeight();
        const auto x = r.getX() + grain.nextFloat() * r.getWidth();
        g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.012f + 0.02f * grain.nextFloat()));
        g.drawHorizontalLine ((int) y, x, x + 40.0f + 260.0f * grain.nextFloat());
    }

    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) r.getY(), r.getX(), r.getRight());
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawHorizontalLine ((int) r.getBottom() - 1, r.getX(), r.getRight());
}

void drawRackEar (juce::Graphics& g, juce::Rectangle<float> area, bool leftSide)
{
    const auto bendX = leftSide ? area.getRight() - 1.0f : area.getX();
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (juce::Rectangle<float> (bendX, area.getY(), 1.0f, area.getHeight()));

    for (auto top : { true, false })
    {
        const auto h = juce::jlimit (10.0f, 16.0f, area.getHeight() * 0.05f);
        const auto slot = juce::Rectangle<float> (area.getWidth() * 0.56f, h)
                              .withCentre ({ area.getCentreX(), top ? area.getY() + area.getHeight() * 0.115f
                                                                    : area.getBottom() - area.getHeight() * 0.115f });
        const auto corner = slot.getHeight() * 0.45f;
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
    juce::Path socket;
    socket.addPolygon (centre, 6, diameter * 0.22f, 0.0f);
    g.setColour (juce::Colours::black);
    g.fillPath (socket);
}

void drawLogo (juce::Graphics& g, juce::Point<float> c, float s, juce::Colour ink, juce::Colour dim, bool withName)
{
    g.setColour (ink);
    g.drawEllipse (juce::Rectangle<float> (40.0f * s, 40.0f * s).withCentre (c), 1.6f * s);

    juce::Path wave;
    for (int i = 0; i <= 40; ++i)
    {
        const auto t  = (float) i / 40.0f;
        const auto px = c.x + s * (-14.0f + 28.0f * t);
        const auto py = c.y - s * 6.0f * std::sin (t * twoPi) * (0.4f + 0.6f * t);
        if (i == 0) wave.startNewSubPath (px, py); else wave.lineTo (px, py);
    }
    g.strokePath (wave, juce::PathStrokeType (2.0f * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (withName)
        drawLabel (g, "INDIEWAVE", { c.x - 60.0f * s, c.y + 26.0f * s, 120.0f * s, 12.0f * s }, 8.0f * s,
                   dim, juce::Justification::centred, false, 2.0f * s);
}

void drawSubpanel (juce::Graphics& g, juce::Rectangle<float> r, float corner, juce::Colour top, juce::Colour bottom)
{
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r.translated (1.0f, 3.0f), corner);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void drawLed (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour colour, float brightness)
{
    brightness = juce::jlimit (0.0f, 1.0f, brightness);
    if (brightness > 0.02f)
    {
        g.setColour (colour.withAlpha (0.25f * brightness));
        g.fillEllipse (juce::Rectangle<float> (r * 5.0f, r * 5.0f).withCentre (c));
    }
    g.setColour (juce::Colour (0xff050505));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f + 2.0f, r * 2.0f + 2.0f).withCentre (c));
    g.setColour (juce::Colour (0xff2a2a2c).interpolatedWith (colour, brightness));
    g.fillEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c));
    g.setColour (juce::Colours::white.withAlpha (0.15f + 0.6f * brightness));
    g.fillEllipse (juce::Rectangle<float> (r * 0.7f, r * 0.7f).withCentre (c.translated (-r * 0.3f, -r * 0.3f)));
}

// ---------------------------------------------------------------------------

RingKnob::RingKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, Style s)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      style (std::move (s)),
      attachment (state, parameterID, *this)
{
    setRotaryParameters (radians (sweepStart), radians (sweepEnd), true);
    setPopupDisplayEnabled (true, true, nullptr, 1200);
    setMouseDragSensitivity (220);
    setScrollWheelEnabled (true);

    if (auto* parameter = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        setTooltip (parameter->getName (64));
    }
}

juce::Rectangle<int> RingKnob::boundsAt (juce::Point<float> centre, const Style& s)
{
    const auto half = s.knobRadius + 46.0f;
    return juce::Rectangle<float> (half * 2.0f, half * 2.0f).withCentre (centre).toNearestInt();
}

bool RingKnob::hitTest (int x, int y)
{
    return getLocalBounds().toFloat().getCentre().getDistanceFrom ({ (float) x, (float) y }) < style.knobRadius + 14.0f;
}

void RingKnob::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const auto r = style.knobRadius;
    const auto params = getRotaryParameters();
    const auto start = params.startAngleRadians, end = params.endAngleRadians;
    const auto position = (float) valueToProportionOfLength (getValue());
    const auto angle = start + position * (end - start);
    const auto enabled = isEnabled();

    // The ring.
    const auto ringR = r + 11.0f;
    for (int i = 0; i < style.leds; ++i)
    {
        const auto t = (float) i / (float) juce::jmax (1, style.leds - 1);
        const auto lit = style.bipolar ? ((t >= 0.5f - 1.0e-3f && t <= position + 1.0e-3f) || (t <= 0.5f + 1.0e-3f && t >= position - 1.0e-3f))
                                       : t <= position + 1.0e-3f;
        const auto colour = style.ledColour ? style.ledColour (t) : juce::Colour (0xffff3b2f);
        const auto at = onCircle (c, ringR, start + t * (end - start));

        if (lit && enabled)
        {
            g.setColour (colour.withAlpha (0.28f));
            g.fillEllipse (juce::Rectangle<float> (style.ledRadius * 3.2f, style.ledRadius * 3.2f).withCentre (at));
            g.setColour (colour);
            g.fillEllipse (juce::Rectangle<float> (style.ledRadius * 2.0f, style.ledRadius * 2.0f).withCentre (at));
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.fillEllipse (juce::Rectangle<float> (style.ledRadius * 0.7f, style.ledRadius * 0.7f).withCentre (at.translated (-0.4f, -0.4f)));
        }
        else
        {
            g.setColour (juce::Colour (0xff3b3b3d));
            g.fillEllipse (juce::Rectangle<float> (style.ledRadius * 2.0f, style.ledRadius * 2.0f).withCentre (at));
        }
    }

    // MIN / MAX in the open foot of the ring, the name beneath.
    const auto legendY = c.y + ringR * 0.86f + 2.0f;
    if (style.lowLegend.isNotEmpty())
        drawLabel (g, style.lowLegend, { c.x - ringR * 0.78f - 46.0f, legendY, 44.0f, style.legendHeight + 2.0f },
                   style.legendHeight, style.ink, juce::Justification::centredRight);
    if (style.highLegend.isNotEmpty())
        drawLabel (g, style.highLegend, { c.x + ringR * 0.78f + 2.0f, legendY, 44.0f, style.legendHeight + 2.0f },
                   style.legendHeight, style.ink, juce::Justification::centredLeft);
    if (style.name.isNotEmpty())
        drawLabel (g, style.name, { c.x - 70.0f, c.y + ringR + 9.0f, 140.0f, style.nameHeight + 2.0f },
                   style.nameHeight, style.ink, juce::Justification::centred, true, 0.4f);
    if (style.subName.isNotEmpty())
        drawLabel (g, style.subName, { c.x - 70.0f, c.y + ringR + 11.0f + style.nameHeight, 140.0f, 10.0f },
                   7.0f, style.dim, juce::Justification::centred, true, 0.3f);

    // Shadow on the plate.
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.16f));
        g.fillEllipse (juce::Rectangle<float> (r * 2.0f + (float) i * 2.5f, r * 2.0f + (float) i * 2.5f)
                           .withCentre (c.translated (0.0f, (float) i * 1.2f)));
    }

    // The knurled skirt.
    const auto skirt = juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2c2c2e), skirt.getX(), skirt.getY(),
                                             juce::Colour (0xff070707), skirt.getRight(), skirt.getBottom(), false));
    g.fillEllipse (skirt);
    const int ridges = juce::roundToInt (r * 3.2f);
    for (int i = 0; i < ridges; ++i)
    {
        const auto a = (float) i * twoPi / (float) ridges;
        const auto facing = 0.5f + 0.5f * std::cos (a + radians (40.0f));
        g.setColour (facing > 0.5f ? juce::Colours::white.withAlpha (0.06f + 0.22f * (facing - 0.5f))
                                   : juce::Colours::black.withAlpha (0.5f));
        g.drawLine ({ onCircle (c, r * 0.86f, a), onCircle (c, r, a) }, 0.9f);
    }

    // The machined top: dark, with faint turning rings and a soft sheen.
    const auto top = skirt.reduced (r * 0.16f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2a2c), top.getX() + top.getWidth() * 0.35f, top.getY() + top.getHeight() * 0.3f,
                                             juce::Colour (0xff0a0a0b), top.getRight(), top.getBottom(), true));
    g.fillEllipse (top);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a5a5c), top.getX(), top.getY(),
                                             juce::Colour (0xff3a3a3c), top.getRight(), top.getBottom(), false));
    g.drawEllipse (top, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.035f));
    for (int k = 1; k < 7; ++k)
        g.drawEllipse (juce::Rectangle<float> (r * 1.6f * (float) k / 7.0f, r * 1.6f * (float) k / 7.0f).withCentre (c), 1.0f);

    // The pointer.
    g.setColour (juce::Colour (0xfff2f2f2).withAlpha (enabled ? 1.0f : 0.35f));
    g.drawLine ({ onCircle (c, r * 0.42f, angle), onCircle (c, r * 0.78f, angle) }, juce::jmax (1.6f, r * 0.07f));
}

// ---------------------------------------------------------------------------

Key::Key (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, Style s)
    : juce::Button (parameterID), style (std::move (s))
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterID, *this);
    if (auto* parameter = state.getParameter (parameterID))
        setTooltip (parameter->getName (64));
}

Key::Key (Style s) : juce::Button ({}), style (std::move (s)) {}

void Key::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat();
    const auto on = getToggleState();
    const auto enabled = isEnabled();
    const auto cap = style.capSize;
    const auto lh = style.legendHeight;

    auto y = b.getY();
    if (style.above.isNotEmpty())
    {
        drawLabel (g, style.above, { b.getX(), y, b.getWidth(), lh + 3.0f }, lh, style.ink);
        y += lh + 6.0f;
    }
    if (style.onLamp)
    {
        drawLabel (g, "ON", { b.getX(), y, b.getWidth(), lh + 2.0f }, lh - 0.5f, style.ink);
        drawLed (g, { b.getCentreX(), y + lh + 7.0f }, 2.4f, juce::Colour (0xffff3b2f), on && enabled ? 1.0f : 0.0f);
        y += lh + 15.0f;
    }

    const auto capArea = juce::Rectangle<float> (cap, cap).withCentre ({ b.getCentreX(), y + cap * 0.5f + 2.0f });
    const auto corner = style.kind == Kind::white ? 3.0f : 2.0f;

    if (style.kind == Kind::white && on && enabled)
    {
        g.setColour (style.lit.withAlpha (0.18f));
        g.fillRoundedRectangle (capArea.expanded (5.0f), corner + 4.0f);
    }

    g.setColour (juce::Colour (0xff050505));
    g.fillRoundedRectangle (capArea.expanded (2.0f), corner + 1.0f);

    auto face = style.kind == Kind::white ? (on ? style.lit : juce::Colour (0xffd9d7d0))
                                          : (on && style.lit != juce::Colour (0xfffffdf4) ? style.lit : juce::Colour (0xff2a2a2c));
    if (! enabled) face = face.withMultipliedBrightness (0.5f);
    if (down) face = face.darker (0.15f);
    else if (highlighted) face = face.brighter (0.05f);
    g.setColour (face);
    g.fillRoundedRectangle (capArea, corner);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (on ? 0.35f : 0.18f), 0.0f, capArea.getY(),
                                             juce::Colours::black.withAlpha (0.18f), 0.0f, capArea.getBottom(), false));
    g.fillRoundedRectangle (capArea, corner);

    auto ly = capArea.getBottom() + 5.0f;
    for (const auto& text : { style.below, style.below2 })
        if (text.isNotEmpty())
        {
            drawLabel (g, text, { b.getX(), ly, b.getWidth(), lh + 2.0f }, lh, enabled ? style.ink : style.dim);
            ly += lh + 1.0f;
        }
}

// ---------------------------------------------------------------------------

ChoiceKeys::ChoiceKeys (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                        juce::StringArray names, std::vector<int> slots, juce::Colour inkColour)
    : labels (std::move (names)), order (std::move (slots)), ink (inkColour),
      attachment (*state.getParameter (parameterID),
                  [this] (float value)
                  {
                      selected = juce::roundToInt (value);
                      repaint();
                      if (onChange) onChange (selected);
                  },
                  nullptr)
{
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> ChoiceKeys::keyArea (int slot) const
{
    const auto w = (float) getWidth() / (float) order.size();
    return juce::Rectangle<float> (30.0f, 30.0f).withCentre ({ w * ((float) slot + 0.5f), (float) getHeight() - 17.0f });
}

void ChoiceKeys::paint (juce::Graphics& g)
{
    const auto w = (float) getWidth() / (float) order.size();
    for (size_t slot = 0; slot < order.size(); ++slot)
    {
        const auto key = keyArea ((int) slot);
        const auto on = order[slot] == selected;
        drawLabel (g, labels[(int) slot], { w * (float) slot, 0.0f, w, 12.0f }, 9.0f, ink);

        if (on)
        {
            g.setColour (juce::Colour (0xfffffdf4).withAlpha (0.18f));
            g.fillRoundedRectangle (key.expanded (5.0f), 7.0f);
        }
        g.setColour (juce::Colour (0xff050505));
        g.fillRoundedRectangle (key.expanded (2.0f), 4.0f);
        g.setColour (on ? juce::Colour (0xfffffdf4) : juce::Colour (0xffd9d7d0));
        g.fillRoundedRectangle (key, 3.0f);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (on ? 0.35f : 0.18f), 0.0f, key.getY(),
                                                 juce::Colours::black.withAlpha (0.18f), 0.0f, key.getBottom(), false));
        g.fillRoundedRectangle (key, 3.0f);
    }
}

void ChoiceKeys::mouseDown (const juce::MouseEvent& e)
{
    for (size_t slot = 0; slot < order.size(); ++slot)
        if (keyArea ((int) slot).expanded (6.0f).contains (e.position))
            attachment.setValueAsCompleteGesture ((float) order[slot]);
}

// ---------------------------------------------------------------------------

LedLadder::LedLadder (std::vector<float> stepsDb, juce::StringArray names, bool left, juce::Colour inkColour)
    : steps (std::move (stepsDb)), labels (std::move (names)), labelsLeft (left), ink (inkColour)
{
    setInterceptsMouseClicks (false, false);
}

void LedLadder::push (float levelDb)
{
    // Fast up, falling at about 20 dB a second at 30 frames.
    shown = levelDb > shown ? levelDb : juce::jmax (levelDb, shown - 0.7f);
    repaint();
}

void LedLadder::paint (juce::Graphics& g)
{
    const auto n = (int) steps.size();
    const auto b = getLocalBounds().toFloat();
    const auto x = labelsLeft ? b.getRight() - 6.0f : b.getX() + 6.0f;

    for (int i = 0; i < n; ++i)
    {
        const auto y = b.getBottom() - 6.0f - (b.getHeight() - 12.0f) * (float) i / (float) (n - 1);
        const auto fraction = (float) i / (float) (n - 1);
        const auto colour = fraction > 0.82f ? juce::Colour (0xffff3b2f) : fraction > 0.62f ? juce::Colour (0xffffc23a) : juce::Colour (0xff3dff6a);
        drawLed (g, { x, y }, 2.2f, colour, shown >= steps[(size_t) i] ? 1.0f : 0.0f);

        if (i < labels.size())
            drawLabel (g, labels[i], labelsLeft ? juce::Rectangle<float> (b.getX(), y - 5.0f, b.getWidth() - 14.0f, 10.0f)
                                                : juce::Rectangle<float> (b.getX() + 14.0f, y - 5.0f, b.getWidth() - 14.0f, 10.0f),
                       7.5f, ink, labelsLeft ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
    }
}

// ---------------------------------------------------------------------------

VuMeter::VuMeter (std::vector<std::pair<float, juce::String>> scaleMarks, std::function<float (float)> map,
                  juce::String text, float red)
    : marks (std::move (scaleMarks)), toPosition (std::move (map)), legend (std::move (text)), redFrom (red)
{
    setInterceptsMouseClicks (false, false);
}

void VuMeter::push (float value)
{
    // Moving coil ballistics: about 300 ms to settle at 30 frames.
    const auto target = juce::jlimit (0.0f, 1.0f, toPosition (value));
    needle += (target - needle) * 0.3f;
    repaint();
}

void VuMeter::setLegend (juce::String text)
{
    if (text != legend) { legend = std::move (text); repaint(); }
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto bezel = getLocalBounds().toFloat();
    const auto face = bezel.reduced (5.0f);
    g.setColour (juce::Colour (0xff0b0b0c));
    g.fillRoundedRectangle (bezel, 5.0f);

    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (face.toNearestInt());

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffffef8), face.getCentreX(), face.getBottom(),
                                             juce::Colour (0xfff1ede0), face.getCentreX(), face.getY(), true));
    g.fillRect (face);
    g.setColour (juce::Colour (0xfffff2c8).withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (face.getWidth() * 1.2f, face.getHeight()).withCentre ({ face.getCentreX(), face.getBottom() }));

    const auto ink = juce::Colour (0xff222222), red = juce::Colour (0xffc8322a);
    const auto pivot = juce::Point<float> (face.getCentreX(), face.getY() + face.getHeight() * 1.02f);
    const auto radius = face.getHeight() * 0.78f;
    const auto angleAt = [] (float t) { return radians (-48.0f + 96.0f * t); };

    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleAt (0.0f), angleAt (juce::jmin (1.0f, redFrom)), true);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (1.2f));
    if (redFrom < 1.0f)
    {
        juce::Path hot;
        hot.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleAt (redFrom), angleAt (1.0f), true);
        g.setColour (red);
        g.strokePath (hot, juce::PathStrokeType (3.0f));
    }

    for (int i = 0; i <= 20; ++i)
    {
        const auto t = (float) i / 20.0f;
        const auto a = angleAt (t);
        g.setColour (t >= redFrom ? red : ink);
        g.drawLine ({ onCircle (pivot, radius, a), onCircle (pivot, radius + (i % 5 == 0 ? 7.0f : 4.0f), a) }, i % 5 == 0 ? 1.2f : 0.8f);
    }

    for (const auto& [t, text] : marks)
    {
        const auto at = onCircle (pivot, radius + 15.0f, angleAt (t));
        drawLabel (g, text, juce::Rectangle<float> (30.0f, 12.0f).withCentre (at), face.getHeight() * 0.1f,
                   t >= redFrom ? red : ink, juce::Justification::centred, true);
    }

    drawLabel (g, legend, { face.getX(), face.getY() + face.getHeight() * 0.74f, face.getWidth(), 12.0f },
               face.getHeight() * 0.075f, ink, juce::Justification::centred, true, 1.0f);

    g.setColour (juce::Colour (0xff111111));
    g.drawLine ({ pivot, onCircle (pivot, radius + 8.0f, angleAt (needle)) }, 1.4f);
    g.fillEllipse (juce::Rectangle<float> (face.getWidth() * 0.36f, face.getHeight() * 0.32f).withCentre ({ pivot.x, face.getBottom() + 2.0f }));

    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), 0.0f, face.getY(),
                                             juce::Colours::white.withAlpha (0.0f), 0.0f, face.getY() + face.getHeight() * 0.45f, false));
    g.fillRect (face);
}

// ---------------------------------------------------------------------------

Display::Display (juce::Colour c) : glow (c) { setInterceptsMouseClicks (false, false); }

void Display::setLines (juce::String a, juce::String b)
{
    if (a != top || b != bottom) { top = std::move (a); bottom = std::move (b); repaint(); }
}

void Display::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black);
    g.fillRoundedRectangle (r, 5.0f);
    const auto screen = r.reduced (3.0f);
    g.setColour (juce::Colour (0xff020604));
    g.fillRoundedRectangle (screen, 3.0f);

    const auto mono = juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
    g.setColour (glow);
    g.setFont (mono.withHeight (screen.getHeight() * 0.24f));
    g.drawText (top, screen.withHeight (screen.getHeight() * 0.5f).translated (0.0f, 3.0f), juce::Justification::centred);
    g.setFont (mono.withHeight (screen.getHeight() * 0.32f));
    g.drawText (bottom, screen.withTrimmedTop (screen.getHeight() * 0.45f).translated (0.0f, -3.0f), juce::Justification::centred);

    g.setColour (juce::Colours::white.withAlpha (0.035f));
    g.fillRect (screen.withHeight (screen.getHeight() * 0.45f));
}

// ---------------------------------------------------------------------------

FoldTab::FoldTab (Edge e, juce::Colour inkColour, juce::Colour fillColour)
    : juce::Button ({}), edge (e), ink (inkColour), fill (fillColour)
{
    setTooltip ("Fold out the second channel");
}

void FoldTab::setOpen (bool shouldBeOpen)
{
    open = shouldBeOpen;
    setTooltip (open ? "Fold the second channel away" : "Fold out the second channel");
    repaint();
}

void FoldTab::setCaption (juce::String closed, juce::String opened)
{
    closedText = std::move (closed);
    openText = std::move (opened);
    repaint();
}

void FoldTab::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto b = getLocalBounds().toFloat();
    const auto text = open ? openText : closedText;
    juce::Rectangle<float> tab;

    if (edge == Edge::right)
        tab = b.reduced (1.0f, 0.0f);
    else
        tab = b;

    g.setColour ((down ? fill.darker (0.2f) : highlighted ? fill.brighter (0.1f) : fill));
    g.fillRoundedRectangle (tab, 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.drawRoundedRectangle (tab, 5.0f, 1.0f);

    juce::Path chevron;
    if (edge == Edge::right)
    {
        // The chevron near the top, the caption running down the tab.
        const auto d = open ? -1.0f : 1.0f;
        const auto cy = tab.getY() + 20.0f;
        chevron.startNewSubPath (tab.getCentreX() - 3.0f * d, cy - 6.0f);
        chevron.lineTo (tab.getCentreX() + 3.0f * d, cy);
        chevron.lineTo (tab.getCentreX() - 3.0f * d, cy + 6.0f);

        juce::Graphics::ScopedSaveState state (g);
        const auto centre = juce::Point<float> (tab.getCentreX(), tab.getY() + 40.0f + (tab.getHeight() - 40.0f) * 0.5f);
        g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, centre.x, centre.y));
        drawLabel (g, text, juce::Rectangle<float> (tab.getHeight() - 50.0f, 12.0f).withCentre (centre), 8.0f, ink,
                   juce::Justification::centred, true, 1.2f);
    }
    else
    {
        const auto d = open ? -1.0f : 1.0f;
        const auto cx = tab.getX() + 26.0f;
        chevron.startNewSubPath (cx - 6.0f, tab.getCentreY() - 3.0f * d);
        chevron.lineTo (cx, tab.getCentreY() + 3.0f * d);
        chevron.lineTo (cx + 6.0f, tab.getCentreY() - 3.0f * d);
        drawLabel (g, text, tab.withTrimmedLeft (36.0f), 8.0f, ink, juce::Justification::centredLeft, true, 1.0f);
    }
    g.setColour (ink);
    g.strokePath (chevron, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

} // namespace indiewave::ui
