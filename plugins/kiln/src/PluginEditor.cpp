#include "PluginEditor.h"

namespace kiln
{

namespace
{
    using Size = panel::Knob::Size;

    constexpr float rowTop[]   = { 14.0f, 178.0f };
    constexpr float rowHeight  = 150.0f;

    // Horizontal plan of a row, left to right.
    constexpr float labelCentre  = 266.0f;
    constexpr float inputCentre  = 336.0f;

    constexpr float compX = 392.0f,  compWidth = 340.0f;
    constexpr float scX   = 742.0f,  scWidth   = 270.0f;
    constexpr float satX  = 1022.0f, satWidth  = 250.0f;
    constexpr float outX  = 1282.0f, outWidth  = 290.0f;

    constexpr float activeCentre = 1606.0f;
    constexpr float meterX       = 1640.0f;
    constexpr float meterWidth   = 148.0f;

    constexpr float largeSide = 130.0f;
    constexpr float smallSide = 96.0f;

    // Small knobs share one height, with their legend in the open foot of the
    // scale and a lamp, where there is one, below.
    constexpr float smallKnobY  = 68.0f;
    constexpr float smallLabelY = 99.0f;
    constexpr float lampY       = 128.0f;

    juce::Rectangle<int> square (float centreX, float centreY, float side)
    {
        return juce::Rectangle<float> (side, side).withCentre ({ centreX, centreY }).toNearestInt();
    }

    juce::Rectangle<int> lampAt (float centreX, float centreY)
    {
        return juce::Rectangle<float> (48.0f, 34.0f).withCentre ({ centreX, centreY }).toNearestInt();
    }

    panel::Knob::Legends trimLegends()
    {
        return { { -15.0f, "-15" }, { -5.0f, "-5" }, { 0.0f, "0" }, { 5.0f, "+5" }, { 15.0f, "+15" } };
    }
}

// ---------------------------------------------------------------------------

KilnEditor::Follow::Follow (panel::Knob& knob, juce::RangedAudioParameter* enableParameter,
                            std::vector<std::pair<juce::RangedAudioParameter*, float>> targets,
                            std::function<bool()> condition)
    : source (knob),
      enable (enableParameter),
      followers (std::move (targets)),
      extraCondition (std::move (condition))
{
    source.onDragStart   = [this] { begin(); };
    source.onValueChange = [this] { update(); };
    source.onDragEnd     = [this] { end(); };
}

void KilnEditor::Follow::begin()
{
    dragging = enable != nullptr && enable->getValue() >= 0.5f && (! extraCondition || extraCondition());

    if (! dragging)
        return;

    sourceStart = (float) source.getValue();
    followerStart.clear();

    for (auto& [parameter, sign] : followers)
    {
        followerStart.push_back (parameter->convertFrom0to1 (parameter->getValue()));
        parameter->beginChangeGesture();
    }
}

void KilnEditor::Follow::update()
{
    if (! dragging)
        return;

    const auto travel = (float) source.getValue() - sourceStart;

    for (size_t i = 0; i < followers.size(); ++i)
    {
        auto* parameter = followers[i].first;
        const auto target = parameter->getNormalisableRange().snapToLegalValue (followerStart[i] + followers[i].second * travel);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (target));
    }
}

void KilnEditor::Follow::end()
{
    if (! dragging)
        return;

    for (auto& [parameter, sign] : followers)
        parameter->endChangeGesture();

    dragging = false;
}

// ---------------------------------------------------------------------------

