#include "Panel.h"
#include "BinaryData.h"

namespace swell::panel
{

juce::Font plateFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Image renderPlate (int width, int height)
{
    juce::Image image (juce::Image::RGB, width, height, false);
    juce::Graphics g (image);

    const auto w = (float) width, h = (float) height;

    // Warm cream, a shade lighter at the top where the light falls.
    g.setGradientFill ({ juce::Colour (0xfff8ecd3), 0.0f, 0.0f,
                         juce::Colour (0xffecdcbd), 0.0f, h, false });
    g.fillAll();

    juce::Random random (0x5e11);

    // Low frequency mottling: anodised metal is never one flat colour.
    for (int i = 0; i < 26; ++i)
    {
        const auto centre = juce::Point<float> (random.nextFloat() * w, random.nextFloat() * h);
        const auto radius = (0.10f + 0.25f * random.nextFloat()) * w;
        const auto light  = random.nextBool();
        const auto tone   = light ? juce::Colours::white.withAlpha (0.16f) : ink.withAlpha (0.045f);

        g.setGradientFill ({ tone, centre.x, centre.y,
                             tone.withAlpha (0.0f), centre.x + radius, centre.y, true });
        g.fillRect (image.getBounds());
    }

    // Brushing: long faint horizontal strokes of light and shade.
    for (int i = 0; i < width * 3; ++i)
    {
        const auto y      = random.nextFloat() * h;
        const auto x      = random.nextFloat() * w - 100.0f;
        const auto length = 40.0f + random.nextFloat() * w * 0.35f;
        const auto light  = random.nextFloat() < 0.6f;

        g.setColour (light ? juce::Colours::white.withAlpha (0.05f + 0.10f * random.nextFloat())
                           : ink.withAlpha (0.012f + 0.03f * random.nextFloat()));
        g.drawHorizontalLine ((int) y, x, x + length);
    }

    return image;
}

void drawGroupWell (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    juce::Path well;
    well.addRoundedRectangle (area, corner);

    // Lit lower lip first, the dark edge on top of it: that order is what makes
    // the eye read a cut instead of a drawn outline.
    g.setColour (juce::Colours::white.withAlpha (0.50f));
    g.strokePath (well, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (0.0f, 1.4f));

    g.setGradientFill ({ ink.withAlpha (0.085f), area.getX(), area.getY(),
                         ink.withAlpha (0.025f), area.getX(), area.getBottom(), false });
    g.fillPath (well);

    {
        juce::Graphics::ScopedSaveState clipped (g);
        g.reduceClipRegion (well);

        for (const auto& pass : { std::pair<float, float> { 10.0f, 0.035f },
                                  std::pair<float, float> {  5.0f, 0.050f },
                                  std::pair<float, float> {  2.0f, 0.075f } })
        {
            g.setColour (ink.withAlpha (pass.second));
            g.strokePath (well, juce::PathStrokeType (pass.first));
        }

        g.setGradientFill ({ ink.withAlpha (0.10f), area.getX(), area.getY(),
                             juce::Colours::transparentBlack, area.getX(), area.getY() + 7.0f, false });
        g.fillRect (area);

        g.setGradientFill ({ juce::Colours::white.withAlpha (0.34f), area.getX(), area.getBottom(),
                             juce::Colours::transparentBlack, area.getX(), area.getBottom() - 5.0f, false });
        g.fillRect (area);
    }

    g.setColour (juce::Colours::white.withAlpha (0.22f));
    g.strokePath (well, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, -1.0f));

    g.setColour (ink.withAlpha (0.32f));
    g.strokePath (well, juce::PathStrokeType (1.0f));
}

void drawTrackedLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       float height, float tracking, juce::Justification justification, float alpha)
{
    const auto font = plateFont (height);
    g.setFont (font);

    const auto count = text.length();

    juce::Array<float> widths;
    auto total = count > 1 ? tracking * (float) (count - 1) : 0.0f;

    for (int i = 0; i < count; ++i)
    {
        const auto width = juce::GlyphArrangement::getStringWidth (font, text.substring (i, i + 1));
        widths.add (width);
        total += width;
    }

    auto x = area.getX();

    if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - total;
    else if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;

    for (int i = 0; i < count; ++i)
    {
        const auto glyph = text.substring (i, i + 1);
        const juce::Rectangle<float> cell (x, area.getY(), widths[i] + 1.0f, area.getHeight());

        g.setColour (juce::Colours::white.withAlpha (0.55f * alpha));
        g.drawText (glyph, cell.translated (0.0f, 1.0f), juce::Justification::centredLeft, false);

        g.setColour (ink.withAlpha (alpha));
        g.drawText (glyph, cell, juce::Justification::centredLeft, false);

        x += widths[i] + tracking;
    }
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float slotAngle)
{
    const auto head = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    g.setColour (ink.withAlpha (0.25f));
    g.fillEllipse (head.translated (0.0f, 1.5f).expanded (1.0f));

    g.setGradientFill ({ juce::Colour (0xfffbfaf6), head.getX(), head.getY(),
                         juce::Colour (0xff8f8a80), head.getRight(), head.getBottom(), false });
    g.fillEllipse (head);

    g.setColour (ink.withAlpha (0.35f));
    g.drawEllipse (head, 0.8f);

    const auto offset = juce::Point<float> (std::cos (slotAngle), std::sin (slotAngle)) * (radius * 0.78f);
    g.setColour (juce::Colour (0xff2b2823));
    g.drawLine ({ centre - offset, centre + offset }, radius * 0.32f);
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawLine ({ centre - offset + juce::Point<float> (0.0f, 1.2f), centre + offset + juce::Point<float> (0.0f, 1.2f) }, 0.8f);
}

