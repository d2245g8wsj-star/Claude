#include "Hardware.h"
#include "../PluginProcessor.h"

namespace crest::hw
{

namespace
{
    constexpr float knobStart = juce::MathConstants<float>::pi * 1.25f;
    constexpr float knobEnd   = juce::MathConstants<float>::pi * 2.75f;
    constexpr int   refreshHz = 30;
}

juce::Font panelFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Image renderBrushedPanel (int width, int height, juce::Colour base, int seed)
{
    juce::Image image (juce::Image::ARGB, juce::jmax (1, width), juce::jmax (1, height), false);
    juce::Graphics g (image);

    // The light in the room comes from above, so the panel is brighter at the
    // top and falls away towards the bottom edge.
    juce::ColourGradient body (base.brighter (0.10f), 0.0f, 0.0f,
                               base.darker (0.42f), 0.0f, (float) height, false);
    body.addColour (0.42, base);
    g.setGradientFill (body);
    g.fillRect (0, 0, width, height);

    // Brushing: fine horizontal streaks, generated once and then left alone.
    // Regenerating them per frame would read as noise instead of metal.
    juce::Random random (seed);

    for (int y = 0; y < height; ++y)
    {
        const auto alpha = random.nextFloat() * 0.055f;

        g.setColour ((random.nextBool() ? juce::Colours::white : juce::Colours::black)
                         .withAlpha (alpha));
        g.drawHorizontalLine (y, 0.0f, (float) width);
    }

    return image;
}

void drawEngraved (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                   juce::Justification justification, float fontHeight, float tracking,
                   juce::Colour colour)
{
    if (text.isEmpty())
        return;

    const auto font = panelFont (fontHeight);
    g.setFont (font);

    float total = 0.0f;
    juce::Array<float> widths;

    for (auto character : text)
    {
        const auto w = juce::GlyphArrangement::getStringWidth (font, juce::String::charToString (character));
        widths.add (w);
        total += w + tracking;
    }

    total -= tracking;

    auto x = area.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - total;

    const auto y = area.getCentreY() - fontHeight * 0.5f;

    // A milled channel is dark at the bottom and catches light on its lower lip.
    for (int pass = 0; pass < 2; ++pass)
    {
        auto cursor = x;
        int index = 0;

        g.setColour (pass == 0 ? juce::Colours::black.withAlpha (0.55f) : colour);

        const auto offset = pass == 0 ? -0.7f : 0.0f;

        for (auto character : text)
        {
            g.drawText (juce::String::charToString (character),
                        juce::Rectangle<float> (cursor, y + offset, widths[index] + 2.0f, fontHeight),
                        juce::Justification::centredLeft, false);

            cursor += widths[index] + tracking;
            ++index;
        }
    }
}

void drawScrew (juce::Graphics& g, juce::Point<float> centre, float radius, float angle)
{
    const auto bounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (bounds.translated (0.0f, 1.0f).expanded (0.6f));

    juce::ColourGradient head (juce::Colour (0xff8e8b85), bounds.getX(), bounds.getY(),
                               juce::Colour (0xff3a3a38), bounds.getRight(), bounds.getBottom(), false);
    g.setGradientFill (head);
    g.fillEllipse (bounds);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (bounds.reduced (0.5f), 1.0f);

    juce::Path slot;
    slot.addRoundedRectangle (-radius * 0.72f, -radius * 0.13f, radius * 1.44f, radius * 0.26f, 0.5f);
    slot.applyTransform (juce::AffineTransform::rotation (angle).translated (centre));

    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillPath (slot);

    g.setColour (juce::Colours::white.withAlpha (0.18f));
    g.fillPath (slot, juce::AffineTransform::translation (0.0f, 0.7f));
}

void drawRecess (juce::Graphics& g, juce::Rectangle<float> area, float corner)
{
    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawRoundedRectangle (area.translated (0.0f, 1.0f), corner, 1.2f);

    g.setColour (juce::Colour (0xff141618));
    g.fillRoundedRectangle (area, corner);

    juce::ColourGradient inner (juce::Colours::black.withAlpha (0.55f), 0.0f, area.getY(),
                                juce::Colours::transparentBlack, 0.0f, area.getY() + 10.0f, false);
    g.setGradientFill (inner);
    g.fillRoundedRectangle (area, corner);

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.drawRoundedRectangle (area.reduced (0.5f), corner, 1.0f);
}

void drawRaised (juce::Graphics& g, juce::Rectangle<float> area, float corner, juce::Colour face)
{
    g.setColour (juce::Colours::black.withAlpha (0.40f));
    g.fillRoundedRectangle (area.translated (0.0f, 1.5f), corner);

    juce::ColourGradient gradient (face.brighter (0.16f), 0.0f, area.getY(),
                                   face.darker (0.18f), 0.0f, area.getBottom(), false);
    g.setGradientFill (gradient);
    g.fillRoundedRectangle (area, corner);

    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.drawRoundedRectangle (area.reduced (0.5f).withTrimmedBottom (area.getHeight() * 0.5f), corner, 1.0f);
}

// ---------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
            const juce::String& caption, bool bipolar)
    : captionText (caption), isBipolar (bipolar),
      attachment (state, parameterID, *this)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (knobStart, knobEnd, true);
    setMouseDragSensitivity (200);
    setWantsKeyboardFocus (false);

    if (auto* p = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, state.getParameterRange (parameterID)
                                             .convertFrom0to1 (p->getDefaultValue()));
        setTooltip (p->getName (64));
    }
}

