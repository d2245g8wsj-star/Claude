#include "PluginEditor.h"

namespace beacon
{

namespace
{
    using Size = panel::Knob::Size;

    // Vertical plan, top to bottom: the header with the name and modes, then
    // two columns, A on the left and B on the right.
    constexpr float earWidth    = 34.0f;
    constexpr float headerBottom = 90.0f;

    constexpr float columnLeft[] = { 46.0f, 646.0f };
    constexpr float columnWidth  = 588.0f;
    constexpr float columnSeam   = 640.0f;

    constexpr float meterTop    = 100.0f;
    constexpr float meterHeight = 118.0f;

    constexpr float wellHeight = 196.0f;
    constexpr float compTop    = 230.0f;
    constexpr float sideTop    = compTop + wellHeight + 8.0f;
    constexpr float outTop     = sideTop + wellHeight + 8.0f;

    // Horizontal plan of the sections, from the column's left edge.
    constexpr float scWidth   = 300.0f;
    constexpr float satLeft   = 310.0f;

    constexpr float largeSide = 156.0f;
    constexpr float smallSide = 112.0f;

    // Within a well: the name along the top, small knobs with their legend in
    // the open foot of the scale and a lamp, where there is one, below; large
    // knobs a little lower, with their legend underneath.
    constexpr float smallKnobY  = 88.0f;
    constexpr float smallLabelY = 134.0f;
    constexpr float largeKnobY  = 104.0f;
    constexpr float largeLabelY = 166.0f;
    constexpr float lampY       = 170.0f;

    juce::Rectangle<int> square (float centreX, float centreY, float side)
    {
        return juce::Rectangle<float> (side, side).withCentre ({ centreX, centreY }).toNearestInt();
    }

    juce::Rectangle<int> lampAt (float centreX, float centreY)
    {
        return juce::Rectangle<float> (64.0f, 40.0f).withCentre ({ centreX, centreY }).toNearestInt();
    }

    panel::Knob::Legends trimLegends()
    {
        return { { -15.0f, "-15" }, { -5.0f, "-5" }, { 0.0f, "0" }, { 5.0f, "+5" }, { 15.0f, "+15" } };
    }
}

// ---------------------------------------------------------------------------

BeaconEditor::Follow::Follow (panel::Knob& knob, juce::RangedAudioParameter* enableParameter,
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

void BeaconEditor::Follow::begin()
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

void BeaconEditor::Follow::update()
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

void BeaconEditor::Follow::end()
{
    if (! dragging)
        return;

    for (auto& [parameter, sign] : followers)
        parameter->endChangeGesture();

    dragging = false;
}

// ---------------------------------------------------------------------------

BeaconEditor::Strip::Strip (BeaconProcessor& p, int index)
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

std::vector<juce::Component*> BeaconEditor::Strip::components()
{
    // Lamps after knobs, so a button sitting in the open foot of a knob's scale
    // takes the click rather than the knob behind it.
    return { &input, &threshold, &attack, &release, &hpf, &bell, &thd, &tube, &mix, &output, &ironPad,
             &dynLink, &feedForward, &limit, &hpfIn, &bellIn, &listen, &tubeIn, &iron, &ironLink, &active,
             &heat, &meter };
}

// ---------------------------------------------------------------------------

BeaconEditor::Face::Face (BeaconProcessor& p)
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

void BeaconEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        layoutStrip (*strips[(size_t) i], columnLeft[i]);

    mode.setBounds (420, 28, 440, 34);
}

const float BeaconEditor::Face::heatScale
    = 0.5f / (level::nominalAmplitude() * tube::ThdCalibration::get().driveFor (1.0f));

void BeaconEditor::Face::layoutStrip (Strip& s, float x)
{
    s.meter .setBounds (juce::Rectangle<float> (x + 150.0f, meterTop, 288.0f, meterHeight).toNearestInt());
    s.active.setBounds (lampAt (x + 516.0f, meterTop + meterHeight * 0.5f));

    s.input      .setBounds (square (x + 72.0f,  compTop + smallKnobY, smallSide));
    s.threshold  .setBounds (square (x + 214.0f, compTop + largeKnobY, largeSide));
    s.attack     .setBounds (square (x + 364.0f, compTop + smallKnobY, smallSide));
    s.release    .setBounds (square (x + 506.0f, compTop + smallKnobY, smallSide));
    s.dynLink    .setBounds (lampAt (x + 72.0f,  compTop + lampY));
    s.feedForward.setBounds (lampAt (x + 364.0f, compTop + lampY));
    s.limit      .setBounds (lampAt (x + 506.0f, compTop + lampY));

    s.hpf   .setBounds (square (x + 64.0f,  sideTop + smallKnobY, smallSide));
    s.bell  .setBounds (square (x + 176.0f, sideTop + smallKnobY, smallSide));
    s.hpfIn .setBounds (lampAt (x + 64.0f,  sideTop + lampY));
    s.bellIn.setBounds (lampAt (x + 176.0f, sideTop + lampY));
    s.listen.setBounds (lampAt (x + 262.0f, sideTop + lampY));

    s.thd   .setBounds (square (x + satLeft + 82.0f,  sideTop + largeKnobY, largeSide));
    s.tube  .setBounds (square (x + satLeft + 212.0f, sideTop + smallKnobY, smallSide));
    s.tubeIn.setBounds (lampAt (x + satLeft + 212.0f, sideTop + lampY));
    s.heat  .setBounds (juce::Rectangle<float> (24.0f, 24.0f)
                            .withCentre ({ x + columnWidth - 20.0f, sideTop + 18.0f }).toNearestInt());

    s.mix     .setBounds (square (x + 110.0f, outTop + smallKnobY, smallSide));
    s.output  .setBounds (square (x + 294.0f, outTop + smallKnobY, smallSide));
    s.ironPad .setBounds (square (x + 478.0f, outTop + smallKnobY, smallSide));
    s.iron    .setBounds (lampAt (x + 294.0f, outTop + lampY));
    s.ironLink.setBounds (lampAt (x + 478.0f, outTop + lampY));
}

void BeaconEditor::Face::refresh()
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

void BeaconEditor::Face::paint (juce::Graphics& g)
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

