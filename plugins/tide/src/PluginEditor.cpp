#include "PluginEditor.h"

namespace tide
{

namespace
{
    using Legend = panel::BakeliteKnob::Legend;

    // Horizontal plan: two halves of four columns either side of the centre
    // switches, inside a printed frame, between the rack ears.
    constexpr float earWidth     = 64.0f;
    constexpr float columnPitch  = 128.0f;
    constexpr float firstColumns[] = { 270.0f, 986.0f };
    constexpr float centreX      = 820.0f;

    constexpr float frameLeft    = 206.0f;
    constexpr float frameRight   = 1434.0f;
    constexpr float frameTop     = 14.0f;
    constexpr float frameBottom  = 282.0f;

    // Vertical plan.
    constexpr float freqY        = 88.0f;
    constexpr float gainY        = 198.0f;
    constexpr float switchY      = 140.0f;
    constexpr float switchOffset = 48.0f;

    constexpr float freqRadius   = 24.0f;
    constexpr float gainRadius   = 29.0f;
    constexpr float freqLegends  = 41.0f;
    constexpr float gainLegends  = 46.0f;
    constexpr float legendHeight = 10.5f;

    // A frequency switch turns from about nine o'clock to half past two, the
    // gain switches from seven to five with 0 at the top.
    constexpr float freqStart = -105.0f, freqEnd = 75.0f;
    constexpr float gainStart = -150.0f, gainEnd = 150.0f;

    std::vector<Legend> legends (std::initializer_list<const char*> texts)
    {
        std::vector<Legend> out;
        float index = 0.0f;

        for (auto* text : texts)
            out.push_back ({ index++, text });

        return out;
    }

    std::vector<Legend> gainLegendSet()
    {
        return legends ({ "4", "3", "2", "1", "0.5", "0", "0.5", "1", "2", "3", "4" });
    }

    juce::Rectangle<int> around (float x, float y, float radius, float legendRadius)
    {
        const auto side = 2.0f * (legendRadius + 24.0f);
        juce::ignoreUnused (radius);
        return juce::Rectangle<float> (side, side).withCentre ({ x, y }).toNearestInt();
    }

    juce::Rectangle<int> box (float x, float y, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre ({ x, y }).toNearestInt();
    }
}

// ---------------------------------------------------------------------------

TideEditor::Strip::Strip (TideProcessor& p, int index)
    : lowFreq  (p.apvts, pid::forStrip (index, pid::lowFreq),  freqRadius, freqStart, freqEnd,
                legends ({ "28", "48", "72", "132", "195", "255" }), freqLegends, legendHeight),
      midFreq  (p.apvts, pid::forStrip (index, pid::midFreq),  freqRadius, freqStart, freqEnd,
                legends ({ "400", "630", "870", "1k3", "2k4", "4k8" }), freqLegends, legendHeight),
      highFreq (p.apvts, pid::forStrip (index, pid::highFreq), freqRadius, freqStart, freqEnd,
                legends ({ "4k", "7k2", "12k", "16k", "20k", "25k" }), freqLegends, legendHeight),
      hpfFreq  (p.apvts, pid::forStrip (index, pid::hpfFreq),  freqRadius, freqStart, freqEnd,
                legends ({ "20/100", "25/125", "30/150", "35/175", "40/200", "45/225" }), freqLegends + 4.0f, legendHeight - 1.0f),
      lowGain  (p.apvts, pid::forStrip (index, pid::lowGain),  gainRadius, gainStart, gainEnd, gainLegendSet(), gainLegends, legendHeight),
      midGain  (p.apvts, pid::forStrip (index, pid::midGain),  gainRadius, gainStart, gainEnd, gainLegendSet(), gainLegends, legendHeight),
      highGain (p.apvts, pid::forStrip (index, pid::highGain), gainRadius, gainStart, gainEnd, gainLegendSet(), gainLegends, legendHeight),
      level    (p.apvts, pid::forStrip (index, pid::level),    gainRadius, gainStart, gainEnd, gainLegendSet(), gainLegends, legendHeight),
      lowShelf  (p.apvts, pid::forStrip (index, pid::lowShelf)),
      highShelf (p.apvts, pid::forStrip (index, pid::highShelf)),
      hpfIn     (p.apvts, pid::forStrip (index, pid::hpfIn)),
      hpfTimes5 (p.apvts, pid::forStrip (index, pid::hpfTimes5))
{
    for (auto* knob : { &lowFreq, &midFreq, &highFreq, &hpfFreq })
        knob->setUnit ("Hz");
}

std::vector<juce::Component*> TideEditor::Strip::components()
{
    // Switches after knobs, so a toggle beside a knob's scale takes the click.
    return { &lowFreq, &midFreq, &highFreq, &hpfFreq, &lowGain, &midGain, &highGain, &level,
             &lowShelf, &highShelf, &hpfIn, &hpfTimes5 };
}

// ---------------------------------------------------------------------------

TideEditor::Face::Face (TideProcessor& p)
    : processor (p),
      eqIn    (p.apvts, pid::eqIn,    "In",   13.0f),
      midSide (p.apvts, pid::midSide, "M/S",  12.0f),
      link    (p.apvts, pid::link,    "LINK", 8.5f),
      cue     (p.apvts, pid::cue),
      cueFirst  (panel::cueGreen),
      cueSecond (panel::cueGreen)
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<Strip> (p, i);