void Knob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    auto captionArea = bounds.removeFromTop (14.0f);
    auto valueArea   = bounds.removeFromBottom (14.0f);

    drawEngraved (g, captionText.toUpperCase(), captionArea,
                  juce::Justification::horizontallyCentred, 10.0f, 1.6f);

    const auto dial   = juce::Rectangle<float> (diameter, diameter).withCentre (bounds.getCentre());
    const auto centre = dial.getCentre();
    const auto radius = diameter * 0.5f;

    const auto proportion = (float) valueToProportionOfLength (getValue());
    const auto valueAngle = knobStart + proportion * (knobEnd - knobStart);
    const auto anchor     = isBipolar ? (knobStart + knobEnd) * 0.5f : knobStart;

    // ---- engraved scale ring, milled into the panel around the knob
    const auto scaleRadius = radius + 12.0f;

    {
        juce::Path channel;
        channel.addCentredArc (centre.x, centre.y, scaleRadius - 4.0f, scaleRadius - 4.0f, 0.0f,
                               knobStart, knobEnd, true);

        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.strokePath (channel, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.strokePath (channel, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (0.0f, 1.0f));
    }

    for (int i = 0; i <= 20; ++i)
    {
        const auto t     = (float) i / 20.0f;
        const auto angle = knobStart + t * (knobEnd - knobStart);
        const auto major = (i % 5) == 0;
        const auto inner = scaleRadius - (major ? 7.0f : 3.5f);

        const juce::Line<float> tick (centre.getPointOnCircumference (inner, angle),
                                      centre.getPointOnCircumference (scaleRadius, angle));

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawLine ({ tick.getStart().translated (0.0f, 1.0f),
                      tick.getEnd().translated (0.0f, 1.0f) }, major ? 1.6f : 1.0f);
        g.setColour (engraved.withAlpha (major ? 0.95f : 0.60f));
        g.drawLine (tick, major ? 1.8f : 1.1f);
    }

    // ---- lit travel: the amber arc reads as a lamp behind the scale
    if (std::abs (valueAngle - anchor) > 0.002f)
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, scaleRadius - 9.5f, scaleRadius - 9.5f, 0.0f,
                           juce::jmin (anchor, valueAngle), juce::jmax (anchor, valueAngle), true);

        g.setColour (lampAmber.withAlpha (0.30f));
        g.strokePath (arc, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour (lampAmber.withAlpha (0.95f));
        g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // ---- the knob itself
    juce::Path body;
    body.addEllipse (dial);

    juce::DropShadow (juce::Colours::black.withAlpha (0.75f), 9, { 0, 4 }).drawForPath (g, body);

    // Chrome collar
    // Turned metal does not fade evenly from light to dark: it has a bright
    // band where it faces the light, a fast fall into shadow, and a second
    // weaker highlight low down where the room bounces back into it.
    juce::ColourGradient collar (chrome.brighter (0.95f), dial.getX(), dial.getY(),
                                 chrome.darker (0.86f), dial.getRight(), dial.getBottom(), false);
    collar.addColour (0.18, chrome.brighter (0.55f));
    collar.addColour (0.34, chrome.darker (0.30f));
    collar.addColour (0.55, chrome.darker (0.72f));
    collar.addColour (0.78, chrome.darker (0.10f));
    g.setGradientFill (collar);
    g.fillEllipse (dial);

    // Contact shadow against the panel, and a lit lower lip.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawEllipse (dial.reduced (0.5f), 1.0f);

    juce::Path lip;
    lip.addCentredArc (centre.x, centre.y, radius - 0.8f, radius - 0.8f, 0.0f, 0.7f, 2.5f, true);
    g.setColour (juce::Colours::white.withAlpha (0.20f));
    g.strokePath (lip, juce::PathStrokeType (1.4f));

    // Machined body
    const auto face = dial.reduced (radius * 0.075f);

    juce::ColourGradient machined (knobBody.brighter (0.42f),
                                   face.getCentreX() - face.getWidth() * 0.3f,
                                   face.getY() - face.getHeight() * 0.15f,
                                   juce::Colours::black,
                                   face.getCentreX() + face.getWidth() * 0.35f,
                                   face.getBottom() + face.getHeight() * 0.1f, true);
    g.setGradientFill (machined);
    g.fillEllipse (face);

    // Knurled rim: alternating flats catching the light
    const auto knurlOuter = face.getWidth() * 0.5f;
    const auto knurlInner = knurlOuter - juce::jmax (4.0f, diameter * 0.085f);

    const auto flutes = juce::jlimit (28, 60, juce::roundToInt (diameter * 0.5f));

    for (int i = 0; i < flutes; ++i)
    {
        const auto angle = juce::MathConstants<float>::twoPi * (float) i / (float) flutes;

        // Each flute is a tiny cylinder: lit on the side facing the light,
        // shadowed on the other. The light is up and to the left.
        const auto lit = 0.5f + 0.5f * std::cos (angle + 2.2f);

        const juce::Line<float> flute { centre.getPointOnCircumference (knurlInner, angle),
                                        centre.getPointOnCircumference (knurlOuter, angle) };

        g.setColour (juce::Colours::white.withAlpha (0.30f * lit * lit));
        g.drawLine (flute, juce::jmax (1.0f, diameter * 0.020f));

        const auto shadowAngle = angle + juce::MathConstants<float>::twoPi / (float) flutes * 0.5f;

        g.setColour (juce::Colours::black.withAlpha (0.55f * (1.0f - lit * 0.7f)));
        g.drawLine ({ centre.getPointOnCircumference (knurlInner, shadowAngle),
                      centre.getPointOnCircumference (knurlOuter, shadowAngle) },
                    juce::jmax (1.0f, diameter * 0.018f));
    }

    // Where the machined face drops away from the collar.
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawEllipse (face.reduced (0.5f), 1.4f);

    // Specular crescent along the top left
    juce::Path gloss;
    gloss.addCentredArc (centre.x, centre.y, knurlInner * 0.86f, knurlInner * 0.86f, 0.0f,
                         -2.5f, -0.7f, true);
    g.setColour (juce::Colours::white.withAlpha (0.26f));
    g.strokePath (gloss, juce::PathStrokeType (juce::jmax (1.5f, diameter * 0.05f),
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Cap and pointer
    const auto capRadius = knurlInner * 0.72f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);

    juce::ColourGradient capFill (knobBody.brighter (0.16f), cap.getX(), cap.getY(),
                                  knobBody.darker (0.5f), cap.getRight(), cap.getBottom(), false);
    g.setGradientFill (capFill);
    g.fillEllipse (cap);

    juce::Path pointer;
    const auto pointerWidth = juce::jmax (2.6f, diameter * 0.040f);
    pointer.addRoundedRectangle (-pointerWidth * 0.5f, -knurlOuter + 1.5f,
                                 pointerWidth, knurlOuter * 0.58f, pointerWidth * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (valueAngle).translated (centre));

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillPath (pointer, juce::AffineTransform::translation (0.0f, 1.2f));
    g.setColour (juce::Colour (0xfff4efe2));
    g.fillPath (pointer);

    // ---- value
    drawEngraved (g, getTextFromValue (getValue()), valueArea,
                  juce::Justification::horizontallyCentred, 11.0f, 0.4f,
                  isMouseOverOrDragging() ? lampAmber : engraved.withAlpha (0.80f));
}

// ---------------------------------------------------------------------------

RotarySwitch::RotarySwitch (juce::AudioProcessorValueTreeState& state,
                            const juce::String& parameterID, const juce::String& caption)
    : parameter (*state.getParameter (parameterID)),
      captionText (caption),
      attachment (parameter,
                  [this] (float newValue) { index = juce::roundToInt (newValue); repaint(); },
                  nullptr)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&parameter))
        choices = choice->choices;

    setTooltip (parameter.getName (64));
    attachment.sendInitialUpdate();
}

