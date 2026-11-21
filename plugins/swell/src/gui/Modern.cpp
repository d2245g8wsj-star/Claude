#include "Modern.h"

namespace swell::modern
{

namespace
{
    constexpr float startAngle = juce::MathConstants<float>::pi * 1.25f;   // -135 degrees from the top
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.75f;   // +135 degrees
}

juce::Font sans (float height, bool bold)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font mono (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height, juce::Font::bold));
}

void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, juce::Font font,
               juce::Colour colour, juce::Justification justification, float tracking)
{
    g.setColour (colour);
    g.setFont (font.withExtraKerningFactor (tracking / juce::jmax (1.0f, font.getHeight())));
    g.drawText (text, area, justification, false);
}

void drawSection (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0.0f, 2.0f), 12.0f);
    g.setGradientFill (juce::ColourGradient (colour::section.brighter (0.06f), r.getX(), r.getY(),
                                             colour::section.darker (0.08f), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
}

// ---------------------------------------------------------------------------

ArcKnob::ArcKnob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, Style s)
    : style (std::move (s)),
      attachment (state, parameterID, *this)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setRotaryParameters (startAngle, endAngle, true);
    setMouseDragSensitivity (220);
    setVelocityBasedMode (false);

    if (auto* parameter = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        setTooltip (parameter->getName (64) + " (double click resets)");
    }
}

juce::Rectangle<int> ArcKnob::boundsAt (juce::Point<float> centre, float radius)
{
    return juce::Rectangle<float> (2.0f * radius + 40.0f, 2.0f * radius + 72.0f).withCentre (centre).toNearestInt();
}

void ArcKnob::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto c = bounds.getCentre();
    const auto r = style.radius;
    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto angle = startAngle + proportion * (endAngle - startAngle);
    const auto enabled = isEnabled();

    // Name above, value below.
    drawText (g, style.name, { bounds.getX(), c.y - r - 30.0f, bounds.getWidth(), 12.0f }, sans (8.5f),
              colour::dim, juce::Justification::centred, 1.4f);
    const auto text = style.format ? style.format (getValue()) : getTextFromValue (getValue());
    drawText (g, text, { bounds.getX(), c.y + r + 12.0f, bounds.getWidth(), 14.0f }, mono (10.5f),
              enabled ? colour::ink : colour::dim);

    // The track and the value arc.
    const auto arcRadius = r + 9.0f;
    const juce::PathStrokeType stroke (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colour::track);
    g.strokePath (track, stroke);

    const auto from = style.bipolar ? 0.5f * (startAngle + endAngle) : startAngle;

    if (std::abs (angle - from) > 0.01f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        g.setColour (style.colour.withAlpha (0.25f));
        g.strokePath (value, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (enabled ? style.colour : style.colour.withSaturation (0.2f));
        g.strokePath (value, stroke);
    }

    // The aluminium knob: a soft shadow, the turned face, a bevel.
    const auto knob = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillEllipse (knob.translated (0.0f, 2.5f).expanded (1.5f));

    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe3e6e8), knob.getX(), knob.getY(),
                                             juce::Colour (0xff7c8288), knob.getRight(), knob.getBottom(), false));
    g.fillEllipse (knob);

    const auto face = knob.reduced (r * 0.12f);
    juce::ColourGradient turned (juce::Colour (0xffd9dcde), c.x, c.y,
                                 juce::Colour (0xff9da3a8), face.getRight(), face.getBottom(), true);
    turned.addColour (0.55, juce::Colour (0xffc3c7ca));
    g.setGradientFill (turned);
    g.fillEllipse (face);

    // Fine concentric turning marks.
    for (float ring = 0.18f; ring < 1.0f; ring += 0.09f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f + 0.04f * std::fmod (ring * 7.0f, 1.0f)));
        g.drawEllipse (face.reduced (face.getWidth() * 0.5f * ring), 0.6f);
    }

    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawEllipse (knob, 0.8f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawEllipse (face.reduced (0.5f), 0.7f);

    // The pointer.
    const auto direction = juce::Point<float> (std::sin (angle), -std::cos (angle));
    const auto inner = c + direction * (r * 0.30f);
    const auto outer = c + direction * (r * 0.82f);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.drawLine ({ inner.translated (0.0f, 0.8f), outer.translated (0.0f, 0.8f) }, 3.0f);
    g.setColour (style.colour == colour::ink ? juce::Colour (0xff1c1f22) : style.colour.darker (0.15f));
    g.drawLine ({ inner, outer }, 2.6f);
}

