#include "Panel.h"

namespace kiln::panel
{

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;   // seven o'clock
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;   // five o'clock

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

void drawTrackedLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       float height, float tracking, juce::Justification justification,
                       juce::Colour colour)
{
    juce::GlyphArrangement glyphs;
    auto font = plateFont (height);
    font.setExtraKerningFactor (tracking / height);

    glyphs.addFittedText (font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(),
                          justification, 1, 1.0f);

    // Engraving: a dark cut a fraction above the lit letter.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    glyphs.draw (g, juce::AffineTransform::translation (0.0f, -0.8f));

    g.setColour (colour);
    glyphs.draw (g);
}

void drawWell (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17181b), area.getX(), area.getY(),
                                             juce::Colour (0xff23252a), area.getX(), area.getBottom(), false));
    g.fillRoundedRectangle (area, corner);

    // The upper wall in shadow, the lower lip catching the light.
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.4f);

    juce::Path lip;
    lip.addRoundedRectangle (area.reduced (0.5f), corner);
    g.saveState();
    g.reduceClipRegion (area.withTrimmedTop (area.getHeight() * 0.6f).toNearestInt());
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.strokePath (lip, juce::PathStrokeType (1.0f));
    g.restoreState();
}

void drawRackEar (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d43), area.getX(), area.getY(),
                                             juce::Colour (0xff26282c), area.getRight(), area.getY(), false));
    g.fillRect (area);

    for (auto y : { area.getY() + area.getHeight() * 0.17f, area.getBottom() - area.getHeight() * 0.17f })
    {
        const auto slot = juce::Rectangle<float> (area.getWidth() * 0.62f, 12.0f)
                              .withCentre ({ area.getCentreX(), y });

        g.setColour (recess);
        g.fillRoundedRectangle (slot, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawRoundedRectangle (slot.translated (0.0f, 0.8f), 6.0f, 0.8f);

        // A pan head screw seated in the slot.
        const auto head = juce::Rectangle<float> (13.0f, 13.0f).withCentre (slot.getCentre());
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffcfcfcb), head.getX(), head.getY(),
                                                 juce::Colour (0xff6c6d6b), head.getRight(), head.getBottom(), false));
        g.fillEllipse (head);
        g.setColour (juce::Colour (0xff3b3c3b));
        g.drawLine (head.getX() + 3.0f, head.getCentreY(), head.getRight() - 3.0f, head.getCentreY(), 1.6f);
        g.drawLine (head.getCentreX(), head.getY() + 3.0f, head.getCentreX(), head.getBottom() - 3.0f, 1.6f);
    }
}

// ---------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
            juce::Colour accentColour, Size knobSize, Legends scaleLegends)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
      accent (accentColour),
      size (knobSize),
      legends (std::move (scaleLegends)),
      attachment (state, parameterID, *this)
{
    setRotaryParameters (startAngle, endAngle, true);
    setPopupDisplayEnabled (true, true, nullptr, 1200);
    setMouseDragSensitivity (size == Size::large ? 260 : 220);
    setVelocityBasedMode (false);

    if (auto* parameter = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        setTooltip (parameter->getName (64));
    }
}

