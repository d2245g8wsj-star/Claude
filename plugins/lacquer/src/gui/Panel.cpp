#include "Panel.h"

namespace lacquer::panel
{

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;   // seven o'clock
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;   // five o'clock

    juce::Point<float> onCircle (juce::Point<float> centre, float radius, float angle)
    {
        return centre.getPointOnCircumference (radius, angle);
    }

    /** An engraved stroke: the cut in shadow just above, the fill on it. */
    void engrave (juce::Graphics& g, const juce::Path& path, float width, juce::Colour colour)
    {
        const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.strokePath (path, stroke, juce::AffineTransform::translation (0.0f, 0.7f));
        g.setColour (colour);
        g.strokePath (path, stroke);
    }

    /** A response curve across the glyph's box: x in 0..1, y returned in -1..1. */
    juce::Path curve (juce::Rectangle<float> area, const std::function<float (float)>& shape)
    {
        juce::Path p;

        for (int i = 0; i <= 32; ++i)
        {
            const auto t = (float) i / 32.0f;
            const auto point = juce::Point<float> (area.getX() + t * area.getWidth(),
                                                   area.getCentreY() - 0.5f * area.getHeight() * shape (t));
            if (i == 0) p.startNewSubPath (point);
            else        p.lineTo (point);
        }

        return p;
    }

    float smoothStep (float t) { t = juce::jlimit (0.0f, 1.0f, t); return t * t * (3.0f - 2.0f * t); }
}

juce::Font plateFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

void drawTrackedLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       float height, float tracking, juce::Justification justification,
                       juce::Colour colour)
{
    juce::GlyphArrangement glyphs;
    auto font = plateFont (height);
    font.setExtraKerningFactor (tracking / height);

    glyphs.addFittedText (font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                          justification, 1, 1.0f);

    // Engraved into a light plate: the lower wall of the cut catches the light.
    g.setColour (juce::Colours::white.withAlpha (0.75f));
    glyphs.draw (g, juce::AffineTransform::translation (0.0f, 0.8f));

    g.setColour (colour);
    glyphs.draw (g);
}

