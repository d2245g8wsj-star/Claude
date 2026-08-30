#include "Panel.h"
#include "../PluginProcessor.h"

namespace crest::panel
{

namespace
{
    constexpr int refreshHz = 30;
}

juce::Font plateFont (float height, bool bold)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

// ---------------------------------------------------------------------------

Filmstrip::Filmstrip (const void* data, int size)
{
    image = juce::ImageCache::getFromMemory (data, size);

    if (image.isValid() && image.getWidth() > 0)
    {
        // Square frames stacked vertically.
        numFrames   = juce::jmax (1, image.getHeight() / image.getWidth());
        frameHeight = image.getHeight() / numFrames;
    }
}

void Filmstrip::drawFrame (juce::Graphics& g, juce::Rectangle<float> area, int frame) const
{
    if (! image.isValid())
        return;

    const auto index  = juce::jlimit (0, numFrames - 1, frame);
    const auto target = area.toNearestInt();

    g.drawImage (image,
                 target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                 0, index * frameHeight, image.getWidth(), frameHeight, false);
}

void Filmstrip::drawProportion (juce::Graphics& g, juce::Rectangle<float> area,
                                float proportion) const
{
    drawFrame (g, area, juce::roundToInt (juce::jlimit (0.0f, 1.0f, proportion)
                                          * (float) (numFrames - 1)));
}

// ---------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
            const Filmstrip& strip)
    : filmstrip (strip), attachment (state, parameterID, *this)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setMouseDragSensitivity (200);
    setWantsKeyboardFocus (false);

    if (auto* p = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, state.getParameterRange (parameterID)
                                             .convertFrom0to1 (p->getDefaultValue()));
        setTooltip (p->getName (64) + ": " + getTextFromValue (getValue()));
    }
}

void Knob::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    if (! frameMap.isEmpty())
    {
        const auto position = juce::jlimit (0, frameMap.size() - 1,
                                            juce::roundToInt (getValue()));

        filmstrip.drawFrame (g, area, frameMap[position]);
    }
    else
    {
        filmstrip.drawProportion (g, area, (float) valueToProportionOfLength (getValue()));
    }

    // The legend is printed on the plate, so the only thing worth adding is the
    // value, and only while the control is being used.
    if (isMouseOverOrDragging())
    {
        auto readout = juce::Rectangle<float> (area.getWidth(), 15.0f)
                           .withCentre ({ area.getCentreX(), area.getBottom() - 4.0f });

        g.setColour (cream.withAlpha (0.92f));
        g.fillRoundedRectangle (readout.reduced (area.getWidth() * 0.14f, 1.0f), 2.5f);

        g.setColour (ink);
        g.setFont (plateFont (10.0f));
        g.drawText (getTextFromValue (getValue()), readout, juce::Justification::centred, false);
    }
}

// ---------------------------------------------------------------------------

LedMeter::LedMeter (MeterSource& source, bool readsOutput,
                    const juce::Image& off, const juce::Image& green,
                    const juce::Image& amberLed, const juce::Image& red)
    : meters (source), output (readsOutput),
      offImage (off), greenImage (green), amberImage (amberLed), redImage (red)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

