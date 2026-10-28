#include "PluginEditor.h"
#include "BinaryData.h"

namespace swell
{

namespace
{
    constexpr float rowTop[]  = { 82.0f, 232.0f };
    constexpr float rowHeight = 140.0f;

    // Horizontal plan of a row, left to right: the strip's own well, the three
    // bands, then output and IN.
    constexpr float stripX  = 80.0f,   stripWidth  = 156.0f;
    constexpr float bandX[] = { 246.0f, 526.0f, 806.0f };
    constexpr float bandWidth = 270.0f;
    constexpr float outX    = 1086.0f, outWidth    = 234.0f;

    constexpr float meterX[] = { 26.0f, 1342.0f };
    constexpr float meterWidth = 32.0f;

    constexpr float largeKnob = 74.0f;
    constexpr float trimKnob  = 70.0f;
    constexpr float smallKnob = 54.0f;

    juce::Rectangle<int> square (float centreX, float centreY, float side)
    {
        return juce::Rectangle<float> (side, side).withCentre ({ centreX, centreY }).toNearestInt();
    }

    struct BandText { const char* name; const char* kind; const char* low; const char* high; };

    constexpr BandText bandText[] = {
        { "LO",  "SHELF", "20",  "420" },
        { "MID", "PEAK",  "200", "6.2k" },
        { "HI",  "SHELF", "5k",  "25k" },
    };

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

SwellEditor::Strip::Strip (SwellProcessor& p, int index, const panel::Filmstrip& large,
                           const panel::Filmstrip& small, const panel::LedImages& leds)
    : input    (p.apvts, pid::forStrip (index, pid::input),   large),
      loLevel  (p.apvts, pid::forStrip (index, pid::loGain),  large),
      loFreq   (p.apvts, pid::forStrip (index, pid::loFreq),  small),
      midLevel (p.apvts, pid::forStrip (index, pid::midGain), large),
      midFreq  (p.apvts, pid::forStrip (index, pid::midFreq), small),
      hiLevel  (p.apvts, pid::forStrip (index, pid::hiGain),  large),
      hiFreq   (p.apvts, pid::forStrip (index, pid::hiFreq),  small),
      output   (p.apvts, pid::forStrip (index, pid::output),  large),
      loIn     (p.apvts, pid::forStrip (index, pid::loIn),    "IN"),
      midIn    (p.apvts, pid::forStrip (index, pid::midIn),   "IN"),
      hiIn     (p.apvts, pid::forStrip (index, pid::hiIn),    "IN"),
      active   (p.apvts, pid::forStrip (index, pid::active),  "IN"),
      loDrive  (leds),
      midDrive (leds),
      hiDrive  (leds)
{
}

std::vector<juce::Component*> SwellEditor::Strip::controls()
{
    return { &input, &loLevel, &loFreq, &midLevel, &midFreq, &hiLevel, &hiFreq, &output,
             &loIn, &midIn, &hiIn, &active, &loDrive, &midDrive, &hiDrive };
}

// ---------------------------------------------------------------------------

SwellEditor::Face::Face (SwellProcessor& p)
    : processor (p),
      largeStrip (BinaryData::knob_large_png, BinaryData::knob_large_pngSize),
      smallStrip (BinaryData::knob_small_png, BinaryData::knob_small_pngSize),
      mode   (p.apvts, pid::mode,   "MODE"),
      bypass (p.apvts, pid::bypass, "BYPASS")
{
    // Rendered at twice the design size, so it stays sharp at the largest
    // editor size the resize limits allow.
    plate = panel::renderPlate (designWidth * 2, designHeight * 2);

    for (int i = 0; i < numChannelStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<Strip> (p, i, largeStrip, smallStrip, leds);

        for (auto* child : strips[(size_t) i]->controls())
            addAndMakeVisible (child);

        meters[(size_t) i] = std::make_unique<panel::LedMeter> (leds);
        addAndMakeVisible (*meters[(size_t) i]);
        addAndMakeVisible (peaks[(size_t) i]);
    }

    addAndMakeVisible (mode);
    addAndMakeVisible (bypass);
    refresh();
}

void SwellEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        layoutStrip (*strips[(size_t) i], rowTop[i]);

        meters[(size_t) i]->setBounds (juce::Rectangle<float> (meterX[i], 88.0f, meterWidth, 274.0f).toNearestInt());
        peaks[(size_t) i].setBounds (juce::Rectangle<float> (56.0f, 20.0f)
                                         .withCentre ({ meterX[i] + meterWidth * 0.5f, 390.0f }).toNearestInt());
    }

    mode  .setBounds (545, 396, 310, 36);
    bypass.setBounds (1196, 412, 104, 20);
}