void drawGlyph (juce::Graphics& g, Glyph glyph, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto w = juce::jmax (1.2f, area.getHeight() * 0.09f);
    const auto box = area.reduced (area.getWidth() * 0.06f, area.getHeight() * 0.12f);

    switch (glyph)
    {
        case Glyph::lowShelfBoost:
            engrave (g, curve (box, [] (float t) { return 0.85f - 1.6f * smoothStep ((t - 0.25f) / 0.5f)
                                                         + 0.25f * std::exp (-std::pow ((t - 0.3f) / 0.1f, 2.0f)); }), w, colour);
            break;

        case Glyph::lowShelfCut:
            engrave (g, curve (box, [] (float t) { return -0.8f + 1.6f * smoothStep ((t - 0.25f) / 0.5f); }), w, colour);
            break;

        case Glyph::bellBoost:
            engrave (g, curve (box, [] (float t) { return -0.7f + 1.6f * std::exp (-std::pow ((t - 0.5f) / 0.17f, 2.0f)); }), w, colour);
            break;

        case Glyph::bellCut:
            engrave (g, curve (box, [] (float t) { return 0.7f - 1.6f * std::exp (-std::pow ((t - 0.5f) / 0.09f, 2.0f)); }), w, colour);
            break;

        case Glyph::trebleBoost:
        {
            // A bell and a shelf drawn together: the switch is either.
            const auto left  = box.withWidth (box.getWidth() * 0.48f);
            const auto right = box.withTrimmedLeft (box.getWidth() * 0.52f);
            engrave (g, curve (left,  [] (float t) { return -0.7f + 1.6f * std::exp (-std::pow ((t - 0.5f) / 0.18f, 2.0f)); }), w, trebleBellColour);
            engrave (g, curve (right, [] (float t) { return -0.8f + 1.6f * smoothStep ((t - 0.2f) / 0.6f); }), w, trebleShelfColour);
            break;
        }

        case Glyph::highShelfCut:
            engrave (g, curve (box, [] (float t) { return 0.8f - 1.6f * smoothStep ((t - 0.25f) / 0.5f); }), w, colour);
            break;

        case Glyph::width:
        {
            // A wide and a narrow bell, one inside the other.
            engrave (g, curve (box, [] (float t) { return -0.8f + 1.6f * std::exp (-std::pow ((t - 0.5f) / 0.26f, 2.0f)); }), w * 0.8f, colour);
            engrave (g, curve (box, [] (float t) { return -0.8f + 1.6f * std::exp (-std::pow ((t - 0.5f) / 0.07f, 2.0f)); }), w * 0.8f, colour);
            break;
        }

        case Glyph::shift:
        {
            // Two shelves, the second moved along: a second filter set.
            engrave (g, curve (box, [] (float t) { return 0.7f - 1.4f * smoothStep ((t - 0.10f) / 0.4f); }), w * 0.85f, colour);
            engrave (g, curve (box, [] (float t) { return 0.7f - 1.4f * smoothStep ((t - 0.45f) / 0.4f); }), w * 0.85f, colour.withAlpha (0.55f));
            break;
        }

        case Glyph::buffer:
        {
            // An amplifier triangle with its input lead.
            const auto tri = box.withSizeKeepingCentre (box.getHeight() * 1.05f, box.getHeight());
            juce::Path p;
            p.addTriangle (tri.getX(), tri.getY(), tri.getX(), tri.getBottom(), tri.getRight(), tri.getCentreY());
            p.startNewSubPath (box.getX(), tri.getCentreY());
            p.lineTo (tri.getX(), tri.getCentreY());
            p.startNewSubPath (tri.getRight(), tri.getCentreY());
            p.lineTo (box.getRight(), tri.getCentreY());
            engrave (g, p, w, colour);
            break;
        }

        case Glyph::transformer:
        {
            // Two windings either side of a laminated core.
            juce::Path p;
            const auto cx = box.getCentreX();
            const auto r  = box.getHeight() / 8.0f;

            for (int side = 0; side < 2; ++side)
            {
                const auto x = side == 0 ? cx - 3.0f * w : cx + 3.0f * w;

                for (int i = 0; i < 4; ++i)
                {
                    const auto y = box.getY() + r + 2.0f * r * (float) i;
                    p.addCentredArc (x, y, r, r, 0.0f,
                                     side == 0 ? juce::MathConstants<float>::pi : 0.0f,
                                     side == 0 ? juce::MathConstants<float>::twoPi : juce::MathConstants<float>::pi, true);
                }
            }

            p.startNewSubPath (cx - 0.8f * w, box.getY());  p.lineTo (cx - 0.8f * w, box.getBottom());
            p.startNewSubPath (cx + 0.8f * w, box.getY());  p.lineTo (cx + 0.8f * w, box.getBottom());
            engrave (g, p, w * 0.8f, colour);
            break;
        }

        case Glyph::power:
        {
            const auto c = box.withSizeKeepingCentre (box.getHeight(), box.getHeight());
            juce::Path p;
            p.addCentredArc (c.getCentreX(), c.getCentreY(), c.getWidth() * 0.45f, c.getHeight() * 0.45f, 0.0f,
                             0.7f, juce::MathConstants<float>::twoPi - 0.7f, true);
            p.startNewSubPath (c.getCentreX(), c.getY() - 1.0f);
            p.lineTo (c.getCentreX(), c.getCentreY());
            engrave (g, p, w, colour);
            break;
        }

        case Glyph::link:
        {
            const auto h = box.getHeight() * 0.55f;
            juce::Path p;
            p.addRoundedRectangle (box.getCentreX() - h * 1.5f, box.getCentreY() - h * 0.5f, h * 1.8f, h, h * 0.5f);
            p.addRoundedRectangle (box.getCentreX() - h * 0.3f, box.getCentreY() - h * 0.5f, h * 1.8f, h, h * 0.5f);
            engrave (g, p, w * 0.9f, colour);
            break;
        }

        case Glyph::output:
        {
            // A level wedge.
            juce::Path p;
            p.startNewSubPath (box.getX(), box.getBottom());
            p.lineTo (box.getRight(), box.getBottom());
            p.lineTo (box.getRight(), box.getY());
            p.closeSubPath();
            engrave (g, p, w * 0.9f, colour);
            break;
        }
    }
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float diameter)
{
    const auto head = juce::Rectangle<float> (diameter, diameter).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillEllipse (head.expanded (1.5f).translated (0.0f, 1.0f));

    juce::ColourGradient metal (juce::Colour (0xfff4f4f2), head.getX(), head.getY(),
                                juce::Colour (0xff8d8f90), head.getRight(), head.getBottom(), false);
    metal.addColour (0.5, juce::Colour (0xffc9cbcc));
    g.setGradientFill (metal);
    g.fillEllipse (head);

    g.setColour (juce::Colour (0xff55575a));
    const auto r = diameter * 0.32f;
    g.drawLine (centre.x - r, centre.y - r * 0.2f, centre.x + r, centre.y + r * 0.2f, diameter * 0.12f);
    g.drawLine (centre.x - r * 0.2f, centre.y + r, centre.x + r * 0.2f, centre.y - r, diameter * 0.12f);
}