// ---------------------------------------------------------------------------

Filmstrip::Filmstrip (const void* data, int size)
{
    image = juce::ImageCache::getFromMemory (data, size);

    if (image.isValid() && image.getWidth() > 0)
    {
        numFrames   = juce::jmax (1, image.getHeight() / image.getWidth());
        frameHeight = image.getHeight() / numFrames;
    }
}

void Filmstrip::drawProportion (juce::Graphics& g, juce::Rectangle<float> area, float proportion) const
{
    if (! image.isValid())
        return;

    const auto index  = juce::roundToInt (juce::jlimit (0.0f, 1.0f, proportion) * (float) (numFrames - 1));
    const auto target = area.toNearestInt();

    g.drawImage (image, target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                 0, index * frameHeight, image.getWidth(), frameHeight, false);
}

// ---------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, const Filmstrip& strip)
    : filmstrip (strip), attachment (state, parameterID, *this)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (200);
    setWantsKeyboardFocus (false);

    if (auto* p = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, state.getParameterRange (parameterID).convertFrom0to1 (p->getDefaultValue()));
        setTooltip (p->getName (64));
    }
}

void Knob::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    filmstrip.drawProportion (g, area, (float) valueToProportionOfLength (getValue()));

    // The legend is printed around the knob, so the figure only shows in use.
    if (isMouseOverOrDragging() && isEnabled())
    {
        const auto readout = juce::Rectangle<float> (area.getWidth() + 20.0f, 15.0f)
                                 .withCentre ({ area.getCentreX(), area.getBottom() - 4.0f });

        g.setColour (cream.withAlpha (0.94f));
        g.fillRoundedRectangle (readout.reduced (8.0f, 1.0f), 2.5f);

        g.setColour (ink);
        g.setFont (plateFont (10.0f));
        g.drawText (getTextFromValue (getValue()), readout, juce::Justification::centred, false);
    }
}

// ---------------------------------------------------------------------------

namespace
{
    void drawFace (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool hover)
    {
        g.setColour (ink.withAlpha (0.20f));
        g.fillRoundedRectangle (area.translated (0.0f, 1.0f), 2.5f);

        g.setGradientFill ({ on ? amber.brighter (0.12f) : cream.brighter (0.05f), 0.0f, area.getY(),
                             on ? amber.darker (0.16f)   : cream.darker (0.12f),   0.0f, area.getBottom(), false });
        g.fillRoundedRectangle (area, 2.5f);

        if (hover && ! on)
        {
            g.setColour (amber.withAlpha (0.14f));
            g.fillRoundedRectangle (area, 2.5f);
        }

        g.setColour (ink.withAlpha (0.45f));
        g.drawRoundedRectangle (area.reduced (0.5f), 2.5f, 1.0f);
    }

    void drawLegend (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, bool on)
    {
        g.setColour (on ? cream : ink.withAlpha (0.85f));
        g.setFont (plateFont (9.5f));
        g.drawText (text, area, juce::Justification::centred, false);
    }
}

Switch::Switch (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, juce::String text)
    : parameter (*state.getParameter (parameterID)),
      caption (std::move (text)),
      attachment (parameter, [this] (float v) { on = v >= 0.5f; repaint(); }, nullptr)
{
    setTooltip (parameter.getName (64));
    attachment.sendInitialUpdate();
}

void Switch::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void Switch::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    drawFace (g, area, on, hover);
    drawLegend (g, caption, area, on);
}

// ---------------------------------------------------------------------------