void SwellEditor::Face::layoutStrip (Strip& s, float top)
{
    s.input.setBounds (square (stripX + 110.0f, top + 80.0f, trimKnob));

    const std::array<std::tuple<panel::Knob*, panel::Knob*, panel::Switch*, panel::DriveLamp*>, 3> bands {
        std::make_tuple (&s.loLevel,  &s.loFreq,  &s.loIn,  &s.loDrive),
        std::make_tuple (&s.midLevel, &s.midFreq, &s.midIn, &s.midDrive),
        std::make_tuple (&s.hiLevel,  &s.hiFreq,  &s.hiIn,  &s.hiDrive)
    };

    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto x = bandX[b];
        auto [level, frequency, in, drive] = bands[b];

        level    ->setBounds (square (x + 72.0f,  top + 82.0f, largeKnob));
        frequency->setBounds (square (x + 170.0f, top + 84.0f, smallKnob));
        in       ->setBounds (juce::Rectangle<float> (x + 214.0f, top + 74.0f, 44.0f, 20.0f).toNearestInt());
        drive    ->setBounds (juce::Rectangle<float> (x + bandWidth - 34.0f, top + 10.0f, 20.0f, 11.0f).toNearestInt());
    }

    s.output.setBounds (square (outX + 74.0f, top + 80.0f, trimKnob));
    s.active.setBounds (juce::Rectangle<float> (outX + 144.0f, top + 72.0f, 74.0f, 22.0f).toNearestInt());
}

void SwellEditor::Face::refresh()
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        auto& s = *strips[(size_t) i];
        auto& source = processor.meters.channels[(size_t) i];

        meters[(size_t) i]->push (MeterSource::take (source.outputPeak));
        s.loDrive .push (MeterSource::take (source.bandDrive[0]));
        s.midDrive.push (MeterSource::take (source.bandDrive[1]));
        s.hiDrive .push (MeterSource::take (source.bandDrive[2]));

        const auto hold = meters[(size_t) i]->getHoldDb();
        peaks[(size_t) i].setValue (hold <= -60.0f ? juce::String ("-inf") : juce::String (hold, 1));
    }

    const auto modeIndex = mode.getSelected();

    if (modeIndex == shownMode)
        return;

    shownMode = modeIndex;

    // In stereo mode strip A drives both channels, so B's controls would only be
    // pretending. They stay in place, faded, and B's lamps still light for the
    // right hand channel.
    const auto linked = static_cast<StripMode> (modeIndex) == StripMode::stereo;

    for (auto* child : strips[1]->controls())
    {
        if (dynamic_cast<panel::DriveLamp*> (child) != nullptr)
            continue;

        child->setEnabled (! linked);
        child->setAlpha (linked ? 0.35f : 1.0f);
    }

    repaint();
}

void SwellEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.drawImage (plate, bounds, juce::RectanglePlacement::stretchToFit);

    // Screws, each slot at its own angle the way a real panel ends up.
    panel::drawScrew (g, { 36.0f, 36.0f },     12.0f, -0.45f);
    panel::drawScrew (g, { 1364.0f, 36.0f },   12.0f,  1.15f);
    panel::drawScrew (g, { 36.0f, 434.0f },    12.0f,  0.35f);
    panel::drawScrew (g, { 1364.0f, 434.0f },  12.0f, -1.05f);

    // Header, laid out as Crest's is.
    panel::drawTrackedLabel (g, "INDIEWAVE AUDIO", { 136.0f, 26.0f, 300.0f, 16.0f }, 10.5f, 3.2f,
                             juce::Justification::centredLeft);
    panel::drawTrackedLabel (g, "SWELL", { 500.0f, 14.0f, 400.0f, 40.0f }, 30.0f, 10.0f);
    panel::drawTrackedLabel (g, "HARMONIC EQUALISER", { 964.0f, 26.0f, 300.0f, 16.0f }, 9.5f, 2.6f,
                             juce::Justification::centredRight);

    const auto stripMode = static_cast<StripMode> (juce::jmax (0, shownMode));

    for (int i = 0; i < numChannelStrips; ++i)
    {
        const auto x = meterX[i] + meterWidth * 0.5f;
        panel::drawTrackedLabel (g, channelLetter (stripMode, i), { x - 20.0f, 70.0f, 40.0f, 12.0f }, 9.5f, 1.0f);
        panel::drawTrackedLabel (g, "OUT", { x - 20.0f, 366.0f, 40.0f, 10.0f }, 7.5f, 1.2f,
                                 juce::Justification::centred, 0.6f);

        paintStrip (g, i, rowTop[i]);
    }

    // The converter details, as along the foot of Crest.
    const auto rate = processor.getSampleRate();
    const auto latency = processor.getLatencySamples();
    const auto status = juce::String (rate > 0.0 ? rate / 1000.0 : 48.0, 1) + " kHz   ·   LATENCY "
                      + juce::String (latency) + " smp   ·   4x OVERSAMPLED   ·   SWELL v"
                      + JucePlugin_VersionString;

    g.setColour (panel::ink.withAlpha (0.5f));
    g.setFont (panel::plateFont (9.0f, false));
    g.drawText (status, juce::Rectangle<float> (300.0f, 444.0f, 800.0f, 14.0f), juce::Justification::centred, false);
}