void drawRackEar (juce::Graphics& g, juce::Rectangle<float> area)
{
    for (auto y : { area.getY() + 36.0f, area.getCentreY(), area.getBottom() - 36.0f })
    {
        const auto slot = juce::Rectangle<float> (area.getWidth() * 0.58f, 13.0f)
                              .withCentre ({ area.getCentreX(), y });

        g.setColour (juce::Colour (0xff1e1d1b));
        g.fillRoundedRectangle (slot, 6.5f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.drawRoundedRectangle (slot.translated (0.0f, 0.8f), 6.5f, 0.8f);

        drawScrew (g, slot.getCentre(), 15.0f);
    }
}

// ---------------------------------------------------------------------------

SteppedKnob::SteppedKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                          Size knobSize, std::vector<Mark> scaleMarks)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      size (knobSize),
      marks (std::move (scaleMarks)),
      attachment (state, parameterID, *this)
{
    setRotaryParameters (startAngle, endAngle, true);
    setPopupDisplayEnabled (true, true, nullptr, 1400);
    setVelocityBasedMode (false);

    // A stepped switch moves one position per few millimetres of drag
    // whatever its number of positions.
    setMouseDragSensitivity (juce::jlimit (120, 320, 22 * (int) marks.size()));

    if (auto* parameter = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        setTooltip (parameter->getName (64));
    }
}

std::vector<SteppedKnob::Mark> SteppedKnob::rampMarks (int positions, juce::Colour colour)
{
    std::vector<Mark> m;

    for (int i = 0; i < positions; ++i)
    {
        const auto t = (float) i / (float) juce::jmax (1, positions - 1);

        // Whole numbers get a full dot, half steps a small one; the first
        // position, where the band does nothing, is a bar.
        if (i == 0)
            m.push_back ({ neutralColour, 1.0f, true });
        else
            m.push_back ({ colour, (i % 2 == 0 ? 0.75f : 0.45f) + 0.55f * t, false });
    }

    return m;
}

std::vector<SteppedKnob::Mark> SteppedKnob::evenMarks (int positions, juce::Colour colour)
{
    return std::vector<Mark> ((size_t) positions, Mark { colour, 1.0f, false });
}