Bank::Bank (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, juce::String text)
    : parameter (*state.getParameter (parameterID)),
      caption (std::move (text)),
      attachment (parameter, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&parameter))
        for (const auto& name : choice->choices)
            choices.add (name.toUpperCase());

    setTooltip (parameter.getName (64));
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> Bank::cell (int i) const
{
    const auto area  = getLocalBounds().toFloat().withTrimmedTop (16.0f);
    const auto width = area.getWidth() / (float) juce::jmax (1, choices.size());

    return { area.getX() + width * (float) i, area.getY(), width, area.getHeight() };
}

void Bank::mouseDown (const juce::MouseEvent& event)
{
    for (int i = 0; i < choices.size(); ++i)
    {
        if (cell (i).contains (event.position) && i != index)
        {
            attachment.setValueAsCompleteGesture ((float) i);
            return;
        }
    }
}

void Bank::paint (juce::Graphics& g)
{
    drawTrackedLabel (g, caption, getLocalBounds().toFloat().withHeight (12.0f), 8.5f, 2.0f,
                      juce::Justification::centred, 0.7f);

    for (int i = 0; i < choices.size(); ++i)
    {
        const auto area = cell (i).reduced (1.0f, 0.0f);
        drawFace (g, area, i == index, false);
        drawLegend (g, choices[i], area, i == index);
    }
}

// ---------------------------------------------------------------------------

LedImages::LedImages()
    : off   (juce::ImageCache::getFromMemory (BinaryData::led_off_png,   BinaryData::led_off_pngSize)),
      green (juce::ImageCache::getFromMemory (BinaryData::led_green_png, BinaryData::led_green_pngSize)),
      amber (juce::ImageCache::getFromMemory (BinaryData::led_amber_png, BinaryData::led_amber_pngSize)),
      red   (juce::ImageCache::getFromMemory (BinaryData::led_red_png,   BinaryData::led_red_pngSize))
{
}

LedMeter::LedMeter (const LedImages& images) : leds (images)
{
    setInterceptsMouseClicks (false, false);
}

void LedMeter::push (float peak)
{
    const auto db = juce::Decibels::gainToDecibels (peak, -100.0f);

    level = db > level ? db : juce::jmax (-100.0f, level - 1.8f);

    if (db >= hold)
    {
        hold = db;
        holdTicks = 34;
    }
    else if (--holdTicks <= 0)
    {
        hold = juce::jmax (-100.0f, hold - 0.9f);
    }

    repaint();
}

void LedMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (recess);
    g.fillRoundedRectangle (bounds, 4.0f);

    if (! leds.off.isValid())
        return;

    const auto aspect  = (float) leds.off.getHeight() / (float) leds.off.getWidth();
    const auto inner   = bounds.reduced (2.0f, 3.0f);
    const auto segment = inner.getWidth() * aspect;
    const auto count   = juce::jmax (4, (int) std::floor (inner.getHeight() / segment));
    const auto spacing = inner.getHeight() / (float) count;

    constexpr float topDb    =   6.0f;
    constexpr float bottomDb = -42.0f;

    for (int i = 0; i < count; ++i)
    {
        const auto db   = juce::jmap ((float) i / (float) (count - 1), bottomDb, topDb);
        const auto cell = juce::Rectangle<float> (inner.getX(), inner.getBottom() - (float) (i + 1) * spacing,
                                                  inner.getWidth(), spacing).reduced (0.0f, spacing * 0.08f);

        const auto lit     = level >= db;
        const auto holding = ! lit && hold >= db && hold < db + (topDb - bottomDb) / (float) count;

        const juce::Image* which = &leds.off;

        if (lit || holding)
            which = db >= 0.0f ? &leds.red : (db >= -9.0f ? &leds.amber : &leds.green);

        if (holding)
            g.setOpacity (0.65f);

        g.drawImage (*which, cell, juce::RectanglePlacement::stretchToFit, false);
        g.setOpacity (1.0f);
    }
}

// ---------------------------------------------------------------------------

DriveLamp::DriveLamp (const LedImages& images) : leds (images)
{
    setInterceptsMouseClicks (false, false);
    setTooltip ("Saturation");
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

    g.setColour (recess);
    g.fillRoundedRectangle (bounds, 2.5f);

    const auto cell = bounds.reduced (2.0f);
    g.drawImage (leds.off, cell, juce::RectanglePlacement::stretchToFit, false);

    if (glow > 0.0f)
    {
        g.setOpacity (glow);
        g.drawImage (leds.amber, cell, juce::RectanglePlacement::stretchToFit, false);
        g.setOpacity (1.0f);
    }
}

// ---------------------------------------------------------------------------

void Readout::setValue (const juce::String& text)
{
    if (text != value)
    {
        value = text;
        repaint();
    }
}

void Readout::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setColour (recess);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (amber.brighter (0.25f));
    g.setFont (plateFont (12.5f));
    g.drawText (value, bounds, juce::Justification::centred, false);
}

} // namespace swell::panel