KilnEditor::Strip::Strip (KilnProcessor& p, int index)
    : input     (p.apvts, pid::forStrip (index, pid::input),     panel::trimAccent, Size::small, trimLegends()),
      threshold (p.apvts, pid::forStrip (index, pid::threshold), panel::compAccent, Size::large,
                 { { -40.0f, "-40" }, { -30.0f, "-30" }, { -20.0f, "-20" }, { -10.0f, "-10" }, { 0.0f, "OFF" } }),
      attack    (p.apvts, pid::forStrip (index, pid::attack),    panel::compAccent, Size::small,
                 { { 0.5f, ".5" }, { 3.0f, "3" }, { 15.0f, "15" }, { 100.0f, "100" } }),
      release   (p.apvts, pid::forStrip (index, pid::release),   panel::compAccent, Size::small,
                 { { 0.1f, ".1" }, { 0.3f, ".3" }, { 1.0f, "1" }, { 3.0f, "3" } }),
      hpf       (p.apvts, pid::forStrip (index, pid::hpf),       panel::scAccent,   Size::small,
                 { { 20.0f, "20" }, { 60.0f, "60" }, { 160.0f, "160" }, { 500.0f, "500" } }),
      bell      (p.apvts, pid::forStrip (index, pid::bell),      panel::scAccent,   Size::small,
                 { { 2000.0f, "2k" }, { 4500.0f, "4.5k" }, { 9000.0f, "9k" }, { 20000.0f, "20k" } }),
      thd       (p.apvts, pid::forStrip (index, pid::thd),       panel::heatAccent, Size::large,
                 { { 0.0f, "0" }, { 0.5f, ".5" }, { 1.0f, "1" }, { 2.0f, "2" }, { 4.0f, "4" }, { 8.0f, "8" } }),
      tube      (p.apvts, pid::forStrip (index, pid::tube),      panel::heatAccent, Size::small, trimLegends()),
      mix       (p.apvts, pid::forStrip (index, pid::mix),       panel::trimAccent, Size::small,
                 { { 0.0f, "DRY" }, { 50.0f, "50" }, { 100.0f, "WET" } }),
      output    (p.apvts, pid::forStrip (index, pid::output),    panel::trimAccent, Size::small, trimLegends()),
      ironPad   (p.apvts, pid::forStrip (index, pid::ironPad),   panel::limitAccent, Size::small,
                 { { 0.0f, "0" }, { 5.0f, "5" }, { 10.0f, "10" }, { 15.0f, "15" } }),
      dynLink     (p.apvts, pid::forStrip (index, pid::dynLink),  panel::trimAccent,  "LINK"),
      feedForward (p.apvts, pid::forStrip (index, pid::feedFwd),  panel::compAccent,  "FF"),
      limit       (p.apvts, pid::forStrip (index, pid::limit),    panel::limitAccent, "LIMIT"),
      hpfIn       (p.apvts, pid::forStrip (index, pid::hpfIn),    panel::scAccent,    "IN"),
      bellIn      (p.apvts, pid::forStrip (index, pid::bellIn),   panel::scAccent,    "IN"),
      listen      (p.apvts, pid::forStrip (index, pid::listen),   panel::scAccent,    "LISTEN"),
      tubeIn      (p.apvts, pid::forStrip (index, pid::tubeIn),   panel::heatAccent,  "IN"),
      iron        (p.apvts, pid::forStrip (index, pid::iron),     panel::limitAccent, "IRON"),
      ironLink    (p.apvts, pid::forStrip (index, pid::ironLink), panel::limitAccent, "LINK"),
      active      (p.apvts, pid::forStrip (index, pid::active),   panel::limitAccent, "IN"),
      heat        (panel::heatAccent)
{
    const auto parameter = [&p, index] (const char* name) { return p.apvts.getParameter (pid::forStrip (index, name)); };

    links.push_back (std::make_unique<Follow> (input, parameter (pid::dynLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { parameter (pid::threshold), 1.0f },
                                                                     { parameter (pid::output),   -1.0f } }));
    links.push_back (std::make_unique<Follow> (threshold, parameter (pid::dynLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { parameter (pid::input),   1.0f },
                                                                     { parameter (pid::output), -1.0f } }));

    auto* ironParameter = parameter (pid::iron);
    links.push_back (std::make_unique<Follow> (output, parameter (pid::ironLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { parameter (pid::ironPad), 1.0f } },
        [ironParameter] { return ironParameter->getValue() >= 0.5f; }));
}

std::vector<juce::Component*> KilnEditor::Strip::components()
{
    // Lamps after knobs, so a button sitting in the open foot of a knob's scale
    // takes the click rather than the knob behind it.
    return { &input, &threshold, &attack, &release, &hpf, &bell, &thd, &tube, &mix, &output, &ironPad,
             &dynLink, &feedForward, &limit, &hpfIn, &bellIn, &listen, &tubeIn, &iron, &ironLink, &active,
             &heat, &meter };
}

// ---------------------------------------------------------------------------

KilnEditor::Face::Face (KilnProcessor& p)
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

void KilnEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        layoutStrip (*strips[(size_t) i], rowTop[i]);

    mode.setBounds (64, 150, 160, 108);
}

const float KilnEditor::Face::heatScale
    = 0.5f / (level::nominalAmplitude() * tube::ThdCalibration::get().driveFor (1.0f));