void Knob::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentre();
    const auto large  = size == Size::large;

    const auto radius      = bounds.getWidth() * (large ? 0.30f : 0.28f);
    const auto tickInner   = radius + 4.0f;
    const auto tickOuter   = radius + (large ? 9.0f : 7.0f);
    const auto legendRadius = radius + (large ? 18.0f : 14.5f);

    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto angle      = startAngle + proportion * (endAngle - startAngle);

    // Scale: fine ticks between the legends, longer ones on them.
    g.setColour (engravingDim);

    constexpr int fineTicks = 21;

    for (int i = 0; i < fineTicks; ++i)
    {
        const auto a = startAngle + (float) i / (fineTicks - 1) * (endAngle - startAngle);
        g.drawLine ({ onCircle (centre, tickInner, a), onCircle (centre, tickInner + 2.5f, a) }, 1.0f);
    }

    for (const auto& [value, text] : legends)
    {
        const auto p = (float) valueToProportionOfLength (value);
        const auto a = startAngle + p * (endAngle - startAngle);

        g.setColour (engraving);
        g.drawLine ({ onCircle (centre, tickInner, a), onCircle (centre, tickOuter, a) }, 1.4f);

        const auto at = onCircle (centre, legendRadius, a);
        const auto box = juce::Rectangle<float> (36.0f, 11.0f).withCentre (at);
        drawTrackedLabel (g, text, box, large ? 8.5f : 7.5f, 0.3f);
    }

    // Shadow cast onto the plate.
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (juce::Rectangle<float> (radius * 2.1f, radius * 2.1f).withCentre (centre.translated (0.0f, 2.5f)));

    // Skirt, in the band's colour, knurled.
    const auto skirt = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (accent.brighter (0.25f), skirt.getX(), skirt.getY(),
                                             accent.darker (0.9f), skirt.getX(), skirt.getBottom(), false));
    g.fillEllipse (skirt);

    g.setColour (juce::Colours::black.withAlpha (0.28f));

    for (int i = 0; i < 40; ++i)
    {
        const auto a = angle + (float) i * juce::MathConstants<float>::twoPi / 40.0f;
        g.drawLine ({ onCircle (centre, radius * 0.80f, a), onCircle (centre, radius * 0.99f, a) }, 1.0f);
    }

    // The pointer runs out across the skirt in cream, so it reads from a distance.
    g.setColour (juce::Colour (0xfff4efe2));
    g.drawLine ({ onCircle (centre, radius * 0.70f, angle), onCircle (centre, radius * 1.0f, angle) },
                large ? 3.0f : 2.4f);

    // Machined aluminium cap.
    const auto capRadius = radius * 0.72f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);

    juce::ColourGradient metal (juce::Colour (0xffe9e8e3), cap.getX() + cap.getWidth() * 0.3f, cap.getY(),
                                juce::Colour (0xff77787a), cap.getRight() - cap.getWidth() * 0.2f, cap.getBottom(),
                                false);
    metal.addColour (0.55, juce::Colour (0xffb4b4b1));
    g.setGradientFill (metal);
    g.fillEllipse (cap);

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    for (auto r : { 0.35f, 0.55f, 0.75f })
        g.drawEllipse (cap.reduced (capRadius * (1.0f - r)), 0.6f);

    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.drawEllipse (cap, 0.8f);

    // The line engraved into the cap, so the setting is visible from above too.
    g.setColour (juce::Colour (0xff2a2b2d));
    g.drawLine ({ onCircle (centre, capRadius * 0.25f, angle), onCircle (centre, capRadius * 0.92f, angle) },
                large ? 2.2f : 1.8f);

    if (! isEnabled())
    {
        g.setColour (plateBottom.withAlpha (0.55f));
        g.fillEllipse (skirt.expanded (1.0f));
    }
}

// ---------------------------------------------------------------------------

Lamp::Lamp (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
            juce::Colour lamp, juce::String legend)
    : juce::Button (legend),
      lampColour (lamp),
      attachment (state, parameterID, *this)
{
    setClickingTogglesState (true);

    if (auto* parameter = state.getParameter (parameterID))
        setTooltip (parameter->getName (64));
}

void Lamp::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto on     = getToggleState() && isEnabled();

    auto capArea = bounds.withHeight (bounds.getHeight() * 0.56f)
                         .withSizeKeepingCentre (juce::jmin (bounds.getWidth(), 30.0f), bounds.getHeight() * 0.56f);

    // The bezel the button sits in.
    g.setColour (recess);
    g.fillRoundedRectangle (capArea.expanded (2.0f), 4.0f);

    if (down)
        capArea = capArea.translated (0.0f, 1.0f);

    if (on)
    {
        // A lit cap glows past its own edge.
        g.setColour (lampColour.withAlpha (0.22f));
        g.fillRoundedRectangle (capArea.expanded (4.0f), 7.0f);

        g.setGradientFill (juce::ColourGradient (lampColour.brighter (0.6f), capArea.getCentreX(), capArea.getY(),
                                                 lampColour.darker (0.2f), capArea.getCentreX(), capArea.getBottom(), false));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4c50), capArea.getCentreX(), capArea.getY(),
                                                 juce::Colour (0xff2c2e31), capArea.getCentreX(), capArea.getBottom(), false));
    }

    g.fillRoundedRectangle (capArea, 3.0f);
    g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.25f : 0.12f));
    g.drawRoundedRectangle (capArea.reduced (0.5f), 3.0f, 0.8f);

    drawTrackedLabel (g, getButtonText(),
                      bounds.withTop (capArea.getBottom() + 3.0f), 8.0f, 1.0f,
                      juce::Justification::centredTop);
}

// ---------------------------------------------------------------------------