        for (auto* child : strips[(size_t) i]->components())
            addAndMakeVisible (child);
    }

    for (auto* child : std::initializer_list<juce::Component*> { &eqIn, &midSide, &link, &cue, &cueFirst, &cueSecond })
        addAndMakeVisible (child);

    refresh();
}

void TideEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        layoutStrip (*strips[(size_t) i], firstColumns[i]);

    eqIn   .setBounds (box (centreX, freqY,  48.0f, 48.0f));
    cue    .setBounds (box (centreX, 150.0f, 44.0f, 34.0f));
    midSide.setBounds (box (centreX, 212.0f, 48.0f, 48.0f));
    link   .setBounds (box (centreX, 258.0f, 44.0f, 18.0f));

    cueFirst .setBounds (box (centreX - 34.0f, 150.0f, 16.0f, 16.0f));
    cueSecond.setBounds (box (centreX + 34.0f, 150.0f, 16.0f, 16.0f));
}

void TideEditor::Face::layoutStrip (Strip& s, float x)
{
    const float columns[] = { x, x + columnPitch, x + 2.0f * columnPitch, x + 3.0f * columnPitch };

    s.lowFreq .setBounds (around (columns[0], freqY, freqRadius, freqLegends));
    s.midFreq .setBounds (around (columns[1], freqY, freqRadius, freqLegends));
    s.highFreq.setBounds (around (columns[2], freqY, freqRadius, freqLegends));
    s.hpfFreq .setBounds (around (columns[3], freqY, freqRadius, freqLegends + 4.0f));

    s.lowGain .setBounds (around (columns[0], gainY, gainRadius, gainLegends));
    s.midGain .setBounds (around (columns[1], gainY, gainRadius, gainLegends));
    s.highGain.setBounds (around (columns[2], gainY, gainRadius, gainLegends));
    s.level   .setBounds (around (columns[3], gainY, gainRadius, gainLegends));

    // The low band's switch sits to the right of its knob, the high band's to
    // the left; the high pass has one each side.
    s.lowShelf .setBounds (box (columns[0] + switchOffset, switchY, 22.0f, 36.0f));
    s.highShelf.setBounds (box (columns[2] - switchOffset, switchY, 22.0f, 36.0f));
    s.hpfIn    .setBounds (box (columns[3] - switchOffset, switchY, 22.0f, 36.0f));
    s.hpfTimes5.setBounds (box (columns[3] + switchOffset, switchY, 22.0f, 36.0f));
}

