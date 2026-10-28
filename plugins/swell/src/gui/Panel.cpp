#include "Panel.h"

namespace swell::panel
{

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;   // seven o'clock
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;   // five o'clock

    juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle)
    {
        return centre.getPointOnCircumference (radius, angle);
    }

    /** Polished steel, for screws, nuts and levers: a cool gradient with a hard highlight. */
    juce::ColourGradient steel (juce::Rectangle<float> area)
    {
        juce::ColourGradient gradient (juce::Colour (0xfff2f1ec), area.getX(), area.getY(),
                                       juce::Colour (0xff5b5a57), area.getRight(), area.getBottom(), false);
        gradient.addColour (0.45, juce::Colour (0xffb8b6b0));
        return gradient;
    }
}

juce::Font legendFont (float height)
{
    // Condensed by scaling rather than by family, so it reads the same on every
    // system whichever sans the platform supplies.
    return juce::Font (juce::FontOptions (height, juce::Font::bold)).withHorizontalScale (0.86f);
}

juce::Image renderPaint (int width, int height)
{
    juce::Image image (juce::Image::RGB, width, height, false);

    {
        juce::Graphics g (image);
        g.setGradientFill ({ paint.brighter (0.10f), 0.0f, 0.0f,
                             paint.darker (0.12f), 0.0f, (float) height, false });
        g.fillAll();

        // Enamel never lies perfectly even: a few broad, faint clouds.
        juce::Random random (0x5e11);

        for (int i = 0; i < 18; ++i)
        {
            const auto centre = juce::Point<float> (random.nextFloat() * (float) width, random.nextFloat() * (float) height);
            const auto radius = (0.08f + 0.2f * random.nextFloat()) * (float) width;
            const auto tone   = random.nextBool() ? juce::Colours::white.withAlpha (0.035f)
                                                  : juce::Colours::black.withAlpha (0.05f);

            g.setGradientFill ({ tone, centre.x, centre.y, tone.withAlpha (0.0f), centre.x + radius, centre.y, true });
            g.fillAll();
        }
    }

    // And a fine speckle, the texture of sprayed paint at arm's length.
    juce::Image::BitmapData pixels (image, juce::Image::BitmapData::readWrite);
    juce::Random random (0xc0a7);

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto c = pixels.getPixelColour (x, y);
            const auto n = (random.nextFloat() - 0.5f) * 0.045f;
            pixels.setPixelColour (x, y, n > 0.0f ? c.brighter (n) : c.darker (-n));
        }
    }

    return image;
}

void drawLegend (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height,
                 float tracking, juce::Justification justification, juce::Colour colour)
{
    auto font = legendFont (height);
    font.setExtraKerningFactor (tracking / height);

    g.setFont (font);
    g.setColour (colour);
    g.drawText (text, area, justification, false);
}

void drawSection (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title)
{
    const auto font = legendFont (12.0f).withExtraKerningFactor (0.16f);
    const auto titleWidth = juce::GlyphArrangement::getStringWidth (font, title) + 14.0f;
    const auto gapStart = area.getX() + 14.0f;

    juce::Path box;
    box.startNewSubPath (gapStart, area.getY());
    box.lineTo (area.getX() + 6.0f, area.getY());
    box.quadraticTo (area.getX(), area.getY(), area.getX(), area.getY() + 6.0f);
    box.lineTo (area.getX(), area.getBottom() - 6.0f);
    box.quadraticTo (area.getX(), area.getBottom(), area.getX() + 6.0f, area.getBottom());
    box.lineTo (area.getRight() - 6.0f, area.getBottom());
    box.quadraticTo (area.getRight(), area.getBottom(), area.getRight(), area.getBottom() - 6.0f);
    box.lineTo (area.getRight(), area.getY() + 6.0f);
    box.quadraticTo (area.getRight(), area.getY(), area.getRight() - 6.0f, area.getY());
    box.lineTo (gapStart + titleWidth, area.getY());

    g.setColour (silk.withAlpha (0.55f));
    g.strokePath (box, juce::PathStrokeType (1.2f));

    g.setColour (silk);
    g.setFont (font);
    g.drawText (title, juce::Rectangle<float> (gapStart, area.getY() - 8.0f, titleWidth, 16.0f),
                juce::Justification::centred, false);
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float slotAngle)
{
    const auto head = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (head.translated (0.0f, 1.2f).expanded (0.8f));

    g.setGradientFill (steel (head));
    g.fillEllipse (head);

    const auto offset = juce::Point<float> (std::cos (slotAngle), std::sin (slotAngle)) * (radius * 0.8f);
    g.setColour (juce::Colour (0xff2a2a28));
    g.drawLine ({ centre - offset, centre + offset }, radius * 0.3f);
}

