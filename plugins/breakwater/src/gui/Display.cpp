#include "Display.h"
#include "Panel.h"

namespace breakwater::display
{

namespace
{
    float toDb (float gain) { return gain > 1.0e-6f ? 20.0f * std::log10 (gain) : -120.0f; }

    juce::String formatDb (float db, int decimals = 1)
    {
        if (db <= -99.0f)
            return "-inf";

        return (db > 0.05f ? "+" : "") + juce::String (db, decimals);
    }

    constexpr int holdFramesForPeak = 45;   // 1.5 s at 30 Hz
}

// ---------------------------------------------------------------------------

Scope::Scope (MeterSource& s)
    : source (s),
      columns ((size_t) numColumns)
{
    readIndex = source.history.getWriteIndex();
    setOpaque (false);
}

float Scope::yForDb (float db, juce::Rectangle<float> area) const noexcept
{
    const auto proportion = juce::jlimit (0.0f, 1.0f, -db / rangeDb);
    return area.getY() + proportion * area.getHeight();
}

void Scope::refresh (float newCeilingDb, float reductionNowDb, const juce::String& newCaption)
{
    const auto writeIndex = source.history.getWriteIndex();

    // After a long pause only the most recent screenful matters.
    if (writeIndex - readIndex > History::capacity)
        readIndex = writeIndex - History::capacity;

    while (readIndex < writeIndex)
    {
        newest = (newest + 1) % numColumns;
        columns[(size_t) newest] = source.history.getColumn (readIndex++);
    }

    ceilingDb = newCeilingDb;
    reductionDb = reductionNowDb;

    if (reductionNowDb >= reductionHold)
    {
        reductionHold = reductionNowDb;
        holdFrames = holdFramesForPeak;
    }
    else if (--holdFrames <= 0)
    {
        reductionHold = juce::jmax (reductionNowDb, reductionHold - 0.5f);
    }

    caption = newCaption;
    repaint();
}

void Scope::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    panel::drawWell (g, bounds, 8.0f);

    const auto plot = bounds.reduced (40.0f, 14.0f).withTrimmedTop (10.0f);

    // Grid and scales: level on the left, reduction on the right.
    for (float db = 0.0f; db >= -rangeDb; db -= 3.0f)
    {
        const auto y = yForDb (db, plot);
        const auto major = std::fmod (-db, 6.0f) < 0.01f;

        g.setColour (juce::Colours::white.withAlpha (major ? 0.07f : 0.035f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());

        if (major)
        {
            panel::drawLabel (g, juce::String ((int) db), { bounds.getX() + 4.0f, y - 6.0f, 30.0f, 12.0f }, 9.5f,
                              panel::textFaint, juce::Justification::centredRight, false, 0.0f);
            panel::drawLabel (g, db > -0.5f ? "GR" : juce::String ((int) -db),
                              { plot.getRight() + 6.0f, y - 6.0f, 30.0f, 12.0f }, 9.5f,
                              panel::reduction.withAlpha (0.55f), juce::Justification::centredLeft, false, 0.0f);
        }
    }

    // Envelopes, oldest at the left.
    const auto step = plot.getWidth() / (float) (numColumns - 1);

    juce::Path inputPath, outputPath, reductionPath;
    inputPath.startNewSubPath (plot.getX(), plot.getBottom());
    outputPath.startNewSubPath (plot.getX(), plot.getBottom());
    reductionPath.startNewSubPath (plot.getX(), plot.getY());

    for (int i = 0; i < numColumns; ++i)
    {
        const auto& c = columns[(size_t) ((newest + 1 + i) % numColumns)];
        const auto x = plot.getX() + step * (float) i;

        inputPath.lineTo  (x, yForDb (toDb (c.input), plot));
        outputPath.lineTo (x, yForDb (toDb (c.output), plot));
        reductionPath.lineTo (x, plot.getY() + juce::jlimit (0.0f, 1.0f, c.reduction / rangeDb) * plot.getHeight());
    }

    inputPath.lineTo (plot.getRight(), plot.getBottom());
    inputPath.closeSubPath();
    outputPath.lineTo (plot.getRight(), plot.getBottom());
    outputPath.closeSubPath();
    reductionPath.lineTo (plot.getRight(), plot.getY());
    reductionPath.closeSubPath();

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    g.setColour (panel::inputShade.withAlpha (0.55f));
    g.fillPath (inputPath);

    g.setGradientFill (juce::ColourGradient (panel::accent.withAlpha (0.55f), 0.0f, plot.getY(),
                                             panel::accent.withAlpha (0.12f), 0.0f, plot.getBottom(), false));
    g.fillPath (outputPath);
    g.setColour (panel::accent.withAlpha (0.9f));
    g.strokePath (outputPath, juce::PathStrokeType (1.0f));

    g.setColour (panel::reduction.withAlpha (0.6f));
    g.fillPath (reductionPath);
    g.setColour (panel::reduction);
    g.strokePath (reductionPath, juce::PathStrokeType (1.2f));

    g.restoreState();

    // The ceiling.
    const auto ceilingY = yForDb (ceilingDb, plot);
    const float dashes[] = { 5.0f, 4.0f };
    g.setColour (panel::text.withAlpha (0.45f));
    g.drawDashedLine ({ plot.getX(), ceilingY, plot.getRight(), ceilingY }, dashes, 2, 1.0f);
    panel::drawLabel (g, "CEILING " + juce::String (ceilingDb, 1), { plot.getX() + 6.0f, ceilingY + 2.0f, 140.0f, 12.0f },
                      9.0f, panel::textDim, juce::Justification::centredLeft, true, 0.8f);

    // Caption and the reduction readout.
    panel::drawLabel (g, caption, { plot.getX() + 6.0f, bounds.getY() + 6.0f, plot.getWidth() * 0.7f, 14.0f }, 10.5f,
                      panel::textDim, juce::Justification::centredLeft, false, 0.2f);

    const auto readout = juce::Rectangle<float> (plot.getRight() - 170.0f, bounds.getY() + 4.0f, 164.0f, 26.0f);
    panel::drawLabel (g, "GR " + juce::String (-reductionDb, 1) + " dB", readout, 19.0f, panel::reduction,
                      juce::Justification::centredRight, true, 0.4f);
    panel::drawLabel (g, "MAX " + juce::String (-reductionHold, 1),
                      readout.translated (0.0f, 22.0f).withHeight (14.0f), 9.5f, panel::textDim,
                      juce::Justification::centredRight, true, 0.6f);
}

// ---------------------------------------------------------------------------

void Meters::Bar::push (float db)
{
    // Instant rise, 24 dB per second fall at 30 frames per second.
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 0.8f);