void TideEditor::Face::refresh()
{
    const auto position = cue.getPosition();
    cueFirst .setLit (position == (int) Cue::first);
    cueSecond.setLit (position == (int) Cue::second);

    const auto linkedNow = link.getToggleState() ? 1 : 0;

    if (linkedNow == shownLink)
        return;

    shownLink = linkedNow;

    // Linked, the left half drives both channels, so the right half would
    // only be pretending. It stays visible, dimmed.
    for (auto* child : strips[1]->components())
        child->setEnabled (linkedNow == 0);

    repaint();
}

void TideEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (panel::plateTop, 0.0f, 0.0f,
                                             panel::plateBottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    // A fine brushed grain under the anodising, seeded so it is the same
    // plate every time.
    juce::Random grain (0x544944);

    for (int i = 0; i < 520; ++i)
    {
        const auto y = grain.nextFloat() * bounds.getHeight();
        const auto x = grain.nextFloat() * bounds.getWidth();
        g.setColour ((grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.012f + 0.02f * grain.nextFloat()));
        g.drawHorizontalLine ((int) y, x, x + 40.0f + 260.0f * grain.nextFloat());
    }

    // The top and bottom edges of the plate catch a little light.
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine (0, 0.0f, bounds.getWidth());
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.drawHorizontalLine ((int) bounds.getBottom() - 1, 0.0f, bounds.getWidth());

    panel::drawRackEar (g, bounds.withWidth (earWidth), true);
    panel::drawRackEar (g, bounds.withLeft (bounds.getRight() - earWidth), false);

    // The maker's mark, where a rack unit has its logo: a wave in a ring.
    {
        const auto c = juce::Point<float> ((earWidth + frameLeft) * 0.5f, 150.0f);
        g.setColour (panel::ink);
        g.drawEllipse (juce::Rectangle<float> (40.0f, 40.0f).withCentre (c), 1.6f);

        juce::Path wave;
        for (int i = 0; i <= 40; ++i)
        {
            const auto t = (float) i / 40.0f;
            const auto px = c.x - 14.0f + 28.0f * t;
            const auto py = c.y - 6.0f * std::sin (t * juce::MathConstants<float>::twoPi) * (0.4f + 0.6f * t);
            if (i == 0) wave.startNewSubPath (px, py); else wave.lineTo (px, py);
        }
        g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        panel::drawLabel (g, "INDIEWAVE", { c.x - 50.0f, c.y + 26.0f, 100.0f, 12.0f }, 8.0f,
                          juce::Justification::centred, panel::inkDim, false, 2.0f);
    }

    // The name, top right, as a rack unit carries its model.
    panel::drawLabel (g, "Tide", { frameRight + 8.0f, freqY - 22.0f, 1576.0f - frameRight - 16.0f, 40.0f }, 30.0f,
                      juce::Justification::centred, panel::ink);
    panel::drawLabel (g, "3 BAND EQ", { frameRight + 8.0f, freqY + 16.0f, 1576.0f - frameRight - 16.0f, 12.0f }, 8.0f,
                      juce::Justification::centred, panel::inkDim, false, 2.0f);

    // The printed frame, broken along the bottom by the two channel names.
    const auto frame = juce::Rectangle<float> (frameLeft, frameTop, frameRight - frameLeft, frameBottom - frameTop);
    {
        juce::Path outline;
        outline.addRoundedRectangle (frame, 10.0f);

        g.saveState();

        const auto firstName  = juce::Rectangle<float> (frameLeft + 196.0f, frameBottom - 8.0f, 0.0f, 16.0f)
                                    .withX (firstColumns[0] + 1.5f * columnPitch - 48.0f).withWidth (96.0f);
        const auto secondName = firstName.withX (firstColumns[1] + 1.5f * columnPitch - 52.0f).withWidth (104.0f);

        g.excludeClipRegion (firstName.toNearestInt());
        g.excludeClipRegion (secondName.toNearestInt());

        g.setColour (panel::rule);
        g.strokePath (outline, juce::PathStrokeType (1.0f));
        g.restoreState();

        panel::drawLabel (g, "Left (Mid)",   firstName,  11.0f);
        panel::drawLabel (g, "Right (Side)", secondName, 11.0f);
    }

    for (auto corner : { frame.getTopLeft(), frame.getTopRight(), frame.getBottomLeft(), frame.getBottomRight() })
        panel::drawScrew (g, corner + juce::Point<float> (corner.x < centreX ? 12.0f : -12.0f,
                                                          corner.y < 150.0f ? 12.0f : -12.0f), 9.0f);

    for (int i = 0; i < numChannelStrips; ++i)
        paintStripLegends (g, firstColumns[i], i == 1 && shownLink == 1);

    // Centre: the cue switch's legends.
    for (auto side : { -1.0f, 1.0f })
    {
        const auto x = centreX + side * 34.0f;
        panel::drawLabel (g, side < 0.0f ? "L/M" : "R/S", { x - 20.0f, 128.0f, 40.0f, 12.0f }, 9.5f);
        panel::drawLabel (g, "Cue", { x - 20.0f, 161.0f, 40.0f, 12.0f }, 9.5f);
    }
}