void drawRackEar (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (paint.darker (0.25f));
    g.fillRect (area);

    // The fold where the ear meets the panel catches a line of light.
    const auto inner = area.getX() < 1.0f ? area.getRight() - 1.0f : area.getX();
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawVerticalLine (juce::roundToInt (inner), area.getY(), area.getBottom());

    for (auto y : { area.getY() + area.getHeight() * 0.18f, area.getBottom() - area.getHeight() * 0.18f })
    {
        const auto slot = juce::Rectangle<float> (area.getWidth() * 0.6f, 13.0f).withCentre ({ area.getCentreX(), y });

        g.setColour (juce::Colour (0xff121314));
        g.fillRoundedRectangle (slot, 6.5f);

        drawScrew (g, slot.getCentre(), 6.5f, y < area.getCentreY() ? 0.3f : -0.8f);
    }
}

void drawNameplate (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f), 3.0f);

    juce::ColourGradient metal (brass.brighter (0.25f), area.getX(), area.getY(),
                                brass.darker (0.35f), area.getRight(), area.getBottom(), false);
    metal.addColour (0.5, brass);
    g.setGradientFill (metal);
    g.fillRoundedRectangle (area, 3.0f);

    // Brushed along its length.
    juce::Random random (0xb7a5);
    for (int i = 0; i < 90; ++i)
    {
        const auto y = area.getY() + random.nextFloat() * area.getHeight();
        g.setColour ((random.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.05f));
        g.drawHorizontalLine (juce::roundToInt (y), area.getX() + 2.0f, area.getRight() - 2.0f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawRoundedRectangle (area.reduced (0.5f), 3.0f, 1.0f);

    for (auto x : { area.getX() + 9.0f, area.getRight() - 9.0f })
        drawScrew (g, { x, area.getCentreY() }, 3.6f, 0.6f);

    // Engraved, then filled black, as maker's plates are.
    const auto ink = juce::Colour (0xff1f1a12);
    drawLegend (g, "SWELL", area.withTrimmedBottom (area.getHeight() * 0.36f).translated (0.0f, 3.0f),
                26.0f, 9.0f, juce::Justification::centred, ink);
    drawLegend (g, "HARMONIC EQUALISER", area.withTrimmedTop (area.getHeight() * 0.62f).translated (0.0f, -4.0f),
                8.5f, 2.6f, juce::Justification::centred, ink.withAlpha (0.85f));
}

// ---------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
            juce::Colour capColour, float knobDiameter, Legends scaleLegends)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      cap (capColour),
      diameter (knobDiameter),
      legends (std::move (scaleLegends)),
      attachment (state, parameterID, *this)
{
    setRotaryParameters (startAngle, endAngle, true);
    setPopupDisplayEnabled (true, true, nullptr, 1200);
    setMouseDragSensitivity (240);

    if (auto* parameter = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        setTooltip (parameter->getName (64));
    }
}