ModeSelector::ModeSelector (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID)
    : attachment (*state.getParameter (parameterID),
                  [this] (float value)
                  {
                      selected = juce::roundToInt (value);
                      repaint();
                  })
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterID)))
        for (auto& name : choice->choices)
            names.add (name.toUpperCase());

    attachment.sendInitialUpdate();
}

juce::Rectangle<float> ModeSelector::buttonArea (int index) const
{
    const auto rowHeight = (float) getHeight() / (float) juce::jmax (1, names.size());
    return { 0.0f, rowHeight * (float) index, (float) getWidth(), rowHeight };
}

void ModeSelector::paint (juce::Graphics& g)
{
    for (int i = 0; i < names.size(); ++i)
    {
        const auto row = buttonArea (i).reduced (0.0f, 4.0f);
        const auto cap = row.withWidth (26.0f).withSizeKeepingCentre (26.0f, 16.0f).withX (row.getX());
        const auto on  = i == selected;

        g.setColour (recess);
        g.fillRoundedRectangle (cap.expanded (2.0f), 4.0f);

        if (on)
        {
            g.setColour (compAccent.withAlpha (0.22f));
            g.fillRoundedRectangle (cap.expanded (4.0f), 7.0f);
            g.setGradientFill (juce::ColourGradient (compAccent.brighter (0.6f), cap.getCentreX(), cap.getY(),
                                                     compAccent.darker (0.2f), cap.getCentreX(), cap.getBottom(), false));
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4c50), cap.getCentreX(), cap.getY(),
                                                     juce::Colour (0xff2c2e31), cap.getCentreX(), cap.getBottom(), false));
        }

        g.fillRoundedRectangle (cap, 3.0f);

        drawTrackedLabel (g, names[i], row.withTrimmedLeft (36.0f), 9.0f, 1.2f,
                          juce::Justification::centredLeft, on ? engraving : engravingDim);
    }
}

void ModeSelector::mouseDown (const juce::MouseEvent& event)
{
    for (int i = 0; i < names.size(); ++i)
    {
        if (buttonArea (i).contains (event.position))
        {
            attachment.setValueAsCompleteGesture ((float) i);
            return;
        }
    }
}

// ---------------------------------------------------------------------------

DriveLamp::DriveLamp (juce::Colour c) : colour (c)
{
    setInterceptsMouseClicks (false, false);
}

void DriveLamp::push (float drive)
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

void DriveLamp::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto lens   = bounds.withSizeKeepingCentre (9.0f, 9.0f);

    if (glow > 0.0f)
    {
        juce::ColourGradient halo (colour.withAlpha (0.55f * glow), lens.getCentreX(), lens.getCentreY(),
                                   colour.withAlpha (0.0f), lens.getCentreX() + bounds.getWidth() * 0.5f,
                                   lens.getCentreY(), true);
        g.setGradientFill (halo);
        g.fillEllipse (bounds);
    }

    g.setColour (recess);
    g.fillEllipse (lens.expanded (1.5f));

    g.setColour (colour.darker (1.6f).interpolatedWith (colour.brighter (0.4f), glow));
    g.fillEllipse (lens);

    g.setColour (juce::Colours::white.withAlpha (0.25f + 0.35f * glow));
    g.fillEllipse (lens.withSizeKeepingCentre (3.0f, 3.0f).translated (-1.5f, -1.5f));
}

// ---------------------------------------------------------------------------

VuMeter::VuMeter()
{
    setInterceptsMouseClicks (false, false);
}

void VuMeter::setLegend (juce::String newLegend)
{
    if (newLegend != legend)
    {
        legend = std::move (newLegend);
        repaint();
    }
}

float VuMeter::proportionFor (float reductionDb) noexcept
{
    // Spread like a printed GR scale: the first few dB get most of the arc.
    // Full scale is 16 dB, as on the unit's meter.
    return std::log10 (1.0f + juce::jlimit (0.0f, 16.0f, reductionDb)) / std::log10 (17.0f);
}