    if (db >= holdDb)
    {
        holdDb = db;
        holdFrames = holdFramesForPeak;
    }
    else if (--holdFrames <= 0)
    {
        holdDb = juce::jmax (levelDb, holdDb - 0.8f);
    }
}

Meters::Meters (MeterSource& s) : source (s) {}

void Meters::refresh()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto in  = toDb (source.inputPeak[ch].take());
        const auto out = toDb (source.outputPeak[ch].take());

        input[(size_t) ch].push (in);
        output[(size_t) ch].push (out);

        inputMaxDb  = juce::jmax (inputMaxDb, in);
        outputMaxDb = juce::jmax (outputMaxDb, out);
    }

    const auto gr = source.reduction.take();
    reductionDb = gr > reductionDb ? gr : juce::jmax (gr, reductionDb - 0.6f);

    if (reductionDb >= reductionHold)
    {
        reductionHold = reductionDb;
        reductionHoldFrames = holdFramesForPeak;
    }
    else if (--reductionHoldFrames <= 0)
    {
        reductionHold = juce::jmax (reductionDb, reductionHold - 0.6f);
    }

    repaint();
}

void Meters::paintBar (juce::Graphics& g, juce::Rectangle<float> area, const Bar& bar, juce::Colour colour) const
{
    g.setColour (panel::well);
    g.fillRoundedRectangle (area, 2.0f);

    const auto proportion = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb); };

    const auto top = area.getBottom() - proportion (bar.levelDb) * area.getHeight();
    auto fill = area.withTop (top);

    g.setGradientFill (juce::ColourGradient (colour, 0.0f, area.getY(), colour.withAlpha (0.35f), 0.0f, area.getBottom(), false));
    g.fillRoundedRectangle (fill, 2.0f);

    if (bar.holdDb > floorDb)
    {
        const auto y = area.getBottom() - proportion (bar.holdDb) * area.getHeight();
        g.setColour (bar.holdDb > -0.01f ? panel::warning : panel::text.withAlpha (0.8f));
        g.fillRect (area.getX(), y - 1.0f, area.getWidth(), 2.0f);
    }
}

