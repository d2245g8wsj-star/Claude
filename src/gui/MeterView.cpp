#include "MeterView.h"

namespace crest::gui
{

namespace
{
    constexpr float meterTopDb    =   6.0f;
    constexpr float meterBottomDb = -48.0f;
    constexpr int   refreshHz     =  30;
    constexpr float fallDbPerTick =  1.6f;
    constexpr int   holdTickCount =  30;    ///< one second
    constexpr int   overTickCount =  45;

    const int scaleMarks[] = { 6, 0, -6, -12, -18, -24, -36, -48 };
}

float LevelMeter::dbToProportion (float db) noexcept
{
    return juce::jlimit (0.0f, 1.0f, (db - meterBottomDb) / (meterTopDb - meterBottomDb));
}

LevelMeter::LevelMeter (MeterSource& source, bool useOutputSlots, const juce::String& caption)
    : meters (source),
      readsOutput (useOutputSlots),
      captionText (caption),
      accent (useOutputSlots ? theme::teal : theme::amber)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

void LevelMeter::timerCallback()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& channel = channels[ch];

        const auto magnitude = readsOutput ? meters.outputPeak[ch].take()
                                           : meters.inputPeak[ch].take();

        const auto db = juce::Decibels::gainToDecibels (magnitude, -100.0f);

        // Instant attack, linear fall: what a peak meter is for.
        channel.level = db > channel.level ? db
                                           : juce::jmax (-100.0f, channel.level - fallDbPerTick);

        if (db >= channel.hold)
        {
            channel.hold      = db;
            channel.holdTicks = holdTickCount;
        }
        else if (--channel.holdTicks <= 0)
        {
            channel.hold = juce::jmax (-100.0f, channel.hold - fallDbPerTick * 0.5f);
        }

        if (magnitude >= 1.0f)
        {
            channel.over      = true;
            channel.overTicks = overTickCount;
        }
        else if (channel.over && --channel.overTicks <= 0)
        {
            channel.over = false;
        }
    }

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Caption
    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    theme::drawTracked (g, captionText, bounds.removeFromTop (14.0f),
                        juce::Justification::horizontallyCentred, 1.4f);

    // Over indicator
    auto overArea = bounds.removeFromTop (10.0f).reduced (6.0f, 2.0f);
    const auto anyOver = channels[0].over || channels[1].over;

    g.setColour (anyOver ? theme::red : theme::outlineSoft);
    g.fillRoundedRectangle (overArea, 2.0f);

    bounds.removeFromTop (4.0f);

    // Room at the bottom for the last scale label to sit beside the bars
    // instead of being clipped by the component edge.
    bounds.removeFromBottom (6.0f);

    auto scaleArea = bounds.removeFromRight (22.0f);
    auto barsArea  = bounds.reduced (4.0f, 0.0f);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (barsArea, 2.0f);

    g.setFont (theme::valueFont (9.0f));

    for (auto mark : scaleMarks)
    {
        const auto y = barsArea.getBottom() - dbToProportion ((float) mark) * barsArea.getHeight();

        g.setColour (theme::outlineSoft);
        g.drawHorizontalLine (juce::roundToInt (y), barsArea.getX(), barsArea.getRight());

        g.setColour (theme::textFaint);
        g.drawText (juce::String (mark),
                    juce::Rectangle<float> (scaleArea.getX() + 2.0f, y - 6.0f, scaleArea.getWidth() - 2.0f, 12.0f),
                    juce::Justification::centredLeft, false);
    }

    // Bars
    const auto gap      = 3.0f;
    const auto barWidth = (barsArea.getWidth() - gap) * 0.5f;

    for (int ch = 0; ch < 2; ++ch)
    {
        auto bar = juce::Rectangle<float> (barsArea.getX() + (float) ch * (barWidth + gap),
                                           barsArea.getY(), barWidth, barsArea.getHeight());

        const auto level = dbToProportion (channels[ch].level);

        if (level > 0.0f)
        {
            auto filled = bar.withTop (bar.getBottom() - level * bar.getHeight());

            // Warm above 0 dBFS, cool below it: the colour change is the
            // headroom readout.
            juce::ColourGradient gradient (accent.withMultipliedBrightness (0.85f), bar.getX(), bar.getBottom(),
                                           theme::red, bar.getX(), bar.getY() - bar.getHeight() * 0.05f, false);
            gradient.addColour (juce::jlimit (0.01, 0.99, (double) dbToProportion (0.0f)), accent);

            g.setGradientFill (gradient);
            g.fillRect (filled);
        }

        if (channels[ch].hold > meterBottomDb)
        {
            const auto y = bar.getBottom() - dbToProportion (channels[ch].hold) * bar.getHeight();

            g.setColour (channels[ch].hold >= 0.0f ? theme::red : accent.brighter (0.4f));
            g.fillRect (juce::Rectangle<float> (bar.getX(), y - 1.0f, bar.getWidth(), 1.5f));
        }
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (barsArea, 2.0f, 1.0f);
}

// ---------------------------------------------------------------------------

ReductionMeter::ReductionMeter (MeterSource& source)
    : meters (source)
{
    setInterceptsMouseClicks (false, false);
    startTimerHz (refreshHz);
}

void ReductionMeter::timerCallback()
{
    // The audio thread reports the largest fraction it removed from a sample;
    // 0.2 means the peak came out at 80% of where it went in.
    const auto removed = juce::jlimit (0.0f, 0.999f, meters.reduction.take());
    const auto db      = -juce::Decibels::gainToDecibels (1.0f - removed, -60.0f);

    reductionDb = db > reductionDb ? db : juce::jmax (0.0f, reductionDb - 0.5f);

    if (db >= holdDb)
    {
        holdDb    = db;
        holdTicks = holdTickCount;
    }
    else if (--holdTicks <= 0)
    {
        holdDb = juce::jmax (0.0f, holdDb - 0.25f);
    }

    repaint();
}

void ReductionMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    auto captionArea = bounds.removeFromTop (13.0f);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    theme::drawTracked (g, "CLIP", captionArea.removeFromLeft (44.0f),
                        juce::Justification::left, 1.4f);

    g.setColour (reductionDb > 0.05f ? theme::amber : theme::textFaint);
    g.setFont (theme::valueFont (10.0f));
    g.drawText (juce::String (reductionDb, 2) + " dB", captionArea,
                juce::Justification::right, false);

    auto bar = bounds.reduced (0.0f, 1.0f);

    g.setColour (theme::panel);
    g.fillRoundedRectangle (bar, 2.0f);

    constexpr float fullScaleDb = 12.0f;

    const auto proportion = juce::jlimit (0.0f, 1.0f, reductionDb / fullScaleDb);

    if (proportion > 0.0f)
    {
        juce::ColourGradient gradient (theme::amber, bar.getX(), 0.0f,
                                       theme::red,   bar.getRight(), 0.0f, false);
        g.setGradientFill (gradient);
        g.fillRoundedRectangle (bar.withWidth (juce::jmax (2.0f, proportion * bar.getWidth())), 2.0f);
    }

    if (holdDb > 0.02f)
    {
        const auto x = bar.getX() + juce::jlimit (0.0f, 1.0f, holdDb / fullScaleDb) * bar.getWidth();

        g.setColour (theme::amber.brighter (0.5f));
        g.fillRect (juce::Rectangle<float> (juce::jmin (x, bar.getRight() - 1.5f), bar.getY(), 1.5f, bar.getHeight()));
    }

    g.setColour (theme::outline);
    g.drawRoundedRectangle (bar, 2.0f, 1.0f);
}

} // namespace crest::gui