void SwellEditor::Face::paintStrip (juce::Graphics& g, int index, float top) const
{
    using panel::drawTrackedLabel;

    const auto stripMode = static_cast<StripMode> (juce::jmax (0, shownMode));
    const auto linked    = stripMode == StripMode::stereo && index == 1;
    const auto dim       = linked ? 0.35f : 0.88f;

    // The strip's own well: its letter and the input.
    panel::drawGroupWell (g, { stripX, top, stripWidth, rowHeight });
    drawTrackedLabel (g, channelLetter (stripMode, index), { stripX + 8.0f, top + 52.0f, 44.0f, 44.0f }, 34.0f, 0.0f,
                      juce::Justification::centred, linked ? 0.45f : 0.88f);

    if (linked)
        drawTrackedLabel (g, "LINKED", { stripX + 4.0f, top + 98.0f, 52.0f, 10.0f }, 7.0f, 1.0f,
                          juce::Justification::centred, 0.6f);

    drawTrackedLabel (g, "INPUT", { stripX + 70.0f, top + 28.0f, 80.0f, 12.0f }, 9.0f, 1.6f,
                      juce::Justification::centred, dim);
    drawTrackedLabel (g, "-20", { stripX + 66.0f, top + 116.0f, 30.0f, 10.0f }, 7.0f, 0.4f,
                      juce::Justification::centred, dim * 0.7f);
    drawTrackedLabel (g, "+20", { stripX + 124.0f, top + 116.0f, 30.0f, 10.0f }, 7.0f, 0.4f,
                      juce::Justification::centred, dim * 0.7f);

    for (int b = 0; b < 3; ++b)
    {
        const auto x    = bandX[b];
        const auto& txt = bandText[b];

        panel::drawGroupWell (g, { x, top, bandWidth, rowHeight });

        drawTrackedLabel (g, txt.name, { x + 14.0f, top + 8.0f, 60.0f, 15.0f }, 11.0f, 2.4f,
                          juce::Justification::centredLeft, dim);
        drawTrackedLabel (g, txt.kind, { x + 64.0f, top + 9.0f, 80.0f, 14.0f }, 7.5f, 1.6f,
                          juce::Justification::centredLeft, dim * 0.65f);

        drawTrackedLabel (g, "LEVEL", { x + 32.0f, top + 32.0f, 80.0f, 12.0f }, 8.5f, 1.6f,
                          juce::Justification::centred, dim);
        drawTrackedLabel (g, "-10", { x + 26.0f, top + 120.0f, 30.0f, 10.0f }, 7.0f, 0.4f,
                          juce::Justification::centred, dim * 0.7f);
        drawTrackedLabel (g, "+10", { x + 88.0f, top + 120.0f, 30.0f, 10.0f }, 7.0f, 0.4f,
                          juce::Justification::centred, dim * 0.7f);

        drawTrackedLabel (g, "FREQ", { x + 140.0f, top + 42.0f, 60.0f, 12.0f }, 8.5f, 1.6f,
                          juce::Justification::centred, dim);
        drawTrackedLabel (g, txt.low,  { x + 130.0f, top + 112.0f, 30.0f, 10.0f }, 7.0f, 0.4f,
                          juce::Justification::centred, dim * 0.7f);
        drawTrackedLabel (g, txt.high, { x + 180.0f, top + 112.0f, 30.0f, 10.0f }, 7.0f, 0.4f,
                          juce::Justification::centred, dim * 0.7f);
    }

    panel::drawGroupWell (g, { outX, top, outWidth, rowHeight });
    drawTrackedLabel (g, "OUTPUT", { outX + 34.0f, top + 28.0f, 80.0f, 12.0f }, 9.0f, 1.6f,
                      juce::Justification::centred, dim);
    drawTrackedLabel (g, "-20", { outX + 30.0f, top + 116.0f, 30.0f, 10.0f }, 7.0f, 0.4f,
                      juce::Justification::centred, dim * 0.7f);
    drawTrackedLabel (g, "+20", { outX + 88.0f, top + 116.0f, 30.0f, 10.0f }, 7.0f, 0.4f,
                      juce::Justification::centred, dim * 0.7f);
    drawTrackedLabel (g, "CHANNEL", { outX + 144.0f, top + 56.0f, 74.0f, 12.0f }, 8.0f, 1.6f,
                      juce::Justification::centred, dim * 0.8f);
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
    g.fillAll (panel::cream);
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