// ---------------------------------------------------------------------------

Pill::Pill (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, juce::Colour c, juce::String label)
    : juce::Button (label),
      colour (c),
      attachment (state, parameterID, *this)
{
    setClickingTogglesState (true);

    if (auto* parameter = state.getParameter (parameterID))
        setTooltip (parameter->getName (64));
}

void Pill::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto on = getToggleState();
    const auto corner = r.getHeight() * 0.5f;

    if (on)
    {
        g.setColour (colour.withAlpha (0.25f));
        g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
        g.setColour (down ? colour.darker (0.2f) : highlighted ? colour.brighter (0.1f) : colour);
        g.fillRoundedRectangle (r, corner);
    }
    else
    {
        g.setColour (down ? juce::Colour (0xff0a0b0c) : juce::Colour (0xff121417));
        g.fillRoundedRectangle (r, corner);
        g.setColour (juce::Colours::white.withAlpha (highlighted ? 0.18f : 0.10f));
        g.drawRoundedRectangle (r, corner, 1.0f);
    }

    drawText (g, getButtonText(), r, sans (juce::jmin (8.0f, r.getHeight() * 0.48f)),
              on ? juce::Colour (0xff16181a) : colour::dim, juce::Justification::centred, 0.6f);
}

// ---------------------------------------------------------------------------

DriveLed::DriveLed (juce::Colour c) : colour (c)
{
    setTooltip ("Drive: how hard the band's curve is working");
}

void DriveLed::push (float drive)
{
    // Fast up, slow down, so a short burst is seen.
    const auto target = juce::jlimit (0.0f, 1.0f, drive);
    const auto next = target > level ? target : level * 0.86f;

    if (std::abs (next - level) > 0.01f)
    {
        level = next;
        repaint();
    }
}

void DriveLed::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const auto r = 3.2f;

    if (level > 0.05f)
    {
        g.setColour (colour.withAlpha (0.35f * level));
        g.fillEllipse (juce::Rectangle<float> (r * 5.0f, r * 5.0f).withCentre (c));
    }

    g.setColour (level > 0.05f ? colour.interpolatedWith (juce::Colours::white, 0.15f * level).withAlpha (0.35f + 0.65f * level)
                               : juce::Colour (0xff2c3034));
    g.fillEllipse (juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c));
}

// ---------------------------------------------------------------------------

BarMeter::BarMeter()
{
    setInterceptsMouseClicks (false, false);
}

void BarMeter::setChannels (juce::StringArray l)
{
    letters = std::move (l);
    levels.assign ((size_t) letters.size(), -100.0f);
    repaint();
}

float BarMeter::position (float db)
{
    // The printed scale: -24, -12, -6, -3 and 0 dB across the bar.
    static constexpr std::array<std::pair<float, float>, 5> marks {
        { { -24.0f, 0.0f }, { -12.0f, 0.4f }, { -6.0f, 0.65f }, { -3.0f, 0.82f }, { 0.0f, 1.0f } } };

    if (db <= marks.front().first)
        return 0.0f;

    for (size_t i = 1; i < marks.size(); ++i)
        if (db <= marks[i].first)
        {
            const auto t = (db - marks[i - 1].first) / (marks[i].first - marks[i - 1].first);
            return marks[i - 1].second + t * (marks[i].second - marks[i - 1].second);
        }

    return 1.0f;
}