void TideEditor::Face::paintStripLegends (juce::Graphics& g, float x, bool greyed) const
{
    const float columns[] = { x, x + columnPitch, x + 2.0f * columnPitch, x + 3.0f * columnPitch };
    const auto colour = greyed ? panel::inkDim.withAlpha (0.5f) : panel::ink;

    // The shape switches: shelf up, peak down.
    auto shapeLegend = [&g, colour] (float sx, bool low)
    {
        panel::drawShelfGlyph (g, { sx - 8.0f, switchY - 30.0f, 16.0f, 9.0f }, low, colour);
        panel::drawPeakGlyph  (g, { sx - 8.0f, switchY + 21.0f, 16.0f, 9.0f }, colour);
    };

    shapeLegend (columns[0] + switchOffset, true);
    shapeLegend (columns[2] - switchOffset, false);

    panel::drawLabel (g, "HPF in", { columns[3] - switchOffset - 22.0f, switchY + 18.0f, 44.0f, 12.0f }, 9.0f,
                      juce::Justification::centred, colour);
    panel::drawLabel (g, "x5",     { columns[3] + switchOffset - 22.0f, switchY - 30.0f, 44.0f, 12.0f }, 9.5f,
                      juce::Justification::centred, colour);

    // Under each gain switch: its name in a printed box, cut and boost
    // either side of it.
    const char* names[] = { "Low", "Mid", "High", "Level" };

    for (int i = 0; i < 4; ++i)
    {
        const auto cx  = columns[i];
        const auto tag = juce::Rectangle<float> (42.0f, 14.0f).withCentre ({ cx, 259.0f });

        g.setColour (colour.withAlpha (0.7f));
        g.drawRoundedRectangle (tag, 2.0f, 0.8f);
        panel::drawLabel (g, names[i], tag, 10.0f, juce::Justification::centred, colour);

        panel::drawLabel (g, juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")), { cx - 40.0f, 252.0f, 12.0f, 14.0f }, 13.0f, juce::Justification::centred, colour);
        panel::drawLabel (g, "+", { cx + 28.0f, 252.0f, 12.0f, 14.0f }, 13.0f, juce::Justification::centred, colour);
    }

    if (greyed)
        panel::drawLabel (g, "LINKED", { columns[1] + 0.5f * columnPitch - 40.0f, frameTop + 2.0f, 80.0f, 12.0f }, 8.5f,
                          juce::Justification::centred, panel::cueGreen.withAlpha (0.8f), true, 1.5f);
}

// ---------------------------------------------------------------------------

TideEditor::TideEditor (TideProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      face (p)
{
    addAndMakeVisible (face);
    face.setBounds (0, 0, designWidth, designHeight);

    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);

    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) designWidth / designHeight);

    setSize (designWidth, designHeight);
    startTimerHz (30);
}

TideEditor::~TideEditor()
{
    stopTimer();
}

void TideEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::plateBottom);
}

void TideEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / designWidth,
                                                     (float) getHeight() / designHeight));
}

void TideEditor::timerCallback()
{
    face.refresh();
}

} // namespace tide
