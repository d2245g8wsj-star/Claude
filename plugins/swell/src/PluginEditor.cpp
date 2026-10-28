#include "PluginEditor.h"

namespace swell
{

namespace
{
    constexpr float rowTop[]  = { 20.0f, 182.0f };
    constexpr float rowHeight = 138.0f;

    // Horizontal plan of a row, left to right.
    constexpr float letterCentre = 266.0f;
    constexpr float inputCentre  = 330.0f;
    constexpr float bandX[]      = { 384.0f, 644.0f, 904.0f };
    constexpr float bandWidth    = 250.0f;
    constexpr float outputCentre = 1206.0f;
    constexpr float activeCentre = 1262.0f;
    constexpr float meterX       = 1290.0f;
    constexpr float meterWidth   = 130.0f;
    constexpr float meterHeight  = 88.0f;

    constexpr float earWidth = 44.0f;

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
        return { { -20.0f, "-20" }, { 0.0f, "0" }, { 20.0f, "+20" } };
    }

    constexpr const char* bandTitle[] = { "LO  SHELF", "MID  PEAK", "HI  SHELF" };

    const char* channelLetter (StripMode mode, int strip)
    {
        switch (mode)
        {
            case StripMode::midSide: return strip == 0 ? "M" : "S";
            case StripMode::stereo:  return strip == 0 ? "L" : "R";
            case StripMode::dualMono: break;
        }

        return strip == 0 ? "A" : "B";
    }
}

// ---------------------------------------------------------------------------

SwellEditor::Strip::Strip (SwellProcessor& p, int index)
    : input    (p.apvts, pid::forStrip (index, pid::input),   panel::capTrim, 52.0f, trimLegends()),
      loLevel  (p.apvts, pid::forStrip (index, pid::loGain),  panel::capLo,   62.0f, gainLegends()),
      loFreq   (p.apvts, pid::forStrip (index, pid::loFreq),  panel::capLo,   42.0f,
                { { 20.0f, "20" }, { 60.0f, "60" }, { 140.0f, "140" }, { 420.0f, "420" } }),
      midLevel (p.apvts, pid::forStrip (index, pid::midGain), panel::capMid,  62.0f, gainLegends()),
      midFreq  (p.apvts, pid::forStrip (index, pid::midFreq), panel::capMid,  42.0f,
                { { 200.0f, ".2k" }, { 600.0f, ".6k" }, { 1800.0f, "1.8k" }, { 6200.0f, "6.2k" } }),
      hiLevel  (p.apvts, pid::forStrip (index, pid::hiGain),  panel::capHi,   62.0f, gainLegends()),
      hiFreq   (p.apvts, pid::forStrip (index, pid::hiFreq),  panel::capHi,   42.0f,
                { { 5000.0f, "5k" }, { 8000.0f, "8k" }, { 14000.0f, "14k" }, { 25000.0f, "25k" } }),
      output   (p.apvts, pid::forStrip (index, pid::output),  panel::capTrim, 52.0f, trimLegends()),
      loIn     (p.apvts, pid::forStrip (index, pid::loIn)),
      midIn    (p.apvts, pid::forStrip (index, pid::midIn)),
      hiIn     (p.apvts, pid::forStrip (index, pid::hiIn)),
      active   (p.apvts, pid::forStrip (index, pid::active))
{
}

std::vector<juce::Component*> SwellEditor::Strip::controls()
{
    return { &input, &loLevel, &loFreq, &midLevel, &midFreq, &hiLevel, &hiFreq, &output,
             &loIn, &midIn, &hiIn, &active, &loDrive, &midDrive, &hiDrive, &meter };
}

// ---------------------------------------------------------------------------

SwellEditor::Face::Face (SwellProcessor& p)
    : processor (p),
      mode (p.apvts, pid::mode, { "DUAL", "STEREO", "M-S" })
{
    // Rendered at twice the design size, so it stays sharp at the largest
    // editor size the resize limits allow.
    enamel = panel::renderPaint (designWidth * 2, designHeight * 2);

    for (int i = 0; i < numChannelStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<Strip> (p, i);

        for (auto* child : strips[(size_t) i]->controls())
            addAndMakeVisible (child);
    }

    addAndMakeVisible (mode);
    refresh();
}

void SwellEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        layoutStrip (*strips[(size_t) i], rowTop[i]);

    mode.setBounds (54, 176, 170, 104);
}

void SwellEditor::Face::layoutStrip (Strip& s, float top)
{
    const auto middle = top + rowHeight * 0.5f + 4.0f;

    s.input.setBounds (square (inputCentre, middle, 110.0f));

    const std::array<std::tuple<panel::Knob*, panel::Knob*, panel::Toggle*, panel::JewelLamp*>, 3> bands {
        std::make_tuple (&s.loLevel,  &s.loFreq,  &s.loIn,  &s.loDrive),
        std::make_tuple (&s.midLevel, &s.midFreq, &s.midIn, &s.midDrive),
        std::make_tuple (&s.hiLevel,  &s.hiFreq,  &s.hiIn,  &s.hiDrive)
    };

    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto x = bandX[b];
        auto [level, frequency, in, drive] = bands[b];

        level    ->setBounds (square (x + 66.0f,  middle, 120.0f));
        frequency->setBounds (square (x + 160.0f, middle - 6.0f, 96.0f));
        in       ->setBounds (juce::Rectangle<float> (30.0f, 70.0f).withCentre ({ x + 223.0f, middle }).toNearestInt());
        drive    ->setBounds (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ x + bandWidth - 18.0f, top + 14.0f }).toNearestInt());
    }

    s.output.setBounds (square (outputCentre, middle, 110.0f));
    s.active.setBounds (juce::Rectangle<float> (30.0f, 70.0f).withCentre ({ activeCentre, middle }).toNearestInt());
    s.meter .setBounds (juce::Rectangle<float> (meterX, middle - meterHeight * 0.5f, meterWidth, meterHeight).toNearestInt());
}

