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
        || std::abs (live - lastLive) > 0.05f)
    {
        lastMode = (int) mode;
        lastKnee = knee;
        lastBias = bias;
        lastLive = live;

        shaper    .setParameters (static_cast<Mode> (lastMode), live, bias * 0.01f);
        baseShaper.setParameters (static_cast<Mode> (lastMode), knee, bias * 0.01f);

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

    const auto ink   = onDark ? juce::Colours::white : panel::ink;
    const auto amber = onDark ? juce::Colour (0xffffb46a) : panel::amber;

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

Waveform::Waveform (const WaveformTap& source) : tap (source)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

void Waveform::timerCallback()
{
    const auto now = tap.getWriteIndex();

    if (now != lastSeen)
    {
        lastSeen = now;
        repaint();
    }
}

void Waveform::paint (juce::Graphics& g)
{
    if (! excluded.isEmpty())
        g.excludeClipRegion (excluded);

    const auto ink   = onDark ? juce::Colours::white : panel::ink;
    const auto amber = onDark ? juce::Colour (0xffe08a44) : panel::amber;

    const auto bounds = getLocalBounds().toFloat();
    const auto centre = bounds.getCentreY();

    // 1.0 is the ceiling, and a little headroom above it is left visible so
    // material that was about to be clipped is still on screen.
    constexpr float range = 1.35f;
    const auto scale = bounds.getHeight() * 0.5f / range;

    auto toY = [centre, scale] (float value) { return centre - value * scale; };

    const auto columns = juce::jmin ((int) bounds.getWidth(), WaveformTap::capacity - 1);
    const auto newest  = tap.getWriteIndex();

    juce::Path input, output;
    bool started = false;

    for (int i = 0; i < columns; ++i)
    {
        const auto column = tap.getColumn (newest - columns + i);
        const auto x = bounds.getRight() - (float) (columns - i);

        if (! started)
        {
            input .startNewSubPath (x, toY (column.inputMax));
            output.startNewSubPath (x, toY (column.outputMax));
            started = true;
        }
        else
        {
            input .lineTo (x, toY (column.inputMax));
            output.lineTo (x, toY (column.outputMax));
        }
    }

    for (int i = columns - 1; i >= 0; --i)
    {
        const auto column = tap.getColumn (newest - columns + i);
        const auto x = bounds.getRight() - (float) (columns - i);

        input .lineTo (x, toY (column.inputMin));
        output.lineTo (x, toY (column.outputMin));
    }

    input.closeSubPath();
    output.closeSubPath();

    // The ceiling, so the eye has something to measure the tops against.
    g.setColour (ink.withAlpha (0.22f));

    for (auto level : { 1.0f, -1.0f })
        g.drawHorizontalLine (juce::roundToInt (toY (level)), bounds.getX(), bounds.getRight());

    // What the clipper was given, and then what it let through. The part of the
    // pale shape still showing above the solid one is what it took away.
    g.setColour (amber.withAlpha (0.30f));
    g.fillPath (input);

    g.setColour (amber.withAlpha (0.85f));
    g.fillPath (output);

    g.setColour (ink.withAlpha (0.30f));
    g.drawHorizontalLine (juce::roundToInt (centre), bounds.getX(), bounds.getRight());
}

} // namespace crest::panel