void Knob::paint (juce::Graphics& g)
{
    const auto centre = getLocalBounds().toFloat().getCentre();
    const auto radius = diameter * 0.5f;
    const auto angle  = startAngle + (float) valueToProportionOfLength (getValue()) * (endAngle - startAngle);

    // The printed scale: fine ticks the whole way round, a dot and a figure at
    // each legend.
    g.setColour (silk.withAlpha (0.6f));

    for (int i = 0; i <= 20; ++i)
    {
        const auto a = startAngle + (float) i / 20.0f * (endAngle - startAngle);
        g.drawLine ({ onCircle (centre, radius + 4.0f, a), onCircle (centre, radius + (i % 5 == 0 ? 8.0f : 6.0f), a) }, 1.0f);
    }

    for (const auto& [value, text] : legends)
    {
        const auto a = startAngle + (float) valueToProportionOfLength (value) * (endAngle - startAngle);
        const auto at = onCircle (centre, radius + 16.0f, a);

        drawLegend (g, text, juce::Rectangle<float> (30.0f, 11.0f).withCentre (at), 8.5f, 0.2f,
                    juce::Justification::centred, silkDim);
    }

    // Bakelite: a fluted skirt, a flat top, a coloured cap.
    const auto body = juce::Rectangle<float> (diameter, diameter).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (body.translated (1.5f, 2.5f));

    g.setGradientFill ({ bakelite.brighter (0.25f), body.getX(), body.getY(),
                         bakelite.darker (0.4f), body.getRight(), body.getBottom(), false });
    g.fillEllipse (body);

    for (int i = 0; i < 28; ++i)
    {
        const auto a = angle + (float) i * juce::MathConstants<float>::twoPi / 28.0f;
        const auto lit = std::cos (a + 0.8f) * 0.5f + 0.5f;    // flutes facing the light show
        g.setColour (juce::Colours::white.withAlpha (0.03f + 0.09f * lit));
        g.drawLine ({ onCircle (centre, radius * 0.80f, a), onCircle (centre, radius * 0.98f, a) }, 1.6f);
    }

    const auto top = body.reduced (radius * 0.22f);
    g.setGradientFill ({ bakelite.brighter (0.45f), top.getX(), top.getY(),
                         bakelite, top.getCentreX(), top.getCentreY(), false });
    g.fillEllipse (top);
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawEllipse (top, 1.0f);

    const auto insert = body.reduced (radius * 0.56f);
    g.setColour (cap);
    g.fillEllipse (insert);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.drawEllipse (insert, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillEllipse (insert.withSizeKeepingCentre (insert.getWidth() * 0.5f, insert.getHeight() * 0.3f)
                         .translated (-insert.getWidth() * 0.1f, -insert.getHeight() * 0.22f));

    // The painted index line, from the cap out to the edge of the skirt.
    g.setColour (silk);
    g.drawLine ({ onCircle (centre, radius * 0.48f, angle), onCircle (centre, radius * 0.96f, angle) },
                juce::jmax (2.0f, diameter * 0.04f));
}

// ---------------------------------------------------------------------------

Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID)
    : parameter (*state.getParameter (parameterID)),
      attachment (parameter, [this] (float v) { on = v >= 0.5f; repaint(); }, nullptr)
{
    setTooltip (parameter.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

void Toggle::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void Toggle::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();

    drawLegend (g, "IN",  bounds.withHeight (12.0f), 9.0f, 1.0f, juce::Justification::centred,
                on ? silk : silkDim);
    drawLegend (g, "OUT", bounds.withTrimmedTop (bounds.getHeight() - 12.0f), 9.0f, 1.0f, juce::Justification::centred,
                on ? silkDim : silk);

    // Hex nut and bushing.
    juce::Path nut;
    nut.addPolygon (centre, 6, 9.0f, 0.0f);
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillPath (nut, juce::AffineTransform::translation (0.8f, 1.5f));
    g.setGradientFill (steel (nut.getBounds()));
    g.fillPath (nut);
    g.setColour (juce::Colour (0xff2a2a28));
    g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (centre));

    // The lever: a tapered rod ending in a ball, thrown up for IN.
    const auto direction = on ? -1.0f : 1.0f;
    const auto tip = centre + juce::Point<float> (0.0f, direction * 13.0f);

    juce::Path lever;
    lever.startNewSubPath (centre.x - 2.6f, centre.y);
    lever.lineTo (tip.x - 1.6f, tip.y);
    lever.lineTo (tip.x + 1.6f, tip.y);
    lever.lineTo (centre.x + 2.6f, centre.y);
    lever.closeSubPath();

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (lever, juce::AffineTransform::translation (1.5f, 2.0f));
    g.setGradientFill ({ juce::Colour (0xff6d6c69), centre.x - 3.0f, 0.0f, juce::Colour (0xffeeede8), centre.x + 1.0f, 0.0f, true });
    g.fillPath (lever);

    const auto ball = juce::Rectangle<float> (7.0f, 7.0f).withCentre (tip);
    g.setGradientFill (steel (ball));
    g.fillEllipse (ball);
}

// ---------------------------------------------------------------------------

JewelLamp::JewelLamp()
{
    setInterceptsMouseClicks (false, false);
    setTooltip ("Saturation");
}

void JewelLamp::push (float drive)
{
    // Dark until the curve starts to bend, full once it is well into it.
    const auto target = juce::jlimit (0.0f, 1.0f, (drive - 0.35f) / 1.0f);
    const auto next   = target > glow ? target : glow * 0.86f;

    if (std::abs (next - glow) > 0.004f)
    {
        glow = next;
        repaint();
    }
}

void JewelLamp::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 1.0f;

    const auto bezel = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setGradientFill (steel (bezel));
    g.fillEllipse (bezel);

    const auto lens = bezel.reduced (radius * 0.24f);
    const auto lit  = juce::Colour (0xff6a3a12).interpolatedWith (juce::Colour (0xffffb54a), glow);
    g.setColour (lit);
    g.fillEllipse (lens);

    // Facets: alternate wedges catch the light, which is what makes it a jewel.
    juce::Path facets;
    for (int i = 0; i < 8; i += 2)
    {
        const auto a0 = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
        const auto a1 = (float) (i + 1) * juce::MathConstants<float>::twoPi / 8.0f;
        facets.startNewSubPath (centre);
        facets.lineTo (onCircle (centre, lens.getWidth() * 0.5f, a0));
        facets.lineTo (onCircle (centre, lens.getWidth() * 0.5f, a1));
        facets.closeSubPath();
    }

    g.setColour (juce::Colours::white.withAlpha (0.10f + 0.12f * glow));
    g.fillPath (facets);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.4f, radius * 0.3f).withCentre (centre.translated (-radius * 0.25f, -radius * 0.3f)));
}