void Meters::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    panel::drawWell (g, bounds, 8.0f);

    const auto inner = bounds.reduced (10.0f, 8.0f);
    const auto barsTop = inner.getY() + 34.0f;
    const auto barsBottom = inner.getBottom() - 16.0f;
    const auto barsHeight = barsBottom - barsTop;

    const auto barWidth   = 18.0f;
    const auto scaleWidth = 28.0f;
    const auto groupWidth = 2.0f * barWidth + 2.0f + 6.0f + scaleWidth + 6.0f + barWidth + 14.0f + 2.0f * barWidth + 2.0f;

    // Columns, left to right: input pair, scale, reduction, output pair.
    const auto inX    = inner.getX() + (inner.getWidth() - groupWidth) * 0.5f;
    const auto scaleX = inX + 2.0f * barWidth + 2.0f + 6.0f;
    const auto grX    = scaleX + scaleWidth + 6.0f;
    const auto outX   = grX + barWidth + 14.0f;

    for (int ch = 0; ch < 2; ++ch)
    {
        paintBar (g, { inX + (float) ch * (barWidth + 2.0f), barsTop, barWidth, barsHeight }, input[(size_t) ch], panel::inputShade.brighter (0.6f));
        paintBar (g, { outX + (float) ch * (barWidth + 2.0f), barsTop, barWidth, barsHeight }, output[(size_t) ch], panel::accent);
    }

    // Scale.
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f })
    {
        const auto y = barsBottom - (db - floorDb) / -floorDb * barsHeight;
        panel::drawLabel (g, juce::String ((int) db), { scaleX, y - 6.0f, scaleWidth, 12.0f }, 8.5f,
                          panel::textFaint, juce::Justification::centred, false, 0.0f);
    }

    // Reduction grows downwards from the top, on a 24 dB scale.
    const auto grArea = juce::Rectangle<float> (grX, barsTop, barWidth, barsHeight);
    g.setColour (panel::well);
    g.fillRoundedRectangle (grArea, 2.0f);
    g.setColour (panel::reduction);
    g.fillRoundedRectangle (grArea.withHeight (juce::jlimit (0.0f, 1.0f, reductionDb / 24.0f) * barsHeight), 2.0f);
    g.setColour (panel::text.withAlpha (0.8f));
    g.fillRect (grX, barsTop + juce::jlimit (0.0f, 1.0f, reductionHold / 24.0f) * barsHeight - 1.0f, barWidth, 2.0f);

    // Numbers above, names below.
    const auto numbers = [&] (float x, float width, const juce::String& text, juce::Colour c)
    {
        panel::drawLabel (g, text, { x - 6.0f, inner.getY(), width + 12.0f, 14.0f }, 10.5f, c,
                          juce::Justification::centred, true, 0.2f);
    };

    numbers (inX, 2.0f * barWidth + 2.0f, formatDb (inputMaxDb), panel::textDim);
    numbers (grX - 4.0f, barWidth + 8.0f, juce::String (-reductionHold, 1), panel::reduction);
    numbers (outX, 2.0f * barWidth + 2.0f, formatDb (outputMaxDb), outputMaxDb > -0.01f ? panel::warning : panel::text);

    panel::drawLabel (g, "PEAK", { inner.getX(), inner.getY() + 15.0f, inner.getWidth(), 12.0f }, 8.0f, panel::textFaint);

    const auto names = barsBottom + 3.0f;
    panel::drawLabel (g, "IN",  { inX, names, 2.0f * barWidth + 2.0f, 12.0f }, 9.0f);
    panel::drawLabel (g, "GR",  { grX - 4.0f, names, barWidth + 8.0f, 12.0f }, 9.0f);
    panel::drawLabel (g, "OUT", { outX, names, 2.0f * barWidth + 2.0f, 12.0f }, 9.0f);
}

// ---------------------------------------------------------------------------

Loudness::Loudness (MeterSource& s) : source (s)
{
    setTooltip ("Click to reset the integrated loudness, range and true peak");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Loudness::refresh (float newCeilingDb)
{
    momentary  = source.momentary.load();
    shortTerm  = source.shortTerm.load();
    integrated = source.integrated.load();
    range      = source.range.load();
    truePeakDb = toDb (source.truePeakMax.load());
    matchDb    = source.matchGainDb.load();
    ceilingDb  = newCeilingDb;
    repaint();
}

void Loudness::mouseDown (const juce::MouseEvent&)
{
    source.resetRequested.store (true);
}

void Loudness::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    panel::drawWell (g, bounds, 8.0f);

    auto area = bounds.reduced (12.0f, 8.0f);

    const auto lufs = [] (float v) { return v <= -69.9f ? juce::String ("--") : juce::String (v, 1); };

    // Integrated, large.
    panel::drawLabel (g, "INTEGRATED", area.removeFromTop (14.0f), 9.0f, panel::textDim,
                      juce::Justification::centredLeft);

    auto big = area.removeFromTop (34.0f);
    panel::drawLabel (g, lufs (integrated), big.removeFromLeft (big.getWidth() * 0.66f), 30.0f, panel::text,
                      juce::Justification::centredLeft, true, 0.2f);
    panel::drawLabel (g, "LUFS", big, 11.0f, panel::textDim, juce::Justification::bottomLeft);

    area.removeFromTop (6.0f);

    const auto row = [&] (const juce::String& name, const juce::String& value, const juce::String& unit, juce::Colour colour)
    {
        auto r = area.removeFromTop (19.0f);
        panel::drawLabel (g, name, r.removeFromLeft (r.getWidth() * 0.46f), 9.0f, panel::textDim,
                          juce::Justification::centredLeft);
        auto unitArea = r.removeFromRight (36.0f);
        panel::drawLabel (g, value, r, 13.0f, colour, juce::Justification::centredRight, true, 0.1f);
        panel::drawLabel (g, unit, unitArea, 8.5f, panel::textFaint, juce::Justification::centredRight);
    };

    row ("SHORT TERM", lufs (shortTerm), "LUFS", panel::text);
    row ("MOMENTARY",  lufs (momentary), "LUFS", panel::text);
    row ("RANGE",      juce::String (range, 1), "LU", panel::text);
    row ("TRUE PEAK",  truePeakDb <= -99.0f ? juce::String ("--") : juce::String (truePeakDb, 2), "dBTP",
         truePeakDb > ceilingDb + 0.05f ? panel::warning : panel::accent);
    row ("MATCH",      formatDb (matchDb), "dB", panel::textDim);
}

} // namespace breakwater::display