float RotarySwitch::angleForIndex (int i) const
{
    const auto count = juce::jmax (1, choices.size() - 1);
    return knobStart + (knobEnd - knobStart) * (float) i / (float) count;
}

void RotarySwitch::selectFromAngle (juce::Point<float> position)
{
    const auto centre = getLocalBounds().toFloat().withTrimmedTop (16.0f)
                            .withTrimmedBottom (16.0f).getCentre();

    auto angle = std::atan2 (position.x - centre.x, centre.y - position.y);

    if (angle < 0.0f)
        angle += juce::MathConstants<float>::twoPi;

    if (angle < knobStart - juce::MathConstants<float>::pi)
        angle += juce::MathConstants<float>::twoPi;

    const auto proportion = juce::jlimit (0.0f, 1.0f,
                                          (angle - knobStart) / (knobEnd - knobStart));

    const auto wanted = juce::roundToInt (proportion * (float) juce::jmax (1, choices.size() - 1));

    if (wanted != index)
        attachment.setValueAsCompleteGesture ((float) wanted);
}

void RotarySwitch::mouseDown (const juce::MouseEvent& event) { selectFromAngle (event.position); }
void RotarySwitch::mouseDrag (const juce::MouseEvent& event) { selectFromAngle (event.position); }

void RotarySwitch::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const auto step = wheel.deltaY > 0.0f ? 1 : -1;
    const auto wanted = juce::jlimit (0, choices.size() - 1, index + step);

    if (wanted != index)
        attachment.setValueAsCompleteGesture ((float) wanted);
}