void VuMeter::push (float reductionDb, float frameSeconds)
{
    // A damped moving coil: about 300 ms to settle, with the slight overshoot
    // of a real movement. Integrated in small steps so the frame rate does
    // not change the ballistics.
    constexpr int   steps   = 12;
    constexpr float omega   = 2.0f * juce::MathConstants<float>::pi * 2.2f;
    constexpr float damping = 0.78f;

    const auto target = juce::jlimit (0.0f, 17.0f, reductionDb);
    const auto dt = frameSeconds / (float) steps;
    const auto before = shownDb;

    for (int i = 0; i < steps; ++i)
    {
        const auto acceleration = omega * omega * (target - shownDb) - 2.0f * damping * omega * velocity;
        velocity += acceleration * dt;
        shownDb  += velocity * dt;
    }

    shownDb = juce::jlimit (0.0f, 17.0f, shownDb);

    if (std::abs (shownDb - before) > 0.005f)
        repaint();
}

void VuMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Bezel and the window cut into the plate.
    g.setColour (recess);
    g.fillRoundedRectangle (bounds, 6.0f);

    const auto face = bounds.reduced (5.0f);

    // A warm backlit face, brighter in the middle where the lamp sits.
    juce::ColourGradient paper (juce::Colour (0xfff3e2bb), face.getCentreX(), face.getY() + face.getHeight() * 0.55f,
                                juce::Colour (0xffb98f55), face.getX(), face.getY(), true);
    g.setGradientFill (paper);
    g.fillRoundedRectangle (face, 3.5f);

    const auto pivot  = juce::Point<float> (face.getCentreX(), face.getBottom() + face.getHeight() * 0.18f);
    const auto radius = face.getHeight() * 0.86f;
    constexpr float sweep = 0.62f;   // radians either side of upright

    const auto angleFor = [] (float proportion) { return sweep - 2.0f * sweep * proportion; };
    const auto ink = juce::Colour (0xff2b2620);

    // The arc and its scale.
    juce::Path arc;
    arc.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, angleFor (1.0f), angleFor (0.0f), true);
    g.setColour (ink);
    g.strokePath (arc, juce::PathStrokeType (1.2f));

    struct Mark { int db; bool major; };

    for (const auto& [db, major] : { Mark { 0, true }, Mark { 1, true }, Mark { 2, true }, Mark { 3, true },
                                     Mark { 4, false }, Mark { 5, true }, Mark { 6, false }, Mark { 8, false },
                                     Mark { 10, true }, Mark { 12, false }, Mark { 16, true } })
    {
        const auto a = angleFor (proportionFor ((float) db));

        g.drawLine ({ onCircle (pivot, radius, a), onCircle (pivot, radius + (major ? 7.0f : 4.0f), a) }, major ? 1.3f : 0.9f);

        if (major)
        {
            const auto at = onCircle (pivot, radius + 15.0f, a);
            g.setFont (plateFont (8.5f));
            g.drawText (juce::String (db), juce::Rectangle<float> (22.0f, 10.0f).withCentre (at),
                        juce::Justification::centred);
        }
    }

    g.setColour (ink.withAlpha (0.8f));
    g.setFont (plateFont (7.5f));
    g.drawText (legend, face.withTrimmedTop (face.getHeight() * 0.56f).withHeight (11.0f), juce::Justification::centred);
    g.setFont (plateFont (7.0f, false));
    g.drawText ("dB", face.withTrimmedTop (face.getHeight() * 0.40f).withHeight (10.0f), juce::Justification::centred);

    // The needle, with its shadow on the face.
    const auto angle = angleFor (proportionFor (shownDb));   // the rest pin stops it at 0
    const auto tip   = onCircle (pivot, radius + 6.0f, angle);
    const auto root  = onCircle (pivot, face.getHeight() * 0.30f, angle);

    g.saveState();
    g.reduceClipRegion (face.toNearestInt());

    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawLine ({ root.translated (2.0f, 2.5f), tip.translated (2.0f, 2.5f) }, 1.6f);

    g.setColour (juce::Colour (0xff1b1714));
    g.drawLine ({ root, tip }, 1.4f);

    // The pivot cover rising out of the bottom of the window.
    g.setColour (juce::Colour (0xff141414));
    g.fillEllipse (juce::Rectangle<float> (face.getWidth() * 0.34f, face.getWidth() * 0.34f)
                       .withCentre ({ pivot.x, face.getBottom() + face.getWidth() * 0.06f }));
    g.restoreState();

    // Glass: a soft highlight across the top.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.22f), face.getX(), face.getY(),
                                             juce::Colours::white.withAlpha (0.0f), face.getX(), face.getCentreY(), false));
    g.fillRoundedRectangle (face.withHeight (face.getHeight() * 0.5f), 3.5f);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRoundedRectangle (face, 3.5f, 1.0f);
}

} // namespace kiln::panel