void BarMeter::push (const std::vector<float>& peaks)
{
    auto loudest = -100.0f;

    for (size_t i = 0; i < levels.size() && i < peaks.size(); ++i)
    {
        const auto db = juce::Decibels::gainToDecibels (peaks[i], -100.0f);
        // Peak ballistics: instant rise, about 20 dB a second fall at 30 Hz.
        levels[i] = juce::jmax (db, levels[i] - 0.7f);
        loudest = juce::jmax (loudest, db);
    }

    if (loudest >= held || ++heldFor > 60)
    {
        held = loudest;
        heldFor = 0;
    }

    repaint();
}

void BarMeter::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto screen = bounds.withTrimmedBottom (16.0f);

    g.setColour (colour::screen);
    g.fillRoundedRectangle (screen, 6.0f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (screen.reduced (0.5f), 6.0f, 1.0f);

    const auto barX = screen.getX() + 22.0f;
    const auto barW = screen.getWidth() - 74.0f;
    const auto rows = juce::jmax (1, letters.size());
    const auto pitch = (screen.getHeight() - 16.0f) / (float) rows;
    constexpr int segments = 30;
    const auto segment = barW / (float) segments;

    for (int row = 0; row < letters.size(); ++row)
    {
        const auto y = screen.getY() + 8.0f + pitch * (float) row + 0.5f * (pitch - 10.0f);
        drawText (g, letters[row], { screen.getX() + 4.0f, y - 1.0f, 14.0f, 12.0f }, sans (8.0f), colour::dim);

        const auto lit = position (levels[(size_t) row]);

        for (int s = 0; s < segments; ++s)
        {
            const auto at = ((float) s + 0.5f) / (float) segments;
            const auto hue = at > 0.82f ? juce::Colour (0xffff4d3d) : at > 0.65f ? colour::mid : juce::Colour (0xff4fd08a);
            const auto on = at <= lit;
            g.setColour (on ? hue : hue.withAlpha (0.12f));
            g.fillRoundedRectangle (barX + segment * (float) s + 0.6f, y, segment - 1.2f, 10.0f, 1.2f);
        }
    }

    const auto readout = held <= -60.0f ? juce::String ("-inf") : (held > 0.05f ? "+" : "") + juce::String (held, 1);
    drawText (g, readout, { screen.getRight() - 50.0f, screen.getCentreY() - 7.0f, 42.0f, 14.0f }, mono (9.5f),
              held > -0.05f ? juce::Colour (0xffff4d3d) : colour::ink, juce::Justification::centredRight);

    for (auto [text, at] : { std::pair { "-24", 0.0f }, { "-12", 0.4f }, { "-6", 0.65f }, { "-3", 0.82f }, { "0", 1.0f } })
        drawText (g, text, { barX + barW * at - 14.0f, screen.getBottom() + 3.0f, 28.0f, 10.0f }, sans (7.0f),
                  colour::dim);
}

// ---------------------------------------------------------------------------

Segments::Segments (juce::StringArray l) : labels (std::move (l))
{
    setRepaintsOnMouseActivity (true);
}

void Segments::setSelected (int index)
{
    if (index != selected)
    {
        selected = index;
        repaint();
    }
}

void Segments::mouseDown (const juce::MouseEvent& e)
{
    const auto index = juce::jlimit (0, labels.size() - 1, (int) (e.position.x / (float) getWidth() * (float) labels.size()));

    if (onChange)
        onChange (index);
}

void Segments::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = r.getHeight() * 0.5f;

    g.setColour (juce::Colour (0xff101214));
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r, corner, 1.0f);

    const auto width = r.getWidth() / (float) labels.size();

    for (int i = 0; i < labels.size(); ++i)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + width * (float) i, r.getY(), width, r.getHeight());

        if (i == selected)
        {
            g.setColour (colour::ink);
            g.fillRoundedRectangle (cell.reduced (2.0f), (cell.getHeight() - 4.0f) * 0.5f);
        }

        drawText (g, labels[i], cell, sans (7.5f), i == selected ? juce::Colour (0xff16181a) : colour::dim,
                  juce::Justification::centred, 0.6f);
    }
}

} // namespace swell::modern