// ---------------------------------------------------------------------------

namespace
{
    constexpr float vuMin = -20.0f, vuMax = 3.0f;
    constexpr float vuReferenceDbfs = -18.0f;     ///< 0 VU
    constexpr float vuSweep = 0.72f;              ///< radians either side of vertical

    /** A VU scale is linear in voltage, not in dB, so the top few dB get most of the arc. */
    float vuToProportion (float vu)
    {
        const auto v  = std::pow (10.0f, vu / 20.0f);
        const auto lo = std::pow (10.0f, vuMin / 20.0f);
        const auto hi = std::pow (10.0f, vuMax / 20.0f);
        return juce::jlimit (0.0f, 1.0f, (v - lo) / (hi - lo));
    }
}

VuMeter::VuMeter()
{
    setInterceptsMouseClicks (false, false);
}

void VuMeter::setCaption (const juce::String& text)
{
    if (text != caption)
    {
        caption = text;
        repaint();
    }
}

void VuMeter::push (float peak)
{
    const auto vu = juce::Decibels::gainToDecibels (peak, -100.0f) - vuReferenceDbfs;

    // About 300 ms to settle at 30 frames a second, the way a VU movement does.
    needle += (juce::jlimit (vuMin - 2.0f, vuMax + 1.0f, vu) - needle) * 0.24f;
    peakLamp = peak >= 1.0f ? 1.0f : juce::jmax (0.0f, peakLamp - 0.05f);

    repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Bezel and window.
    g.setColour (juce::Colour (0xff141414));
    g.fillRoundedRectangle (bounds, 4.0f);

    const auto face = bounds.reduced (4.0f);
    g.setGradientFill ({ juce::Colour (0xfff3e7c4), face.getCentreX(), face.getCentreY(),
                         juce::Colour (0xffd8c89c), face.getX(), face.getY(), true });
    g.fillRoundedRectangle (face, 2.0f);

    const auto pivot  = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.32f);
    const auto radius = face.getHeight() * 0.92f;
    const auto toAngle = [] (float vu) { return -vuSweep + 2.0f * vuSweep * vuToProportion (vu); };

    const juce::Colour ink (0xff22201c), red (0xffb8382a);

    // The red zone past zero.
    {
        juce::Path zone;
        zone.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, toAngle (0.0f), toAngle (vuMax), true);
        g.setColour (red);
        g.strokePath (zone, juce::PathStrokeType (3.5f));

        juce::Path arc;
        arc.addCentredArc (pivot.x, pivot.y, radius - 2.5f, radius - 2.5f, 0.0f, toAngle (vuMin), toAngle (0.0f), true);
        g.setColour (ink);
        g.strokePath (arc, juce::PathStrokeType (1.0f));
    }

    for (auto [vu, text] : std::initializer_list<std::pair<float, const char*>> {
             { -20.0f, "20" }, { -10.0f, "10" }, { -7.0f, "7" }, { -5.0f, "5" }, { -3.0f, "3" },
             { -2.0f, "" }, { -1.0f, "" }, { 0.0f, "0" }, { 1.0f, "" }, { 2.0f, "" }, { 3.0f, "+3" } })
    {
        const auto a = toAngle (vu);
        const auto major = *text != '\0';

        g.setColour (vu > 0.0f ? red : ink);
        g.drawLine ({ pivot.getPointOnCircumference (radius - 4.0f, a),
                      pivot.getPointOnCircumference (radius + (major ? 5.0f : 3.0f), a) }, 1.0f);

        if (major)
            drawLegend (g, text, juce::Rectangle<float> (20.0f, 10.0f).withCentre (pivot.getPointOnCircumference (radius + 11.0f, a)),
                        8.0f, 0.0f, juce::Justification::centred, vu > 0.0f ? red : ink);
    }

    drawLegend (g, "VU", juce::Rectangle<float> (40.0f, 14.0f).withCentre ({ face.getCentreX(), face.getBottom() - 16.0f }),
                12.0f, 1.5f, juce::Justification::centred, ink);
    drawLegend (g, caption, juce::Rectangle<float> (20.0f, 10.0f).withPosition (face.getX() + 4.0f, face.getBottom() - 13.0f),
                8.5f, 0.5f, juce::Justification::centredLeft, ink.withAlpha (0.7f));

    // The needle, clipped to the window so it disappears behind the bezel.
    {
        juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (face.toNearestInt());

        const auto tip = pivot.getPointOnCircumference (radius + 6.0f, toAngle (needle));
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawLine ({ pivot.translated (2.0f, 1.0f), tip.translated (2.0f, 1.0f) }, 1.4f);
        g.setColour (ink);
        g.drawLine ({ pivot, tip }, 1.4f);
    }

    // Peak lamp, top right of the window.
    const auto lamp = juce::Rectangle<float> (6.0f, 6.0f).withPosition (face.getRight() - 11.0f, face.getY() + 5.0f);
    g.setColour (juce::Colour (0xff5a1a12).interpolatedWith (juce::Colour (0xffff4a32), peakLamp));
    g.fillEllipse (lamp);

    // Glass.
    g.setGradientFill ({ juce::Colours::white.withAlpha (0.16f), face.getX(), face.getY(),
                         juce::Colours::transparentWhite, face.getX(), face.getCentreY(), false });
    g.fillRoundedRectangle (face, 2.0f);
}