void LedMeter::timerCallback()
{
    const auto magnitude = juce::jmax (output ? meters.outputPeak[0].take() : meters.inputPeak[0].take(),
                                       output ? meters.outputPeak[1].take() : meters.inputPeak[1].take());

    const auto db = juce::Decibels::gainToDecibels (magnitude, -100.0f);

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
    auto bounds = getLocalBounds().toFloat();

    g.setColour (recess);
    g.fillRoundedRectangle (bounds, 4.0f);

    if (! offImage.isValid())
        return;

    // Keep the segment's own proportions rather than stretching it, and let the
    // count follow from the height that leaves.
    const auto aspect     = (float) offImage.getHeight() / (float) offImage.getWidth();
    const auto inner      = bounds.reduced (2.0f, 3.0f);
    const auto segment    = inner.getWidth() * aspect;
    const auto count      = juce::jmax (4, (int) std::floor (inner.getHeight() / segment));
    const auto spacing    = inner.getHeight() / (float) count;

    constexpr float topDb    =   6.0f;
    constexpr float bottomDb = -42.0f;

    for (int i = 0; i < count; ++i)
    {
        const auto fraction = (float) i / (float) (count - 1);
        const auto db       = juce::jmap (fraction, 0.0f, 1.0f, bottomDb, topDb);

        const auto cell = juce::Rectangle<float> (inner.getX(),
                                                  inner.getBottom() - (float) (i + 1) * spacing,
                                                  inner.getWidth(), spacing).reduced (0.0f, spacing * 0.08f);

        const auto lit     = level >= db;
        const auto holding = ! lit && hold >= db && hold < db + (topDb - bottomDb) / (float) count;

        const juce::Image* which = &offImage;

        if (lit || holding)
            which = db >= 0.0f ? &redImage : (db >= -9.0f ? &amberImage : &greenImage);

        if (which->isValid())
        {
            if (holding)
                g.setOpacity (0.65f);

            g.drawImage (*which, cell, juce::RectanglePlacement::stretchToFit, false);
            g.setOpacity (1.0f);
        }
    }
}

// ---------------------------------------------------------------------------

namespace
{
    constexpr float plotRange  = 1.5f;
    constexpr int   curveSteps = 220;
}

Scope::Scope (juce::AudioProcessorValueTreeState& state, MeterSource& source,
              const std::atomic<float>& liveKnee)
    : apvts (state), meters (source), adaptedKnee (liveKnee)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

juce::Point<float> Scope::toPixels (float x, float y) const
{
    return { juce::jmap (x, -plotRange, plotRange, plot.getX(), plot.getRight()),
             juce::jmap (y, -plotRange, plotRange, plot.getBottom(), plot.getY()) };
}

void Scope::resized()
{
    plot = getLocalBounds().toFloat().reduced (3.0f);
    rebuild();
}

void Scope::rebuild()
{
    curvePath.clear();
    basePath.clear();

    if (plot.isEmpty())
        return;

    for (int i = 0; i <= curveSteps; ++i)
    {
        const auto x = juce::jmap ((float) i, 0.0f, (float) curveSteps, -plotRange, plotRange);

        const auto live = toPixels (x, shaper.process (x));
        const auto base = toPixels (x, baseShaper.process (x));

        if (i == 0) { curvePath.startNewSubPath (live); basePath.startNewSubPath (base); }
        else        { curvePath.lineTo (live);          basePath.lineTo (base); }
    }
}