    panel::drawRackEar (g, bounds.withWidth (earWidth));
    panel::drawRackEar (g, bounds.withLeft (bounds.getRight() - earWidth));

    // Header: the name on the left, the modes in the middle, the maker on
    // the right.
    panel::drawTrackedLabel (g, "BEACON", { 56.0f, 14.0f, 300.0f, 42.0f }, 38.0f, 9.0f,
                             juce::Justification::centredLeft);
    panel::drawTrackedLabel (g, "VARI-MU TUBE COMPRESSOR", { 58.0f, 58.0f, 300.0f, 16.0f }, 11.5f, 2.4f,
                             juce::Justification::centredLeft, panel::engravingDim);

    g.setColour (panel::heatAccent.withAlpha (0.8f));
    g.fillRect (juce::Rectangle<float> (58.0f, 78.0f, 96.0f, 2.0f));

    panel::drawTrackedLabel (g, "MODE", { 340.0f, 28.0f, 70.0f, 34.0f }, panel::lampLegendHeight, 2.0f,
                             juce::Justification::centredLeft, panel::engravingDim);
    panel::drawTrackedLabel (g, "INDIEWAVE AUDIO", { 960.0f, 28.0f, 268.0f, 34.0f }, 12.0f, 3.0f,
                             juce::Justification::centredRight, panel::engravingDim);

    // The seam under the header, and between the two columns.
    const auto seams = [&g] (juce::Rectangle<float> line, bool vertical)
    {
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (line);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (vertical ? line.translated (line.getWidth(), 0.0f).withWidth (1.0f)
                             : line.translated (0.0f, line.getHeight()).withHeight (1.0f));
    };

    seams ({ earWidth + 12.0f, headerBottom, bounds.getWidth() - 2.0f * (earWidth + 12.0f), 1.2f }, false);
    seams ({ columnSeam - 0.75f, headerBottom + 10.0f, 1.5f, bounds.getHeight() - headerBottom - 20.0f }, true);