// ---------------------------------------------------------------------------

Selector::Selector (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                    juce::StringArray positions)
    : parameter (*state.getParameter (parameterID)),
      names (std::move (positions)),
      attachment (parameter, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr)
{
    setTooltip (parameter.getName (64));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    attachment.sendInitialUpdate();
}

float Selector::angleFor (int position) const
{
    const auto spread = 1.7f;     // radians from the first position to the last
    const auto count  = juce::jmax (2, names.size());
    return -spread * 0.5f + spread * (float) position / (float) (count - 1);
}

juce::Rectangle<float> Selector::labelArea (int position) const
{
    const auto bounds = getLocalBounds().toFloat();
    const auto pivot  = juce::Point<float> (bounds.getCentreX(), bounds.getBottom() - 26.0f);
    const auto at     = pivot.getPointOnCircumference (52.0f, angleFor (position));

    return juce::Rectangle<float> (54.0f, 14.0f).withCentre (at);
}

void Selector::mouseDown (const juce::MouseEvent& event)
{
    for (int i = 0; i < names.size(); ++i)
    {
        if (labelArea (i).expanded (4.0f).contains (event.position))
        {
            attachment.setValueAsCompleteGesture ((float) i);
            return;
        }
    }

    // A click on the knob itself steps it round, as turning it would.
    attachment.setValueAsCompleteGesture ((float) ((index + 1) % juce::jmax (1, names.size())));
}

void Selector::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto pivot  = juce::Point<float> (bounds.getCentreX(), bounds.getBottom() - 26.0f);

    for (int i = 0; i < names.size(); ++i)
    {
        const auto a = angleFor (i);
        g.setColour (silk.withAlpha (0.7f));
        g.drawLine ({ pivot.getPointOnCircumference (27.0f, a), pivot.getPointOnCircumference (33.0f, a) }, 1.4f);

        drawLegend (g, names[i], labelArea (i), 9.5f, 1.0f, juce::Justification::centred,
                    i == index ? silk : silkDim);
    }

    // The chicken head: a round boss and a pointer moulded in one piece.
    const auto a = angleFor (index);
    const auto pointerTip = pivot.getPointOnCircumference (28.0f, a);
    const auto tail       = pivot.getPointOnCircumference (-16.0f, a);
    const auto side       = juce::Point<float> (std::cos (a), std::sin (a)) * 7.0f;

    juce::Path head;
    head.addEllipse (juce::Rectangle<float> (36.0f, 36.0f).withCentre (pivot));
    head.startNewSubPath (tail + side * 1.4f);
    head.lineTo (pointerTip + side * 0.35f);
    head.lineTo (pointerTip - side * 0.35f);
    head.lineTo (tail - side * 1.4f);
    head.closeSubPath();

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillPath (head, juce::AffineTransform::translation (1.5f, 2.5f));
    g.setGradientFill ({ bakelite.brighter (0.4f), pivot.x - 14.0f, pivot.y - 14.0f,
                         bakelite.darker (0.3f), pivot.x + 14.0f, pivot.y + 14.0f, false });
    g.fillPath (head);

    g.setColour (silk);
    g.drawLine ({ pivot.getPointOnCircumference (6.0f, a), pivot.getPointOnCircumference (25.0f, a) }, 2.2f);
}

} // namespace swell::panel