void SteppedKnob::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();

    const auto radius = bounds.getWidth() * (size == Size::large ? 0.31f : size == Size::medium ? 0.30f : 0.29f);
    const auto dotRadius = radius + juce::jmax (7.0f, radius * 0.24f);
    const auto dotSize   = juce::jmax (3.2f, radius * 0.13f);

    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto angle      = startAngle + proportion * (endAngle - startAngle);
    const auto selected   = marks.size() > 1 ? juce::roundToInt (proportion * (float) (marks.size() - 1)) : 0;
    const auto enabled    = isEnabled();

    // The scale: a dot per position, engraved and colour filled.
    for (size_t i = 0; i < marks.size(); ++i)
    {
        const auto t = marks.size() > 1 ? (float) i / (float) (marks.size() - 1) : 0.0f;
        const auto a = startAngle + t * (endAngle - startAngle);
        const auto& mark = marks[i];
        const auto colour = enabled ? mark.colour : mark.colour.withSaturation (0.0f).withAlpha (0.35f);

        if (mark.bar)
        {
            juce::Path bar;
            bar.startNewSubPath (onCircle (centre, dotRadius - dotSize * 0.7f, a));
            bar.lineTo (onCircle (centre, dotRadius + dotSize * 0.7f, a));
            engrave (g, bar, dotSize * 0.45f, colour);
            continue;
        }

        const auto d   = dotSize * mark.size;
        const auto dot = juce::Rectangle<float> (d, d).withCentre (onCircle (centre, dotRadius, a));

        g.setColour (juce::Colours::white.withAlpha (0.7f));
        g.fillEllipse (dot.translated (0.0f, 0.7f));
        g.setColour (colour);
        g.fillEllipse (dot);

        if ((int) i == selected && enabled)
        {
            g.setColour (colour.withAlpha (0.35f));
            g.drawEllipse (dot.expanded (2.0f), 1.0f);
        }
    }

    // Shadow cast onto the plate, offset down and right of the light.
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.fillEllipse (juce::Rectangle<float> (radius * 2.0f, radius * 2.0f)
                           .withCentre (centre.translated (1.0f + (float) i, 2.5f + 1.2f * (float) i))
                           .expanded ((float) i));
    }

    // Skirt: black phenolic with a fluted edge that turns with the knob.
    const auto skirt = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3a3b), skirt.getX(), skirt.getY(),
                                             juce::Colour (0xff070707), skirt.getRight(), skirt.getBottom(), false));
    g.fillEllipse (skirt);

    constexpr int flutes = 36;

    for (int i = 0; i < flutes; ++i)
    {
        const auto a = angle + (float) i * juce::MathConstants<float>::twoPi / (float) flutes;
        const auto lit = 0.5f + 0.5f * std::cos (a + juce::MathConstants<float>::pi * 0.75f);
        g.setColour (juce::Colours::white.withAlpha (0.03f + 0.09f * lit));
        g.drawLine ({ onCircle (centre, radius * 0.86f, a), onCircle (centre, radius * 0.995f, a) }, 1.2f);
    }

    // Body: a raised dome with the light from the upper left.
    const auto body = skirt.reduced (radius * 0.16f);
    juce::ColourGradient dome (juce::Colour (0xff4a4a4c), body.getX() + body.getWidth() * 0.28f, body.getY() + body.getHeight() * 0.22f,
                               juce::Colour (0xff050505), body.getRight(), body.getBottom(), true);
    dome.addColour (0.45, juce::Colour (0xff1b1b1c));
    g.setGradientFill (dome);
    g.fillEllipse (body);

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (body, 1.0f);

    // Specular highlight.
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.fillEllipse (body.withSizeKeepingCentre (body.getWidth() * 0.5f, body.getHeight() * 0.32f)
                       .translated (-body.getWidth() * 0.12f, -body.getHeight() * 0.22f));

    // The index: a white filled line from the centre of the cap to the skirt.
    g.setColour (enabled ? juce::Colour (0xfff6f3ea) : juce::Colour (0xff8a8984));
    g.drawLine ({ onCircle (centre, radius * 0.18f, angle), onCircle (centre, radius * 0.97f, angle) },
                juce::jmax (2.0f, radius * 0.075f));

    if (! enabled)
    {
        g.setColour (plateTop.withAlpha (0.45f));
        g.fillEllipse (skirt.expanded (1.0f));
    }
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