    for (int i = 0; i < numChannelStrips; ++i)
        paintStripLegends (g, i, columnLeft[i]);
}

void BeaconEditor::Face::paintStripLegends (juce::Graphics& g, int index, float x) const
{
    const auto stripMode = static_cast<StripMode> (juce::jmax (0, shownMode));
    const auto linked    = stripMode == StripMode::stereo && index == 1;

    const juce::String letter = stripMode == StripMode::midSide ? (index == 0 ? "M" : "S")
                                                                : (index == 0 ? "A" : "B");

    panel::drawTrackedLabel (g, letter, { x + 20.0f, meterTop + 20.0f, 80.0f, 56.0f }, 46.0f, 0.0f,
                             juce::Justification::centred, linked ? panel::engravingDim : panel::engraving);

    if (linked)
        panel::drawTrackedLabel (g, "LINK A", { x + 10.0f, meterTop + 80.0f, 100.0f, 16.0f }, panel::lampLegendHeight, 1.4f,
                                 juce::Justification::centred, panel::compAccent);

    const auto label = [&g] (const char* text, float centreX, float y, bool primary = false)
    {
        panel::drawTrackedLabel (g, text, { centreX - 60.0f, y, 120.0f, 16.0f }, panel::controlLabel, 1.8f,
                                 juce::Justification::centred, primary ? panel::engraving : panel::engravingDim);
    };

    struct Well { float left, top, width; const char* name; const char* subtitle; };

    for (const auto& w : { Well { x,           compTop, columnWidth,           "COMPRESSOR", "VARI-MU" },
                           Well { x,           sideTop, scWidth,               "SIDECHAIN",  "DETECTOR" },
                           Well { x + satLeft, sideTop, columnWidth - satLeft, "SATURATION", "" },
                           Well { x,           outTop,  columnWidth,           "OUTPUT",     "IRON = RED STAGE" } })
    {
        panel::drawWell (g, { w.left, w.top, w.width, wellHeight });
        panel::drawTrackedLabel (g, w.name, { w.left + 14.0f, w.top + 9.0f, 180.0f, 19.0f }, panel::sectionLabel, 2.2f,
                                 juce::Justification::centredLeft);
        panel::drawTrackedLabel (g, w.subtitle, { w.left + 14.0f, w.top + 9.0f, w.width - 28.0f, 19.0f }, 11.0f, 1.8f,
                                 juce::Justification::centredRight, panel::engravingDim);
    }

    label ("INPUT",     x + 72.0f,  compTop + smallLabelY, true);
    label ("THRESHOLD", x + 214.0f, compTop + largeLabelY, true);
    label ("ATTACK",    x + 364.0f, compTop + smallLabelY);
    label ("RELEASE",   x + 506.0f, compTop + smallLabelY);

    label ("HPF",  x + 64.0f,  sideTop + smallLabelY);
    label ("BELL", x + 176.0f, sideTop + smallLabelY);

    label ("THD",  x + satLeft + 82.0f,  sideTop + largeLabelY, true);
    label ("TUBE", x + satLeft + 212.0f, sideTop + smallLabelY);

    label ("MIX",    x + 110.0f, outTop + smallLabelY, true);
    label ("OUTPUT", x + 294.0f, outTop + smallLabelY, true);
    label ("PAD",    x + 478.0f, outTop + smallLabelY);
}

// ---------------------------------------------------------------------------

BeaconEditor::BeaconEditor (BeaconProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      face (p)
{
    addAndMakeVisible (face);
    face.setBounds (0, 0, designWidth, designHeight);

    setResizable (true, true);
    setResizeLimits (designWidth * 11 / 20, designHeight * 11 / 20, designWidth * 8 / 5, designHeight * 8 / 5);

    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) designWidth / designHeight);

    setSize (designWidth * 4 / 5, designHeight * 4 / 5);
    startTimerHz (30);
}

BeaconEditor::~BeaconEditor()
{
    stopTimer();
}

void BeaconEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::plateBottom);
}

void BeaconEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / designWidth,
                                                     (float) getHeight() / designHeight));
}

void BeaconEditor::timerCallback()
{
    face.refresh();
}

} // namespace beacon