void RotarySwitch::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    drawEngraved (g, captionText, bounds.removeFromTop (14.0f),
                  juce::Justification::horizontallyCentred, 10.0f, 1.6f);

    bounds.removeFromBottom (4.0f);

    const auto diameter = juce::jmin (bounds.getWidth() - 62.0f, bounds.getHeight() - 26.0f);
    const auto dial     = juce::Rectangle<float> (diameter, diameter).withCentre (bounds.getCentre());
    const auto centre   = dial.getCentre();
    const auto radius   = diameter * 0.5f;

    // Position legends around the switch, lit at the selected one.
    for (int i = 0; i < choices.size(); ++i)
    {
        const auto angle = angleForIndex (i);
        const auto point = centre.getPointOnCircumference (radius + 20.0f, angle);
        const auto lit   = i == index;

        const juce::Line<float> tick (centre.getPointOnCircumference (radius + 4.0f, angle),
                                      centre.getPointOnCircumference (radius + 9.0f, angle));

        g.setColour (lit ? lampAmber : engraved.withAlpha (0.5f));
        g.drawLine (tick, lit ? 2.2f : 1.2f);

        drawEngraved (g, choices[i],
                      juce::Rectangle<float> (46.0f, 12.0f).withCentre (point),
                      juce::Justification::horizontallyCentred, 9.5f, 1.0f,
                      lit ? lampAmber : engravedDim);
    }

    juce::Path body;
    body.addEllipse (dial);
    juce::DropShadow (juce::Colours::black.withAlpha (0.8f), 10, { 0, 4 }).drawForPath (g, body);

    juce::ColourGradient collar (chrome.brighter (0.45f), dial.getX(), dial.getY(),
                                 chrome.darker (0.7f), dial.getRight(), dial.getBottom(), false);
    g.setGradientFill (collar);
    g.fillEllipse (dial);

    // Chicken head pointer: the classic shape for a detented switch.
    const auto face = dial.reduced (radius * 0.14f);

    juce::Path head;
    head.addEllipse (face);
    head.addTriangle (centre.x - face.getWidth() * 0.20f, centre.y,
                      centre.x + face.getWidth() * 0.20f, centre.y,
                      centre.x, centre.y - radius * 1.30f);

    juce::ColourGradient headFill (knobBody.brighter (0.34f), face.getX(), face.getY(),
                                   knobBody.darker (0.55f), face.getRight(), face.getBottom(), false);

    g.setGradientFill (headFill);
    g.fillPath (head, juce::AffineTransform::rotation (angleForIndex (index), centre.x, centre.y));

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.strokePath (head, juce::PathStrokeType (1.2f),
                  juce::AffineTransform::rotation (angleForIndex (index), centre.x, centre.y));

    // Cream stripe down the pointer, which is what you actually read.
    juce::Path stripe;
    stripe.addRoundedRectangle (-1.6f, -radius * 1.22f, 3.2f, radius * 0.78f, 1.6f);

    g.setColour (juce::Colour (0xfff4efe2));
    g.fillPath (stripe, juce::AffineTransform::rotation (angleForIndex (index))
                            .translated (centre.x, centre.y));

    g.setColour (knobBody.darker (0.3f));
    g.fillEllipse (juce::Rectangle<float> (radius * 0.30f, radius * 0.30f).withCentre (centre));
}

