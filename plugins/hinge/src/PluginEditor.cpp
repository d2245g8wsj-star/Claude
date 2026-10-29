#include "PluginEditor.h"

namespace hinge
{

namespace
{
    using Size = panel::Knob::Size;

    constexpr float rowTop[]  = { 14.0f, 178.0f };
    constexpr float rowHeight = 150.0f;

    // Horizontal plan of a row, left to right.
    constexpr float letterCentre = 268.0f;

    constexpr float lowWellX  = 296.0f;
    constexpr float highWellX = 686.0f;
    constexpr float toneWellWidth = 380.0f;
    constexpr float dynWellX  = 1076.0f;
    constexpr float dynWellWidth = 308.0f;

    constexpr float lowCutX = 356.0f, lowShelfX = 470.0f, lowLevelX = 598.0f;
    constexpr float highLevelX = 762.0f, highShelfX = 892.0f, highCutX = 1006.0f;
    constexpr float thresholdX = 1128.0f, lowDynX = 1230.0f, highDynX = 1332.0f;

    constexpr float outputX = 1440.0f;
    constexpr float engageX = 1514.0f;
    constexpr float meterX  = 1546.0f;
    constexpr float meterWidth = 22.0f;

    constexpr float largeSide   = 130.0f;
    constexpr float smallSide   = 96.0f;
    constexpr float compactSide = 84.0f;

    juce::Rectangle<int> square (float centreX, float centreY, float side)
    {
        return juce::Rectangle<float> (side, side).withCentre ({ centreX, centreY }).toNearestInt();
    }

    panel::Knob::Legends positions (std::initializer_list<const char*> names)
    {
        panel::Knob::Legends out;
        float index = 0.0f;

        for (auto* name : names)
            out.push_back ({ index++, name });

        return out;
    }

    panel::Knob::Legends levelLegends()
    {
        return { { -5.0f, "-5" }, { -3.0f, "-3" }, { 0.0f, "0" }, { 3.0f, "+3" }, { 5.0f, "+5" } };
    }

    panel::Knob::Legends outputLegends()
    {
        auto at = [] (int step)
        {
            const auto db = juce::Decibels::gainToDecibels (network::outputGain (step));
            return std::pair { (float) step, (db > 0.05 ? "+" : "") + juce::String (db, std::abs (db) < 0.05 ? 0 : 1) };
        };

        return { at (-10), at (-5), at (0), at (5), at (10) };
    }
}

// ---------------------------------------------------------------------------

HingeEditor::Strip::Strip (HingeProcessor& p, int index)
    : lowCut    (p.apvts, pid::forStrip (index, pid::lowCut),    panel::lowAccent,  Size::small,
                 positions ({ "OUT", "12", "18", "24", "30", "36", "43", "54" })),
      lowShelf  (p.apvts, pid::forStrip (index, pid::lowShelf),  panel::lowAccent,  Size::small,
                 positions ({ "74", "84", "98", "116", "131", "166", "230", "361" })),
      lowLevel  (p.apvts, pid::forStrip (index, pid::lowLevel),  panel::lowAccent,  Size::large, levelLegends()),
      highLevel (p.apvts, pid::forStrip (index, pid::highLevel), panel::highAccent, Size::large, levelLegends()),
      highShelf (p.apvts, pid::forStrip (index, pid::highShelf), panel::highAccent, Size::small,
                 positions ({ "18k", "7.1k", "4.8k", "3.4k", "2.5k", "2.1k", "1.8k", "1.6k" })),
      highCut   (p.apvts, pid::forStrip (index, pid::highCut),   panel::highAccent, Size::small,
                 positions ({ "OUT", "70k", "28k", "18k", "12.6", "11.1", "9k", "7.5k" })),
      threshold (p.apvts, pid::forStrip (index, pid::threshold), panel::dynAccent,  Size::compact,
                 { { -48.0f, "-48" }, { -36.0f, "-36" }, { -24.0f, "-24" }, { -12.0f, "-12" }, { 0.0f, "0" } }),
      lowDyn    (p.apvts, pid::forStrip (index, pid::lowDyn),    panel::lowAccent,  Size::compact,
                 { { -5.0f, "-5" }, { 0.0f, "0" }, { 5.0f, "+5" } }),
      highDyn   (p.apvts, pid::forStrip (index, pid::highDyn),   panel::highAccent, Size::compact,
                 { { -5.0f, "-5" }, { 0.0f, "0" }, { 5.0f, "+5" } }),
      output    (p.apvts, pid::forStrip (index, pid::output),    panel::trimAccent, Size::small, outputLegends()),
      engage    (p.apvts, pid::forStrip (index, pid::engage),    juce::Colour (0xffe8452c), "ENGAGE"),
      lowMovement  (panel::lowAccent),
      highMovement (panel::highAccent)
{
}

std::vector<juce::Component*> HingeEditor::Strip::components()
{
    // Lamps after knobs, so a button sitting in the open foot of a knob's scale
    // takes the click rather than the knob behind it.
    return { &lowCut, &lowShelf, &lowLevel, &highLevel, &highShelf, &highCut,
             &threshold, &lowDyn, &highDyn, &output,
             &engage, &lowMovement, &highMovement, &meter };
}

// ---------------------------------------------------------------------------

HingeEditor::Face::Face (HingeProcessor& p)
    : processor (p),
      link    (p.apvts, pid::link,    panel::lowAccent, "LINK"),
      midSide (p.apvts, pid::midSide, panel::lowAccent, "M/S")
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<Strip> (p, i);

