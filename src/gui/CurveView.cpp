#include "CurveView.h"
#include "../PluginProcessor.h"

namespace crest::gui
{

namespace
{
    /// The plot shows 3.5 dB of headroom past the ceiling, which is about as
    /// far as anyone drives a clipper before the corner stops being the point.
    constexpr float plotRange   = 1.5f;
    constexpr int   curvePoints = 320;
}

CurveView::CurveView (juce::AudioProcessorValueTreeState& state, MeterSource& source)
    : apvts (state), meters (source)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

juce::Point<float> CurveView::toPixels (float x, float y) const
{
    return { juce::jmap (x, -plotRange, plotRange, plotArea.getX(), plotArea.getRight()),
             juce::jmap (y, -plotRange, plotRange, plotArea.getBottom(), plotArea.getY()) };
}

void CurveView::resized()
{
    plotArea = getLocalBounds().toFloat().reduced (14.0f).withTrimmedTop (18.0f).withTrimmedBottom (14.0f);

    // Keep the plot square so the diagonal really is 45 degrees; a stretched
    // plot makes a gentle knee look like a hard one.
    const auto side = juce::jmin (plotArea.getWidth(), plotArea.getHeight());
    plotArea = juce::Rectangle<float> (side, side).withCentre (plotArea.getCentre());

    rebuildCurve();
}

void CurveView::rebuildCurve()
{
    curvePath.clear();
    shadedPath.clear();

    if (plotArea.isEmpty())
        return;

    for (int i = 0; i <= curvePoints; ++i)
    {
        const auto x = juce::jmap ((float) i, 0.0f, (float) curvePoints, -plotRange, plotRange);
        const auto p = toPixels (x, shaper.process (x));

        if (i == 0) curvePath.startNewSubPath (p);
        else        curvePath.lineTo (p);
    }

    // Region between the curve and unity gain: the part that gets removed.
    shadedPath = curvePath;

    for (int i = curvePoints; i >= 0; --i)
    {
        const auto x = juce::jmap ((float) i, 0.0f, (float) curvePoints, -plotRange, plotRange);
        shadedPath.lineTo (toPixels (x, juce::jlimit (-plotRange, plotRange, x)));
    }

    shadedPath.closeSubPath();
}

void CurveView::timerCallback()
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
        characterName = modeName (static_cast<Mode> (lastMode));

        rebuildCurve();
        repaint();
    }

    const auto drive = meters.driveMagnitude.take();

    const auto updated = drive > driveLevel ? drive : juce::jmax (0.0f, driveLevel - 0.045f);

    if (std::abs (updated - driveLevel) > 0.0005f)
    {
        driveLevel = updated;
        repaint();
    }
}

void CurveView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bounds, 5.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 5.0f, 1.0f);

    // ------------------------------------------------------------- captions
    auto header = bounds.reduced (12.0f, 8.0f).withHeight (14.0f);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    theme::drawTracked (g, "TRANSFER", header, juce::Justification::left, 1.6f);

    g.setColour (theme::amber);
    theme::drawTracked (g, characterName, header, juce::Justification::right, 1.6f);

    if (plotArea.isEmpty())
        return;

    // ----------------------------------------------------------------- grid
    g.setColour (theme::outlineSoft);

    for (int i = 1; i < 6; ++i)
    {
        const auto t = juce::jmap ((float) i, 0.0f, 6.0f, -plotRange, plotRange);
        const auto p = toPixels (t, t);

        g.drawVerticalLine   (juce::roundToInt (p.x), plotArea.getY(), plotArea.getBottom());
        g.drawHorizontalLine (juce::roundToInt (p.y), plotArea.getX(), plotArea.getRight());
    }

    g.setColour (theme::outline);
    g.drawRect (plotArea, 1.0f);

    // Ceiling: the box the curve is not allowed to leave.
    {
        const auto top    = toPixels (0.0f,  1.0f).y;
        const auto bottom = toPixels (0.0f, -1.0f).y;
        const auto left   = toPixels (-1.0f, 0.0f).x;
        const auto right  = toPixels ( 1.0f, 0.0f).x;

        juce::Path dashes;
        dashes.startNewSubPath (plotArea.getX(), top);
        dashes.lineTo (plotArea.getRight(), top);
        dashes.startNewSubPath (plotArea.getX(), bottom);
        dashes.lineTo (plotArea.getRight(), bottom);
        dashes.startNewSubPath (left, plotArea.getY());
        dashes.lineTo (left, plotArea.getBottom());
        dashes.startNewSubPath (right, plotArea.getY());
        dashes.lineTo (right, plotArea.getBottom());

        const float dashLengths[] = { 3.0f, 4.0f };

        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, dashes, dashLengths, 2);

        g.setColour (theme::teal.withAlpha (0.28f));
        g.fillPath (dashed);
    }

    // Unity diagonal
    {
        juce::Path diagonal;
        diagonal.startNewSubPath (toPixels (-plotRange, -plotRange));
        diagonal.lineTo (toPixels (plotRange, plotRange));

        const float dashLengths[] = { 2.0f, 5.0f };

        juce::Path dashed;
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, diagonal, dashLengths, 2);

        g.setColour (theme::textFaint);
        g.fillPath (dashed);
    }

    // ---------------------------------------------------------------- curve
    g.setColour (theme::amber.withAlpha (0.10f));
    g.fillPath (shadedPath);

    g.setColour (theme::amber);
    g.strokePath (curvePath, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));

    // -------------------------------------------------------- live position
    if (driveLevel > 0.001f)
    {
        const auto x = juce::jmin (driveLevel, plotRange);
        const auto y = shaper.process (x);

        const auto positive = toPixels (x, y);
        const auto negative = toPixels (-x, shaper.process (-x));

        g.setColour (theme::teal.withAlpha (0.22f));
        g.drawVerticalLine (juce::roundToInt (positive.x), plotArea.getY(), plotArea.getBottom());
        g.drawHorizontalLine (juce::roundToInt (positive.y), plotArea.getX(), plotArea.getRight());

        for (auto point : { positive, negative })
        {
            g.setColour (theme::teal.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre (point));
            g.setColour (theme::teal);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (point));
        }
    }
}

} // namespace crest::gui