// ---------------------------------------------------------------------------

LampButton::LampButton (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                        const juce::String& caption, juce::Colour lampColour)
    : parameter (*state.getParameter (parameterID)),
      captionText (caption), lamp (lampColour),
      attachment (parameter, [this] (float v) { on = v >= 0.5f; repaint(); }, nullptr)
{
    setTooltip (parameter.getName (64));
    attachment.sendInitialUpdate();
}

void LampButton::mouseDown (const juce::MouseEvent&)
{
    attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void LampButton::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    drawRecess (g, bounds, 3.0f);

    auto lens = bounds.reduced (2.5f);

    if (on)
    {
        juce::ColourGradient glow (lamp.withAlpha (0.85f), lens.getCentreX(), lens.getCentreY(),
                                   lamp.withAlpha (0.10f), lens.getCentreX(), lens.getBottom(), true);
        g.setGradientFill (glow);
        g.fillRoundedRectangle (lens, 2.0f);

        g.setColour (lamp.withAlpha (0.55f));
        g.drawRoundedRectangle (lens.reduced (0.5f), 2.0f, 1.0f);
    }
    else
    {
        juce::ColourGradient dark (juce::Colour (0xff232629), 0.0f, lens.getY(),
                                   juce::Colour (0xff15181a), 0.0f, lens.getBottom(), false);
        g.setGradientFill (dark);
        g.fillRoundedRectangle (lens, 2.0f);

        if (hover)
        {
            g.setColour (lamp.withAlpha (0.16f));
            g.fillRoundedRectangle (lens, 2.0f);
        }
    }

    // The legend is printed on the lens, so it lights up with it.
    drawEngraved (g, captionText, lens, juce::Justification::horizontallyCentred, 10.0f, 1.5f,
                  on ? juce::Colours::black.withAlpha (0.78f) : engravedDim);

    // Glass highlight across the top of the lens
    juce::ColourGradient sheen (juce::Colours::white.withAlpha (on ? 0.30f : 0.10f), 0.0f, lens.getY(),
                                juce::Colours::transparentWhite, 0.0f, lens.getCentreY(), false);
    g.setGradientFill (sheen);
    g.fillRoundedRectangle (lens.withHeight (lens.getHeight() * 0.5f), 2.0f);
}

// ---------------------------------------------------------------------------

LampBank::LampBank (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                    const juce::String& caption)
    : parameter (*state.getParameter (parameterID)),
      captionText (caption),
      attachment (parameter, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&parameter))
        choices = choice->choices;

    setTooltip (parameter.getName (64));
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> LampBank::cellBounds (int i) const
{
    auto strip = getLocalBounds().toFloat().withTrimmedTop (14.0f);
    const auto width = strip.getWidth() / (float) juce::jmax (1, choices.size());

    return juce::Rectangle<float> (strip.getX() + width * (float) i, strip.getY(),
                                   width, strip.getHeight()).reduced (1.5f, 0.0f);
}

void LampBank::mouseDown (const juce::MouseEvent& event)
{
    for (int i = 0; i < choices.size(); ++i)
    {
        if (cellBounds (i).expanded (1.5f, 0.0f).contains (event.position) && i != index)
        {
            attachment.setValueAsCompleteGesture ((float) i);
            return;
        }
    }
}