void Toggle::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const auto up     = getToggleState();
    const auto nut    = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.42f;

    // Hex nut on the plate.
    juce::Path hex;
    hex.addPolygon (centre, 6, nut * 0.5f, juce::MathConstants<float>::pi / 6.0f);

    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.fillPath (hex, juce::AffineTransform::translation (1.0f, 2.0f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff5f5f3), centre.x - nut * 0.4f, centre.y - nut * 0.4f,
                                             juce::Colour (0xff8a8c8e), centre.x + nut * 0.4f, centre.y + nut * 0.5f, false));
    g.fillPath (hex);
    g.setColour (juce::Colour (0xff6d6f71));
    g.strokePath (hex, juce::PathStrokeType (0.8f));

    // Bushing.
    const auto bushing = juce::Rectangle<float> (nut * 0.52f, nut * 0.52f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffd9dadb), bushing.getX(), bushing.getY(),
                                             juce::Colour (0xff5f6163), bushing.getRight(), bushing.getBottom(), false));
    g.fillEllipse (bushing);

    // Bat handle, tapering away from the pivot, seen from a little above.
    const auto length = nut * 1.3f;
    const auto tip    = centre.translated (0.0f, up ? -length : length);
    const auto baseW  = nut * 0.20f;
    const auto tipW   = nut * 0.30f;

    juce::Path lever;
    lever.startNewSubPath (centre.translated (-baseW, 0.0f));
    lever.lineTo (tip.translated (-tipW * 0.75f, 0.0f));
    lever.lineTo (tip.translated ( tipW * 0.75f, 0.0f));
    lever.lineTo (centre.translated (baseW, 0.0f));
    lever.closeSubPath();

    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillPath (lever, juce::AffineTransform::translation (2.0f, 3.0f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), tip.x - tipW, tip.y,
                                             juce::Colour (0xff77797b), tip.x + tipW, tip.y, false));
    g.fillPath (lever);

    const auto ball = juce::Rectangle<float> (tipW * 1.7f, tipW * 1.7f).withCentre (tip);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffffff), ball.getX() + ball.getWidth() * 0.3f, ball.getY() + ball.getHeight() * 0.3f,
                                             juce::Colour (0xff6f7173), ball.getRight(), ball.getBottom(), true));
    g.fillEllipse (ball);
    g.setColour (juce::Colour (0xff5d5f61));
    g.drawEllipse (ball, 0.6f);

    if (highlighted)
    {
        g.setColour (juce::Colours::black.withAlpha (0.06f));
        g.fillEllipse (bounds.withSizeKeepingCentre (nut * 1.6f, nut * 1.6f));
    }

    if (! isEnabled())
    {
        g.setColour (plateTop.withAlpha (0.5f));
        g.fillRect (bounds);
    }
}

// ---------------------------------------------------------------------------

PilotLamp::PilotLamp()
{
    setInterceptsMouseClicks (false, false);
}

void PilotLamp::setLit (bool shouldBeLit)
{
    if (lit != shouldBeLit)
    {
        lit = shouldBeLit;
        repaint();
    }
}

void PilotLamp::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto bezel  = bounds.reduced (bounds.getWidth() * 0.18f);
    const auto jewel  = bezel.reduced (bezel.getWidth() * 0.16f);
    const juce::Colour red (0xffe8291c);

    if (lit)
    {
        juce::ColourGradient halo (red.withAlpha (0.45f), bounds.getCentreX(), bounds.getCentreY(),
                                   red.withAlpha (0.0f), bounds.getRight(), bounds.getCentreY(), true);
        g.setGradientFill (halo);
        g.fillEllipse (bounds);
    }

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2f2f0), bezel.getX(), bezel.getY(),
                                             juce::Colour (0xff7d7f81), bezel.getRight(), bezel.getBottom(), false));
    g.fillEllipse (bezel);

    const auto core = lit ? red.brighter (0.2f) : red.darker (1.4f);
    g.setGradientFill (juce::ColourGradient (core.brighter (0.6f), jewel.getX() + jewel.getWidth() * 0.35f, jewel.getY() + jewel.getHeight() * 0.3f,
                                             core.darker (0.6f), jewel.getRight(), jewel.getBottom(), true));
    g.fillEllipse (jewel);

    // Facets.
    g.setColour (juce::Colours::white.withAlpha (lit ? 0.35f : 0.18f));
    for (int i = 0; i < 6; ++i)
    {
        const auto a = (float) i * juce::MathConstants<float>::twoPi / 6.0f;
        g.drawLine ({ jewel.getCentre(), onCircle (jewel.getCentre(), jewel.getWidth() * 0.5f, a) }, 0.5f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.7f));
    g.fillEllipse (jewel.withSizeKeepingCentre (jewel.getWidth() * 0.25f, jewel.getHeight() * 0.18f)
                        .translated (-jewel.getWidth() * 0.15f, -jewel.getHeight() * 0.2f));
}

} // namespace lacquer::panel