void KilnEditor::Face::layoutStrip (Strip& s, float top)
{
    s.input  .setBounds (square (inputCentre, top + smallKnobY, smallSide));
    s.dynLink.setBounds (lampAt (inputCentre, top + lampY));

    s.threshold  .setBounds (square (compX + 74.0f,  top + 80.0f, largeSide));
    s.attack     .setBounds (square (compX + 194.0f, top + smallKnobY, smallSide));
    s.release    .setBounds (square (compX + 290.0f, top + smallKnobY, smallSide));
    s.feedForward.setBounds (lampAt (compX + 194.0f, top + lampY));
    s.limit      .setBounds (lampAt (compX + 290.0f, top + lampY));

    s.hpf   .setBounds (square (scX + 52.0f,  top + smallKnobY, smallSide));
    s.bell  .setBounds (square (scX + 148.0f, top + smallKnobY, smallSide));
    s.hpfIn .setBounds (lampAt (scX + 52.0f,  top + lampY));
    s.bellIn.setBounds (lampAt (scX + 148.0f, top + lampY));
    s.listen.setBounds (lampAt (scX + 230.0f, top + 82.0f));

    s.thd   .setBounds (square (satX + 74.0f,  top + 80.0f, largeSide));
    s.tube  .setBounds (square (satX + 194.0f, top + smallKnobY, smallSide));
    s.tubeIn.setBounds (lampAt (satX + 194.0f, top + lampY));
    s.heat  .setBounds (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ satX + satWidth - 18.0f, top + 15.0f }).toNearestInt());

    s.mix     .setBounds (square (outX + 52.0f,  top + smallKnobY, smallSide));
    s.output  .setBounds (square (outX + 148.0f, top + smallKnobY, smallSide));
    s.ironPad .setBounds (square (outX + 244.0f, top + smallKnobY, smallSide));
    s.iron    .setBounds (lampAt (outX + 148.0f, top + lampY));
    s.ironLink.setBounds (lampAt (outX + 244.0f, top + lampY));

    s.active.setBounds (lampAt (activeCentre, top + 75.0f));
    s.meter .setBounds (juce::Rectangle<float> (meterX, top + 16.0f, meterWidth, rowHeight - 32.0f).toNearestInt());
}

void KilnEditor::Face::refresh()
{
    const auto modeIndex = mode.getSelected();

    for (int i = 0; i < numChannelStrips; ++i)
    {
        auto& s = *strips[(size_t) i];

        auto& peaks = processor.meters.channels[(size_t) i];
        s.meter.push (MeterSource::take (peaks.reductionDb));

        // The heater lamp follows how hard the THD stage is driven: just
        // glowing at 1 % on the operating level, fully lit by 8 %.
        s.heat.push (MeterSource::take (peaks.thdDrive) * heatScale);
    }

    const auto stripMode = static_cast<StripMode> (modeIndex);

    for (int i = 0; i < numChannelStrips; ++i)
    {
        auto& s = *strips[(size_t) i];

        // In stereo mode strip A drives both channels, so B's controls would
        // only be pretending. In stereo and mid/side the output transformers
        // are wired left and right and both follow strip A, so B's output
        // stage is A's. The PAD and its link act only in RED mode. All of it
        // stays visible, greyed, and B's meter and lamp still show channel B.
        const auto stripLive  = ! (stripMode == StripMode::stereo && i == 1);
        const auto outputLive = stripMode == StripMode::dualMono || i == 0;
        const auto red        = s.iron.getToggleState();

        for (auto* child : s.components())
            if (dynamic_cast<panel::VuMeter*> (child) == nullptr && dynamic_cast<panel::DriveLamp*> (child) == nullptr)
                child->setEnabled (stripLive);

        s.iron    .setEnabled (outputLive);
        s.ironPad .setEnabled (outputLive && red);
        s.ironLink.setEnabled (outputLive && red);
    }

    if (modeIndex == shownMode)
        return;

    shownMode = modeIndex;

    const auto midSide = stripMode == StripMode::midSide;
    strips[0]->meter.setLegend (midSide ? "MID  GR" : "GAIN REDUCTION");
    strips[1]->meter.setLegend (midSide ? "SIDE  GR" : "GAIN REDUCTION");

    repaint();
}

void KilnEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (panel::plateTop, 0.0f, 0.0f,
                                             panel::plateBottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    // Brushed finish: faint horizontal grain, seeded so it is the same plate
    // every time it is drawn.
    juce::Random grain (0x4b494c);

    for (int i = 0; i < 300; ++i)
    {
        const auto y = grain.nextFloat() * bounds.getHeight();
        const auto x = grain.nextFloat() * bounds.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.012f + 0.02f * grain.nextFloat()));
        g.drawHorizontalLine ((int) y, x, x + 80.0f + 400.0f * grain.nextFloat());
    }

    panel::drawRackEar (g, bounds.withWidth (44.0f));
    panel::drawRackEar (g, bounds.withLeft (bounds.getRight() - 44.0f));

    // Name block.
    panel::drawTrackedLabel (g, "KILN", { 52.0f, 30.0f, 182.0f, 40.0f }, 34.0f, 11.0f);
    panel::drawTrackedLabel (g, "VARI-MU TUBE COMPRESSOR", { 52.0f, 74.0f, 182.0f, 14.0f }, 8.5f, 2.0f,
                             juce::Justification::centred, panel::engravingDim);

    g.setColour (panel::heatAccent.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (98.0f, 96.0f, 90.0f, 2.0f));

    panel::drawTrackedLabel (g, "MODE", { 64.0f, 132.0f, 160.0f, 14.0f }, 9.0f, 2.0f,
                             juce::Justification::centredLeft, panel::engravingDim);
    panel::drawTrackedLabel (g, "INDIEWAVE AUDIO", { 52.0f, 296.0f, 182.0f, 14.0f }, 8.5f, 2.6f,
                             juce::Justification::centred, panel::engravingDim);

    // The seam between the name block and the strips, and between the strips.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (240.0f, 14.0f, 1.5f, bounds.getHeight() - 28.0f);
    g.fillRect (250.0f, 170.0f, 1538.0f, 1.2f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (241.5f, 14.0f, 1.0f, bounds.getHeight() - 28.0f);
    g.fillRect (250.0f, 171.2f, 1538.0f, 1.0f);

    for (int i = 0; i < numChannelStrips; ++i)
        paintStripLegends (g, i, rowTop[i]);
}

void KilnEditor::Face::paintStripLegends (juce::Graphics& g, int index, float top) const
{
    const auto stripMode = static_cast<StripMode> (juce::jmax (0, shownMode));
    const auto linked    = stripMode == StripMode::stereo && index == 1;

    const juce::String letter = stripMode == StripMode::midSide ? (index == 0 ? "M" : "S")
                                                                : (index == 0 ? "A" : "B");

    panel::drawTrackedLabel (g, letter, { labelCentre - 20.0f, top + 50.0f, 40.0f, 40.0f }, 30.0f, 0.0f,
                             juce::Justification::centred, linked ? panel::engravingDim : panel::engraving);

    if (linked)
        panel::drawTrackedLabel (g, "LINK A", { labelCentre - 26.0f, top + 92.0f, 52.0f, 12.0f }, 7.5f, 1.2f,
                                 juce::Justification::centred, panel::compAccent);

    const auto small = 8.5f;
    const auto smallLabel = [&] (const char* text, float centreX, juce::Colour colour = panel::engravingDim)
    {
        panel::drawTrackedLabel (g, text, { centreX - 40.0f, top + smallLabelY, 80.0f, 12.0f }, 7.5f, 1.6f,
                                 juce::Justification::centred, colour);
    };

    smallLabel ("INPUT", inputCentre, panel::engraving);

    struct Well { float x, width; const char* name; const char* subtitle; };

    for (const auto& w : { Well { compX, compWidth, "COMPRESSOR", "VARI-MU" },
                           Well { scX,   scWidth,   "SIDECHAIN",  "DETECTOR" },
                           Well { satX,  satWidth,  "SATURATION", "THD + TUBE" },
                           Well { outX,  outWidth,  "OUTPUT",     "IRON = RED" } })
    {
        panel::drawWell (g, { w.x, top, w.width, rowHeight });
        panel::drawTrackedLabel (g, w.name, { w.x + 12.0f, top + 7.0f, 120.0f, 15.0f }, 11.0f, 2.0f,
                                 juce::Justification::centredLeft);
        panel::drawTrackedLabel (g, w.subtitle, { w.x + 12.0f, top + 7.0f, w.width - 46.0f, 15.0f }, 7.5f, 1.6f,
                                 juce::Justification::centredRight, panel::engravingDim);
    }

    panel::drawTrackedLabel (g, "THRESHOLD", { compX + 24.0f, top + 124.0f, 100.0f, 12.0f }, small, 1.8f);
    smallLabel ("ATTACK",  compX + 194.0f);
    smallLabel ("RELEASE", compX + 290.0f);

    smallLabel ("HPF",  scX + 52.0f);
    smallLabel ("BELL", scX + 148.0f);

    panel::drawTrackedLabel (g, "THD", { satX + 24.0f, top + 124.0f, 100.0f, 12.0f }, small, 1.8f);
    smallLabel ("TUBE", satX + 194.0f);

    smallLabel ("MIX",    outX + 52.0f,  panel::engraving);
    smallLabel ("OUTPUT", outX + 148.0f, panel::engraving);
    smallLabel ("PAD",    outX + 244.0f);
}

// ---------------------------------------------------------------------------

KilnEditor::KilnEditor (KilnProcessor& p)
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

KilnEditor::~KilnEditor()
{
    stopTimer();
}

void KilnEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::plateBottom);
}

void KilnEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / designWidth,
                                                     (float) getHeight() / designHeight));
}

void KilnEditor::timerCallback()
{
    face.refresh();
}

} // namespace kiln