void LampBank::paint (juce::Graphics& g)
{
    drawEngraved (g, captionText, getLocalBounds().toFloat().withHeight (13.0f),
                  juce::Justification::horizontallyCentred, 9.5f, 1.8f);

    for (int i = 0; i < choices.size(); ++i)
    {
        const auto cell = cellBounds (i);
        const auto lit  = i == index;

        drawRecess (g, cell, 2.5f);

        auto lens = cell.reduced (2.0f);

        if (lit)
        {
            juce::ColourGradient glow (lampAmber.withAlpha (0.90f), lens.getCentreX(), lens.getY(),
                                       lampAmber.withAlpha (0.35f), lens.getCentreX(), lens.getBottom(), false);
            g.setGradientFill (glow);
            g.fillRoundedRectangle (lens, 1.8f);
        }

        drawEngraved (g, choices[i], lens, juce::Justification::horizontallyCentred, 9.5f, 0.6f,
                      lit ? juce::Colours::black.withAlpha (0.80f) : engravedDim);
    }
}

// ---------------------------------------------------------------------------

namespace
{
    constexpr float ladderTopDb    =   6.0f;
    constexpr float ladderBottomDb = -42.0f;
    constexpr int   ladderSegments =  24;
}

LadderMeter::LadderMeter (MeterSource& source, bool readsOutput, const juce::String& caption)
    : meters (source), output (readsOutput), captionText (caption)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

void LadderMeter::timerCallback()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& channel = channels[ch];

        const auto magnitude = output ? meters.outputPeak[ch].take() : meters.inputPeak[ch].take();
        const auto db = juce::Decibels::gainToDecibels (magnitude, -100.0f);

        channel.level = db > channel.level ? db : juce::jmax (-100.0f, channel.level - 1.8f);

        if (db >= channel.hold)
        {
            channel.hold = db;
            channel.holdTicks = 34;
        }
        else if (--channel.holdTicks <= 0)
        {
            channel.hold = juce::jmax (-100.0f, channel.hold - 0.9f);
        }
    }

    repaint();
}

void LadderMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    drawEngraved (g, captionText, bounds.removeFromTop (14.0f),
                  juce::Justification::horizontallyCentred, 9.5f, 1.8f);

    drawRecess (g, bounds, 3.0f);

    auto inside = bounds.reduced (4.0f, 5.0f);

    const auto segmentHeight = inside.getHeight() / (float) ladderSegments;
    const auto columnWidth   = (inside.getWidth() - 3.0f) * 0.5f;

    for (int ch = 0; ch < 2; ++ch)
    {
        const auto x = inside.getX() + (float) ch * (columnWidth + 3.0f);

        for (int segment = 0; segment < ladderSegments; ++segment)
        {
            const auto db = juce::jmap ((float) segment, 0.0f, (float) (ladderSegments - 1),
                                        ladderBottomDb, ladderTopDb);

            const auto cell = juce::Rectangle<float> (x, inside.getBottom()
                                                             - (float) (segment + 1) * segmentHeight,
                                                      columnWidth, segmentHeight).reduced (0.0f, 0.8f);

            const auto colour = db >= 0.0f ? lampRed : (db >= -9.0f ? lampAmber : lampGreen);
            const auto isLit  = channels[ch].level >= db;
            const auto isHold = std::abs (channels[ch].hold - db) < segmentHeight * 0.0f + 1.2f
                                && channels[ch].hold > ladderBottomDb;

            if (isLit || isHold)
            {
                g.setColour (colour.withAlpha (isHold && ! isLit ? 0.55f : 0.30f));
                g.fillRoundedRectangle (cell.expanded (1.2f), 1.5f);
                g.setColour (colour.withAlpha (isHold && ! isLit ? 0.85f : 1.0f));
                g.fillRoundedRectangle (cell, 1.0f);
            }
            else
            {
                g.setColour (juce::Colours::black.withAlpha (0.55f));
                g.fillRoundedRectangle (cell, 1.0f);
                g.setColour (colour.withAlpha (0.07f));
                g.fillRoundedRectangle (cell, 1.0f);
            }
        }
    }
}

// ---------------------------------------------------------------------------

ClipMeter::ClipMeter (juce::AudioProcessorValueTreeState& state, MeterSource& source,
                      const std::atomic<bool>& sidechainFlag)
    : apvts (state), meters (source), sidechain (sidechainFlag)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