void SwellEditor::Face::refresh()
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        auto& s = *strips[(size_t) i];
        auto& source = processor.meters.channels[(size_t) i];

        s.meter   .push (MeterSource::take (source.outputPeak));
        s.loDrive .push (MeterSource::take (source.bandDrive[0]));
        s.midDrive.push (MeterSource::take (source.bandDrive[1]));
        s.hiDrive .push (MeterSource::take (source.bandDrive[2]));
    }

    const auto modeIndex = mode.getSelected();

    if (modeIndex == shownMode)
        return;

    shownMode = modeIndex;

    const auto stripMode = static_cast<StripMode> (modeIndex);

    for (int i = 0; i < numChannelStrips; ++i)
        strips[(size_t) i]->meter.setCaption (channelLetter (stripMode, i));

    // In stereo mode strip A drives both channels, so B's controls would only be
    // pretending. They stay in place, faded; B's lamps and meter still show the
    // right hand channel.
    const auto linked = stripMode == StripMode::stereo;

    for (auto* child : strips[1]->controls())
    {
        if (dynamic_cast<panel::JewelLamp*> (child) != nullptr || dynamic_cast<panel::VuMeter*> (child) != nullptr)
            continue;

        child->setEnabled (! linked);
        child->setAlpha (linked ? 0.35f : 1.0f);
    }

    repaint();
}

void SwellEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.drawImage (enamel, bounds, juce::RectanglePlacement::stretchToFit);

    panel::drawRackEar (g, bounds.withWidth (earWidth));
    panel::drawRackEar (g, bounds.withLeft (bounds.getRight() - earWidth));

    // Name block.
    panel::drawNameplate (g, { 62.0f, 26.0f, 154.0f, 62.0f });
    panel::drawLegend (g, "MODE", { 62.0f, 150.0f, 154.0f, 14.0f }, 11.0f, 2.4f);
    panel::drawLegend (g, "INDIEWAVE AUDIO", { 52.0f, 300.0f, 174.0f, 14.0f }, 9.5f, 2.6f,
                       juce::Justification::centred, panel::silkDim);

    // The rule between the name block and the strips.
    g.setColour (panel::silk.withAlpha (0.3f));
    g.drawVerticalLine (236, 20.0f, bounds.getBottom() - 20.0f);

    for (int i = 0; i < numChannelStrips; ++i)
        paintStrip (g, i, rowTop[i]);
}

void SwellEditor::Face::paintStrip (juce::Graphics& g, int index, float top) const
{
    const auto stripMode = static_cast<StripMode> (juce::jmax (0, shownMode));
    const auto linked    = stripMode == StripMode::stereo && index == 1;
    const auto middle    = top + rowHeight * 0.5f + 4.0f;

    panel::drawLegend (g, channelLetter (stripMode, index), { letterCentre - 20.0f, middle - 22.0f, 40.0f, 40.0f },
                       34.0f, 0.0f, juce::Justification::centred, linked ? panel::silkDim : panel::silk);

    if (linked)
        panel::drawLegend (g, "LINKED", { letterCentre - 26.0f, middle + 20.0f, 52.0f, 12.0f }, 8.5f, 1.4f,
                           juce::Justification::centred, panel::capMid);

    panel::drawLegend (g, "INPUT",  { inputCentre - 40.0f,  top + 2.0f, 80.0f, 14.0f }, 10.0f, 1.8f);
    panel::drawLegend (g, "OUTPUT", { outputCentre - 40.0f, top + 2.0f, 80.0f, 14.0f }, 10.0f, 1.8f);

    for (int b = 0; b < 3; ++b)
    {
        const auto x = bandX[b];
        panel::drawSection (g, { x, top + 8.0f, bandWidth, rowHeight - 8.0f }, bandTitle[b]);

        panel::drawLegend (g, "LEVEL", { x + 36.0f, top + rowHeight - 18.0f, 60.0f, 12.0f }, 9.0f, 1.6f,
                           juce::Justification::centred, panel::silkDim);
        panel::drawLegend (g, "FREQ",  { x + 130.0f, top + rowHeight - 18.0f, 60.0f, 12.0f }, 9.0f, 1.6f,
                           juce::Justification::centred, panel::silkDim);
        panel::drawLegend (g, "DRIVE", { x + bandWidth - 78.0f, top + 8.0f, 50.0f, 12.0f }, 8.0f, 1.2f,
                           juce::Justification::centredRight, panel::silkDim);
    }
}

// ---------------------------------------------------------------------------

SwellEditor::SwellEditor (SwellProcessor& p)
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

SwellEditor::~SwellEditor()
{
    stopTimer();
}

void SwellEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::paint);
}

void SwellEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / designWidth,
                                                     (float) getHeight() / designHeight));
}

void SwellEditor::timerCallback()
{
    face.refresh();
}

} // namespace swell
