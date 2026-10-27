#include "PluginEditor.h"

namespace ember
{

namespace
{
    using Size = panel::Knob::Size;

    constexpr float rowTop[]   = { 14.0f, 178.0f };
    constexpr float rowHeight  = 150.0f;

    // Horizontal plan of a row, left to right.
    constexpr float labelCentre  = 266.0f;
    constexpr float inputCentre  = 336.0f;
    constexpr float wellX[]      = { 392.0f, 642.0f, 892.0f };
    constexpr float wellWidth    = 240.0f;
    constexpr float outputCentre = 1182.0f;
    constexpr float activeCentre = 1258.0f;
    constexpr float meterX       = 1298.0f;
    constexpr float meterWidth   = 24.0f;

    constexpr float largeSide = 130.0f;
    constexpr float smallSide = 96.0f;

    juce::Rectangle<int> square (float centreX, float centreY, float side)
    {
        return juce::Rectangle<float> (side, side).withCentre ({ centreX, centreY }).toNearestInt();
    }

    panel::Knob::Legends gainLegends()
    {
        return { { -10.0f, "-10" }, { -5.0f, "-5" }, { 0.0f, "0" }, { 5.0f, "+5" }, { 10.0f, "+10" } };
    }

    panel::Knob::Legends trimLegends()
    {
        return { { -20.0f, "-20" }, { -10.0f, "-10" }, { 0.0f, "0" }, { 10.0f, "+10" }, { 20.0f, "+20" } };
    }
}

// ---------------------------------------------------------------------------

EmberEditor::Strip::Strip (EmberProcessor& p, int index)
    : input    (p.apvts, pid::forStrip (index, pid::input),   panel::trimAccent, Size::small, trimLegends()),
      loLevel  (p.apvts, pid::forStrip (index, pid::loGain),  panel::loAccent,   Size::large, gainLegends()),
      loFreq   (p.apvts, pid::forStrip (index, pid::loFreq),  panel::loAccent,   Size::small,
                { { 20.0f, "20" }, { 50.0f, "50" }, { 120.0f, "120" }, { 420.0f, "420" } }),
      midLevel (p.apvts, pid::forStrip (index, pid::midGain), panel::midAccent,  Size::large, gainLegends()),
      midFreq  (p.apvts, pid::forStrip (index, pid::midFreq), panel::midAccent,  Size::small,
                { { 200.0f, ".2k" }, { 600.0f, ".6k" }, { 1800.0f, "1.8k" }, { 6200.0f, "6.2k" } }),
      hiLevel  (p.apvts, pid::forStrip (index, pid::hiGain),  panel::hiAccent,   Size::large, gainLegends()),
      hiFreq   (p.apvts, pid::forStrip (index, pid::hiFreq),  panel::hiAccent,   Size::small,
                { { 5000.0f, "5k" }, { 8000.0f, "8k" }, { 14000.0f, "14k" }, { 25000.0f, "25k" } }),
      output   (p.apvts, pid::forStrip (index, pid::output),  panel::trimAccent, Size::small, trimLegends()),
      loIn     (p.apvts, pid::forStrip (index, pid::loIn),    panel::loAccent,   "IN"),
      midIn    (p.apvts, pid::forStrip (index, pid::midIn),   panel::midAccent,  "IN"),
      hiIn     (p.apvts, pid::forStrip (index, pid::hiIn),    panel::hiAccent,   "IN"),
      active   (p.apvts, pid::forStrip (index, pid::active),  juce::Colour (0xffff5a3c), "IN"),
      loDrive  (panel::loAccent),
      midDrive (panel::midAccent),
      hiDrive  (panel::hiAccent)
{
}

std::vector<juce::Component*> EmberEditor::Strip::components()
{
    // Lamps after knobs, so a button sitting in the open foot of a knob's scale
    // takes the click rather than the knob behind it.
    return { &input, &loLevel, &loFreq, &midLevel, &midFreq, &hiLevel, &hiFreq, &output,
             &loIn, &midIn, &hiIn, &active, &loDrive, &midDrive, &hiDrive, &meter };
}

// ---------------------------------------------------------------------------

EmberEditor::Face::Face (EmberProcessor& p)
    : processor (p),
      mode (p.apvts, pid::mode)
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<Strip> (p, i);