void ClipMeter::timerCallback()
{
    const auto removed = juce::jlimit (0.0f, 0.999f, meters.reduction.take());
    const auto db = -juce::Decibels::gainToDecibels (1.0f - removed, -60.0f);

    reductionDb = db > reductionDb ? db : juce::jmax (0.0f, reductionDb - 0.6f);

    if (db >= holdDb)
    {
        holdDb = db;
        holdTicks = 34;
    }
    else if (--holdTicks <= 0)
    {
        holdDb = juce::jmax (0.0f, holdDb - 0.3f);
    }

    // The sidechain hands low frequency material back, which can then run into
    // the ceiling clamp instead of the shaper. Worth saying out loud.
    const auto tpSafe = apvts.getRawParameterValue (pid::tpSafe)->load() > 0.5f;
    limited = sidechain.load (std::memory_order_relaxed) && tpSafe && reductionDb > 0.5f;

    repaint();
}

void ClipMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    auto header = bounds.removeFromTop (14.0f);

    drawEngraved (g, "CLIP", header.removeFromLeft (44.0f), juce::Justification::left, 9.5f, 1.8f);

    drawEngraved (g, juce::String (reductionDb, 2) + " dB", header.removeFromRight (66.0f),
                  juce::Justification::right, 9.5f, 0.4f,
                  reductionDb > 0.05f ? lampAmber : engravedDim);

    if (limited)
        drawEngraved (g, "SC AT CEILING", header, juce::Justification::horizontallyCentred,
                      9.0f, 1.2f, lampRed);

    drawRecess (g, bounds, 3.0f);

    auto inside = bounds.reduced (4.0f, 4.0f);

    constexpr float fullScaleDb = 12.0f;
    constexpr int   segments    = 28;

    const auto segmentWidth = inside.getWidth() / (float) segments;

    for (int i = 0; i < segments; ++i)
    {
        const auto db = juce::jmap ((float) (i + 1), 0.0f, (float) segments, 0.0f, fullScaleDb);

        const auto cell = juce::Rectangle<float> (inside.getX() + (float) i * segmentWidth,
                                                  inside.getY(), segmentWidth, inside.getHeight())
                              .reduced (0.8f, 0.0f);

        const auto colour = db > 8.0f ? lampRed : lampAmber;
        const auto isLit  = reductionDb >= db;
        const auto isHold = holdDb >= db && holdDb < db + fullScaleDb / (float) segments;

        if (isLit || isHold)
        {
            g.setColour (colour.withAlpha (0.30f));
            g.fillRoundedRectangle (cell.expanded (1.2f), 1.5f);
            g.setColour (colour.withAlpha (isHold && ! isLit ? 0.8f : 1.0f));
            g.fillRoundedRectangle (cell, 1.0f);
        }
        else
        {
            g.setColour (colour.withAlpha (0.09f));
            g.fillRoundedRectangle (cell, 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------

namespace
{
    constexpr float plotRange = 1.5f;
    constexpr int   curveSteps = 260;
}

ScopeWindow::ScopeWindow (juce::AudioProcessorValueTreeState& state, MeterSource& source)
    : apvts (state), meters (source)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

juce::Point<float> ScopeWindow::toPixels (float x, float y) const
{
    return { juce::jmap (x, -plotRange, plotRange, plot.getX(), plot.getRight()),
             juce::jmap (y, -plotRange, plotRange, plot.getBottom(), plot.getY()) };
}

void ScopeWindow::resized()
{
    plot = getLocalBounds().toFloat().reduced (16.0f, 14.0f).withTrimmedTop (10.0f);

    const auto side = juce::jmin (plot.getWidth(), plot.getHeight());
    plot = juce::Rectangle<float> (side, side).withCentre (plot.getCentre());

    rebuild();
}

void ScopeWindow::rebuild()
{
    curvePath.clear();

    if (plot.isEmpty())
        return;

    for (int i = 0; i <= curveSteps; ++i)
    {
        const auto x = juce::jmap ((float) i, 0.0f, (float) curveSteps, -plotRange, plotRange);
        const auto p = toPixels (x, shaper.process (x));

        if (i == 0) curvePath.startNewSubPath (p);
        else        curvePath.lineTo (p);
    }
}

void ScopeWindow::timerCallback()
{
    const auto mode = apvts.getRawParameterValue (pid::mode)->load();
    const auto knee = apvts.getRawParameterValue (pid::knee)->load();
    const auto bias = apvts.getRawParameterValue (pid::bias)->load();

    if ((int) mode != lastMode
        || std::abs (knee - lastKnee) > 1.0e-4f
        || std::abs (bias - lastBias) > 1.0e-4f)
    {
        lastMode = (int) mode;
        lastKnee = knee;
        lastBias = bias;

        shaper.setParameters (static_cast<Mode> (lastMode), knee * 0.01f, bias * 0.01f);
        rebuild();
        repaint();
    }

    const auto incoming = meters.driveMagnitude.take();
    const auto updated  = incoming > drive ? incoming : juce::jmax (0.0f, drive - 0.045f);

    if (std::abs (updated - drive) > 0.0005f)
    {
        drive = updated;
        repaint();
    }
}

void ScopeWindow::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    drawRecess (g, bounds, 4.0f);

    // Phosphor glass
    juce::ColourGradient tube (glass.brighter (0.10f), bounds.getCentreX(), bounds.getCentreY(),
                               glass.darker (0.6f), bounds.getCentreX(), bounds.getBottom(), true);
    g.setGradientFill (tube);
    g.fillRoundedRectangle (bounds.reduced (2.0f), 3.0f);

    if (plot.isEmpty())
        return;

    // Graticule
    g.setColour (lampGreen.withAlpha (0.10f));

    for (int i = 1; i < 6; ++i)
    {
        const auto t = juce::jmap ((float) i, 0.0f, 6.0f, -plotRange, plotRange);
        const auto p = toPixels (t, t);

        g.drawVerticalLine   (juce::roundToInt (p.x), plot.getY(), plot.getBottom());
        g.drawHorizontalLine (juce::roundToInt (p.y), plot.getX(), plot.getRight());
    }

    g.setColour (lampGreen.withAlpha (0.22f));
    g.drawRect (plot, 1.0f);

    // Ceiling markers
    {
        const auto top    = toPixels (0.0f,  1.0f).y;
        const auto bottom = toPixels (0.0f, -1.0f).y;

        g.setColour (lampGreen.withAlpha (0.30f));
        g.drawHorizontalLine (juce::roundToInt (top),    plot.getX(), plot.getRight());
        g.drawHorizontalLine (juce::roundToInt (bottom), plot.getX(), plot.getRight());
    }

    // Unity reference
    {
        juce::Path diagonal;
        diagonal.startNewSubPath (toPixels (-plotRange, -plotRange));
        diagonal.lineTo (toPixels (plotRange, plotRange));

        const float dashes[] = { 2.0f, 5.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, diagonal, dashes, 2);

        g.setColour (lampGreen.withAlpha (0.28f));
        g.fillPath (dashed);
    }

    // The trace, with the bloom a phosphor tube has around a bright line
    g.setColour (lampAmber.withAlpha (0.18f));
    g.strokePath (curvePath, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved));
    g.setColour (lampAmber.withAlpha (0.45f));
    g.strokePath (curvePath, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved));
    g.setColour (lampAmber.brighter (0.5f));
    g.strokePath (curvePath, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));

    // Where the signal is sitting on the curve right now
    if (drive > 0.001f)
    {
        const auto x = juce::jmin (drive, plotRange);

        for (auto point : { toPixels (x, shaper.process (x)),
                            toPixels (-x, shaper.process (-x)) })
        {
            g.setColour (lampGreen.withAlpha (0.30f));
            g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (point));
            g.setColour (lampGreen.brighter (0.4f));
            g.fillEllipse (juce::Rectangle<float> (4.5f, 4.5f).withCentre (point));
        }
    }

    // Scanlines and a vignette: cheap, and they sell the glass
    g.setColour (juce::Colours::black.withAlpha (0.10f));

    for (float y = bounds.getY(); y < bounds.getBottom(); y += 3.0f)
        g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());

    juce::ColourGradient vignette (juce::Colours::transparentBlack,
                                   bounds.getCentreX(), bounds.getCentreY(),
                                   juce::Colours::black.withAlpha (0.55f),
                                   bounds.getX(), bounds.getBottom(), true);
    g.setGradientFill (vignette);
    g.fillRoundedRectangle (bounds.reduced (2.0f), 3.0f);

    drawEngraved (g, "TRANSFER", bounds.reduced (12.0f, 8.0f).withHeight (12.0f),
                  juce::Justification::left, 9.0f, 1.8f, lampGreen.withAlpha (0.55f));
}

} // namespace crest::hw