        for (auto* child : strips[(size_t) i]->components())
            addAndMakeVisible (child);
    }

    addAndMakeVisible (link);
    addAndMakeVisible (midSide);
    refresh();
}

void HingeEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        layoutStrip (*strips[(size_t) i], rowTop[i]);

    link   .setBounds (juce::Rectangle<float> (52.0f, 40.0f).withCentre ({ 108.0f, 196.0f }).toNearestInt());
    midSide.setBounds (juce::Rectangle<float> (52.0f, 40.0f).withCentre ({ 176.0f, 196.0f }).toNearestInt());
}

void HingeEditor::Face::layoutStrip (Strip& s, float top)
{
    s.lowCut   .setBounds (square (lowCutX,    top + 70.0f, smallSide));
    s.lowShelf .setBounds (square (lowShelfX,  top + 70.0f, smallSide));
    s.lowLevel .setBounds (square (lowLevelX,  top + 78.0f, largeSide));
    s.highLevel.setBounds (square (highLevelX, top + 78.0f, largeSide));
    s.highShelf.setBounds (square (highShelfX, top + 70.0f, smallSide));
    s.highCut  .setBounds (square (highCutX,   top + 70.0f, smallSide));

    s.threshold.setBounds (square (thresholdX, top + 74.0f, compactSide));
    s.lowDyn   .setBounds (square (lowDynX,    top + 74.0f, compactSide));
    s.highDyn  .setBounds (square (highDynX,   top + 74.0f, compactSide));

    s.lowMovement .setBounds (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ lowDynX  + 30.0f, top + 16.0f }).toNearestInt());
    s.highMovement.setBounds (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ highDynX + 30.0f, top + 16.0f }).toNearestInt());

    s.output.setBounds (square (outputX, top + 70.0f, smallSide));
    s.engage.setBounds (juce::Rectangle<float> (60.0f, 38.0f).withCentre ({ engageX, top + 74.0f }).toNearestInt());
    s.meter .setBounds (juce::Rectangle<float> (meterX, top + 12.0f, meterWidth, rowHeight - 24.0f).toNearestInt());
}

void HingeEditor::Face::refresh()
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        auto& s = *strips[(size_t) i];
        auto& peaks = processor.meters.channels[(size_t) i];

        s.meter       .push (MeterSource::take (peaks.outputPeak));
        s.lowMovement .push (MeterSource::take (peaks.shelfMovement[0]));
        s.highMovement.push (MeterSource::take (peaks.shelfMovement[1]));
    }

    const auto linkedNow  = link.getToggleState() ? 1 : 0;
    const auto midSideNow = midSide.getToggleState() ? 1 : 0;

    if (linkedNow == shownLink && midSideNow == shownMidSide)
        return;

    shownLink    = linkedNow;
    shownMidSide = midSideNow;

    // Linked, row A drives both channels, so B's controls would only be
    // pretending. They stay visible, greyed; B's meter and lamps still show
    // the second channel.
    for (auto* child : strips[1]->components())
        if (dynamic_cast<panel::LedMeter*> (child) == nullptr && dynamic_cast<panel::MovementLamp*> (child) == nullptr)
            child->setEnabled (linkedNow == 0);

    repaint();
}

void HingeEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (panel::plateTop, 0.0f, 0.0f,
                                             panel::plateBottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    // Enamel is not quite even: faint mottling, seeded so it is the same
    // plate every time it is drawn.
    juce::Random grain (0x48494e);

    for (int i = 0; i < 220; ++i)
    {
        const auto y = grain.nextFloat() * bounds.getHeight();
        const auto x = grain.nextFloat() * bounds.getWidth();
        g.setColour (juce::Colours::black.withAlpha (0.008f + 0.012f * grain.nextFloat()));
        g.drawHorizontalLine ((int) y, x, x + 60.0f + 300.0f * grain.nextFloat());
    }

    panel::drawRackEar (g, bounds.withWidth (44.0f));
    panel::drawRackEar (g, bounds.withLeft (bounds.getRight() - 44.0f));

    // Name block.
    panel::drawTrackedLabel (g, "HINGE", { 52.0f, 30.0f, 182.0f, 40.0f }, 34.0f, 9.0f);
    panel::drawTrackedLabel (g, "SHELVING EQUALISER", { 52.0f, 74.0f, 182.0f, 14.0f }, 9.5f, 2.6f,
                             juce::Justification::centred, panel::engravingDim);

    g.setColour (panel::lowAccent);
    g.fillRect (juce::Rectangle<float> (98.0f, 96.0f, 45.0f, 2.0f));
    g.setColour (panel::highAccent);
    g.fillRect (juce::Rectangle<float> (143.0f, 96.0f, 45.0f, 2.0f));

    panel::drawTrackedLabel (g, "STEREO", { 64.0f, 148.0f, 160.0f, 14.0f }, 9.0f, 2.0f,
                             juce::Justification::centred, panel::engravingDim);
    panel::drawTrackedLabel (g, "INDIEWAVE AUDIO", { 52.0f, 296.0f, 182.0f, 14.0f }, 8.5f, 2.6f,
                             juce::Justification::centred, panel::engravingDim);

    // The seam between the name block and the rows, and between the rows.
    const auto rowsRight = meterX + meterWidth + 4.0f;

    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillRect (240.0f, 14.0f, 1.5f, bounds.getHeight() - 28.0f);
    g.fillRect (250.0f, 170.0f, rowsRight - 250.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.fillRect (241.5f, 14.0f, 1.0f, bounds.getHeight() - 28.0f);
    g.fillRect (250.0f, 171.2f, rowsRight - 250.0f, 1.0f);

    for (int i = 0; i < numChannelStrips; ++i)
        paintStripLegends (g, i, rowTop[i]);
}

void HingeEditor::Face::paintStripLegends (juce::Graphics& g, int index, float top) const
{
    const auto linked  = shownLink == 1;
    const auto msMode  = shownMidSide == 1;
    const auto greyed  = linked && index == 1;

    const juce::String letter = msMode ? (index == 0 ? "M" : "S") : (index == 0 ? "L" : "R");

    panel::drawTrackedLabel (g, letter, { letterCentre - 20.0f, top + 50.0f, 40.0f, 40.0f }, 30.0f, 0.0f,
                             juce::Justification::centred, greyed ? panel::engravingDim : panel::engraving);

    if (greyed)
        panel::drawTrackedLabel (g, "LINKED", { letterCentre - 26.0f, top + 92.0f, 52.0f, 12.0f }, 7.5f, 1.2f,
                                 juce::Justification::centred, panel::lowAccent);

    const auto small = 8.5f;

    struct Well { float x, width; const char* title; };

    for (const auto& well : { Well { lowWellX,  toneWellWidth, "LOW" },
                              Well { highWellX, toneWellWidth, "HIGH" },
                              Well { dynWellX,  dynWellWidth,  "DYNAMIC" } })
    {
        panel::drawWell (g, { well.x, top, well.width, rowHeight });
        panel::drawTrackedLabel (g, well.title, { well.x + 12.0f, top + 7.0f, 120.0f, 15.0f }, 12.0f, 2.0f,
                                 juce::Justification::centredLeft);
    }

    auto under = [&g, top, small] (float x, const char* text, float y)
    {
        panel::drawTrackedLabel (g, text, { x - 45.0f, top + y, 90.0f, 12.0f }, small, 1.8f);
    };

    under (lowCutX,    "CUT Hz",    112.0f);
    under (lowShelfX,  "SHELF Hz",  112.0f);
    under (lowLevelX,  "LEVEL dB",  126.0f);
    under (highLevelX, "LEVEL dB",  126.0f);
    under (highShelfX, "SHELF Hz",  112.0f);
    under (highCutX,   "CUT Hz",    112.0f);
    under (thresholdX, "THRESHOLD", 114.0f);
    under (lowDynX,    "LOW dB",    114.0f);
    under (highDynX,   "HIGH dB",   114.0f);
    under (outputX,    "OUTPUT",    112.0f);

    panel::drawTrackedLabel (g, "OUT", { meterX - 8.0f, top + rowHeight - 12.0f, meterWidth + 16.0f, 10.0f }, 7.0f, 1.0f,
                             juce::Justification::centred, panel::engravingDim);
}

// ---------------------------------------------------------------------------

HingeEditor::HingeEditor (HingeProcessor& p)
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

    setSize (designWidth * 4 / 5, designHeight * 4 / 5);
    startTimerHz (30);
}

HingeEditor::~HingeEditor()
{
    stopTimer();
}

void HingeEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::plateBottom);
}

void HingeEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / designWidth,
                                                     (float) getHeight() / designHeight));
}

void HingeEditor::timerCallback()
{
    face.refresh();
}

} // namespace hinge