        for (auto* child : strips[(size_t) i]->components())
            addAndMakeVisible (child);
    }

    addAndMakeVisible (mode);
    refresh();
}

void EmberEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        layoutStrip (*strips[(size_t) i], rowTop[i]);

    mode.setBounds (64, 150, 160, 108);
}

void EmberEditor::Face::layoutStrip (Strip& s, float top)
{
    s.input.setBounds (square (inputCentre, top + 72.0f, smallSide));

    const std::array<std::tuple<panel::Knob*, panel::Knob*, panel::Lamp*, panel::DriveLamp*>, 3> bands {
        std::make_tuple (&s.loLevel,  &s.loFreq,  &s.loIn,  &s.loDrive),
        std::make_tuple (&s.midLevel, &s.midFreq, &s.midIn, &s.midDrive),
        std::make_tuple (&s.hiLevel,  &s.hiFreq,  &s.hiIn,  &s.hiDrive)
    };

    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto x = wellX[b];
        auto [level, frequency, in, drive] = bands[b];

        level    ->setBounds (square (x + 70.0f,  top + 80.0f, largeSide));
        frequency->setBounds (square (x + 182.0f, top + 68.0f, smallSide));
        in       ->setBounds (juce::Rectangle<float> (40.0f, 34.0f).withCentre ({ x + 182.0f, top + 128.0f }).toNearestInt());
        drive    ->setBounds (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ x + wellWidth - 18.0f, top + 15.0f }).toNearestInt());
    }

    s.output.setBounds (square (outputCentre, top + 72.0f, smallSide));
    s.active.setBounds (juce::Rectangle<float> (44.0f, 36.0f).withCentre ({ activeCentre, top + 75.0f }).toNearestInt());
    s.meter .setBounds (juce::Rectangle<float> (meterX, top + 12.0f, meterWidth, rowHeight - 24.0f).toNearestInt());
}

void EmberEditor::Face::refresh()
{
    const auto modeIndex = mode.getSelected();

    for (int i = 0; i < numChannelStrips; ++i)
    {
        auto& s = *strips[(size_t) i];

        auto& peaks = processor.meters.channels[(size_t) i];
        s.meter   .push (MeterSource::take (peaks.outputPeak));
        s.loDrive .push (MeterSource::take (peaks.bandDrive[0]));
        s.midDrive.push (MeterSource::take (peaks.bandDrive[1]));
        s.hiDrive .push (MeterSource::take (peaks.bandDrive[2]));
    }

    if (modeIndex == shownMode)
        return;

    shownMode = modeIndex;

    // In stereo mode strip A drives both channels, so B's controls would only
    // be pretending. They stay visible, greyed, and the meter and lamps on B
    // still show the right hand channel.
    const auto linked = static_cast<StripMode> (modeIndex) == StripMode::stereo;

    for (auto* child : strips[1]->components())
        if (dynamic_cast<panel::LedMeter*> (child) == nullptr && dynamic_cast<panel::DriveLamp*> (child) == nullptr)
            child->setEnabled (! linked);

    repaint();
}

void EmberEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (panel::plateTop, 0.0f, 0.0f,
                                             panel::plateBottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    // Brushed finish: faint horizontal grain, seeded so it is the same plate
    // every time it is drawn.
    juce::Random grain (0x454d42);

    for (int i = 0; i < 260; ++i)
    {
        const auto y = grain.nextFloat() * bounds.getHeight();
        const auto x = grain.nextFloat() * bounds.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.012f + 0.02f * grain.nextFloat()));
        g.drawHorizontalLine ((int) y, x, x + 80.0f + 400.0f * grain.nextFloat());
    }

    panel::drawRackEar (g, bounds.withWidth (44.0f));
    panel::drawRackEar (g, bounds.withLeft (bounds.getRight() - 44.0f));

    // Name block.
    panel::drawTrackedLabel (g, "EMBER", { 52.0f, 30.0f, 182.0f, 40.0f }, 34.0f, 9.0f);
    panel::drawTrackedLabel (g, "HARMONIC EQUALISER", { 52.0f, 74.0f, 182.0f, 14.0f }, 9.5f, 2.6f,
                             juce::Justification::centred, panel::engravingDim);

    g.setColour (panel::loAccent.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (98.0f, 96.0f, 90.0f, 2.0f));

    panel::drawTrackedLabel (g, "MODE", { 64.0f, 132.0f, 160.0f, 14.0f }, 9.0f, 2.0f,
                             juce::Justification::centredLeft, panel::engravingDim);
    panel::drawTrackedLabel (g, "INDIEWAVE AUDIO", { 52.0f, 296.0f, 182.0f, 14.0f }, 8.5f, 2.6f,
                             juce::Justification::centred, panel::engravingDim);

    // The seam between the name block and the strips, and between the strips.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (240.0f, 14.0f, 1.5f, bounds.getHeight() - 28.0f);
    g.fillRect (250.0f, 170.0f, 1092.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (241.5f, 14.0f, 1.0f, bounds.getHeight() - 28.0f);
    g.fillRect (250.0f, 171.2f, 1092.0f, 1.0f);

    for (int i = 0; i < numChannelStrips; ++i)
        paintStripLegends (g, i, rowTop[i]);
}

void EmberEditor::Face::paintStripLegends (juce::Graphics& g, int index, float top) const
{
    const auto stripMode = static_cast<StripMode> (juce::jmax (0, shownMode));
    const auto linked    = stripMode == StripMode::stereo && index == 1;

    const juce::String letter = stripMode == StripMode::midSide ? (index == 0 ? "M" : "S")
                                                                : (index == 0 ? "A" : "B");

    panel::drawTrackedLabel (g, letter, { labelCentre - 20.0f, top + 50.0f, 40.0f, 40.0f }, 30.0f, 0.0f,
                             juce::Justification::centred, linked ? panel::engravingDim : panel::engraving);

    if (linked)
        panel::drawTrackedLabel (g, "LINK A", { labelCentre - 26.0f, top + 92.0f, 52.0f, 12.0f }, 7.5f, 1.2f,
                                 juce::Justification::centred, panel::loAccent);

    const auto small = 8.5f;

    panel::drawTrackedLabel (g, "INPUT",  { inputCentre - 40.0f,  top + 104.0f, 80.0f, 12.0f }, small, 1.8f);
    panel::drawTrackedLabel (g, "OUTPUT", { outputCentre - 40.0f, top + 104.0f, 80.0f, 12.0f }, small, 1.8f);
    panel::drawTrackedLabel (g, "OUT",    { meterX - 8.0f, top + rowHeight - 12.0f, meterWidth + 16.0f, 10.0f }, 7.0f, 1.0f,
                             juce::Justification::centred, panel::engravingDim);

    const char* names[]    = { "LO", "MID", "HI" };
    const char* subtitles[] = { "SHELF", "PEAK", "SHELF" };

    for (int b = 0; b < 3; ++b)
    {
        const auto x = wellX[b];
        panel::drawWell (g, { x, top, wellWidth, rowHeight });

        panel::drawTrackedLabel (g, names[b], { x + 12.0f, top + 7.0f, 60.0f, 15.0f }, 12.0f, 2.0f,
                                 juce::Justification::centredLeft);
        panel::drawTrackedLabel (g, subtitles[b], { x + 12.0f, top + 7.0f, wellWidth - 46.0f, 15.0f }, 7.5f, 1.6f,
                                 juce::Justification::centredRight, panel::engravingDim);

        panel::drawTrackedLabel (g, "LEVEL", { x + 30.0f,  top + 124.0f, 80.0f, 12.0f }, small, 1.8f);
        panel::drawTrackedLabel (g, "FREQ",  { x + 152.0f, top + 94.0f,  60.0f, 12.0f }, 7.5f, 1.6f,
                                 juce::Justification::centred, panel::engravingDim);
    }
}

// ---------------------------------------------------------------------------

EmberEditor::EmberEditor (EmberProcessor& p)
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

EmberEditor::~EmberEditor()
{
    stopTimer();
}

void EmberEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::plateBottom);
}

void EmberEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / designWidth,
                                                     (float) getHeight() / designHeight));
}

void EmberEditor::timerCallback()
{
    face.refresh();
}

} // namespace ember