void Scope::timerCallback()
{
    const auto mode = apvts.getRawParameterValue (pid::mode)->load();
    const auto knee = apvts.getRawParameterValue (pid::knee)->load();
    const auto bias = apvts.getRawParameterValue (pid::bias)->load();
    const auto live = adaptedKnee.load (std::memory_order_relaxed);

    if ((int) mode != lastMode
        || std::abs (knee - lastKnee) > 1.0e-4f
        || std::abs (bias - lastBias) > 1.0e-4f
        || std::abs (live - lastLive) > 3.0e-3f)
    {
        lastMode = (int) mode;
        lastKnee = knee;
        lastBias = bias;
        lastLive = live;

        shaper    .setParameters (static_cast<Mode> (lastMode), live,         bias * 0.01f);
        baseShaper.setParameters (static_cast<Mode> (lastMode), knee * 0.01f, bias * 0.01f);

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

void Scope::paint (juce::Graphics& g)
{
    if (plot.isEmpty())
        return;

    // The window and its grid are etched into the plate, so only the trace and
    // the reading belong here.
    {
        juce::Path diagonal;
        diagonal.startNewSubPath (toPixels (-plotRange, -plotRange));
        diagonal.lineTo (toPixels (plotRange, plotRange));

        const float dashes[] = { 2.0f, 4.0f };
        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, diagonal, dashes, 2);

        g.setColour (ink.withAlpha (0.22f));
        g.fillPath (dashed);
    }

    g.setColour (ink.withAlpha (0.20f));
    g.strokePath (basePath, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

    g.setColour (amber);
    g.strokePath (curvePath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

    if (drive > 0.001f)
    {
        const auto x = juce::jmin (drive, plotRange);

        for (auto point : { toPixels (x, shaper.process (x)),
                            toPixels (-x, shaper.process (-x)) })
        {
            g.setColour (amber.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (point));
            g.setColour (amber.darker (0.3f));
            g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (point));
        }
    }
}

// ---------------------------------------------------------------------------

namespace
{
    void drawFace (juce::Graphics& g, juce::Rectangle<float> area, bool on, bool hover, bool pressed)
    {
        g.setColour (ink.withAlpha (0.20f));
        g.fillRoundedRectangle (area.translated (0.0f, 1.0f), 2.5f);

        auto face = area;

        juce::ColourGradient fill (on ? amber.brighter (0.12f) : cream.brighter (0.05f),
                                   0.0f, face.getY(),
                                   on ? amber.darker (0.16f)   : cream.darker (0.12f),
                                   0.0f, face.getBottom(), false);
        g.setGradientFill (fill);
        g.fillRoundedRectangle (face, 2.5f);

        if (pressed)
        {
            g.setColour (ink.withAlpha (0.18f));
            g.fillRoundedRectangle (face, 2.5f);
        }
        else if (hover && ! on)
        {
            g.setColour (amber.withAlpha (0.14f));
            g.fillRoundedRectangle (face, 2.5f);
        }

        g.setColour (ink.withAlpha (0.45f));
        g.drawRoundedRectangle (face.reduced (0.5f), 2.5f, 1.0f);
    }

    void drawLegend (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                     bool on, float height = 9.5f)
    {
        g.setColour (on ? cream : ink.withAlpha (0.85f));
        g.setFont (plateFont (height));
        g.drawText (text, area, juce::Justification::centred, false);
    }
}

Switch::Switch (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
                const juce::String& caption)
    : parameter (*state.getParameter (parameterID)), captionText (caption),
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

    // Where the face is printed on the plate there is nothing to draw until the
    // switch is engaged: the art already shows it in its resting state.
    if (! drawFace)
    {
        if (on)
        {
            panel::drawFace (g, area, true, false, false);
            drawLegend (g, captionText, area, true);
        }
        else if (hover)
        {
            g.setColour (amber.withAlpha (0.16f));
            g.fillRoundedRectangle (area, 2.5f);
        }

        return;
    }

    panel::drawFace (g, area, on, hover, false);
    drawLegend (g, captionText, area, on);
}

// ---------------------------------------------------------------------------

Push::Push (const juce::String& caption, std::function<void()> onClick)
    : captionText (caption), callback (std::move (onClick))
{
}

void Push::mouseDown (const juce::MouseEvent&)
{
    held = true;
    repaint();

    if (callback)
        callback();
}

void Push::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();

    if (! drawFace)
    {
        if (lit || held)
        {
            panel::drawFace (g, area, lit, false, held);
            drawLegend (g, captionText, area, lit);
        }

        return;
    }

    panel::drawFace (g, area, lit, false, held);
    drawLegend (g, captionText, area, lit);
}

// ---------------------------------------------------------------------------

Bank::Bank (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID,
            const juce::String& caption)
    : parameter (*state.getParameter (parameterID)), captionText (caption),
      attachment (parameter, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr)
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (&parameter))
        choices = choice->choices;

    setTooltip (parameter.getName (64));
    attachment.sendInitialUpdate();
}

juce::Rectangle<float> Bank::strip() const
{
    return getLocalBounds().toFloat().withTrimmedTop (13.0f);
}

juce::Rectangle<float> Bank::cell (int i) const
{
    const auto area  = strip();
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
    g.setColour (ink.withAlpha (0.7f));
    g.setFont (plateFont (8.5f));
    g.drawText (captionText, getLocalBounds().toFloat().withHeight (12.0f),
                juce::Justification::centredLeft, false);

    for (int i = 0; i < choices.size(); ++i)
    {
        const auto area = cell (i).reduced (1.0f, 0.0f);

        panel::drawFace (g, area, i == index, false, false);
        drawLegend (g, choices[i], area, i == index, 9.0f);
    }
}

} // namespace crest::panel
