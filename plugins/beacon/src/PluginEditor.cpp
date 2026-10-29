#include "PluginEditor.h"

namespace beacon
{

namespace
{
    using Size = panel::Knob::Size;

    // The plan: a header with the name and modes, then two rows of wells.
    // The top row is the compressor and the meter, the bottom row the
    // sidechain, saturation and output stage.
    constexpr float earWidth     = 34.0f;
    constexpr float headerBottom = 90.0f;
    constexpr float contentLeft  = 46.0f;
    constexpr float contentRight = 1134.0f;

    constexpr float wellHeight = 196.0f;
    constexpr float topRow     = 100.0f;
    constexpr float bottomRow  = topRow + wellHeight + 8.0f;

    constexpr float compLeft = contentLeft, compWidth = 588.0f;
    constexpr float meterLeft = compLeft + compWidth + 10.0f;

    constexpr float scLeft  = contentLeft,           scWidth  = 320.0f;
    constexpr float satLeft = scLeft + scWidth + 10.0f, satWidth = 300.0f;
    constexpr float outLeft = satLeft + satWidth + 10.0f;
    constexpr float outWidth = contentRight - outLeft;

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

    const juce::Identifier viewProperty { "editorView" };

    /** The parameter ID of a control in the main or the side set. */
    juce::String controlId (const char* name, bool side)
    {
        return side ? pid::side (name) : juce::String (name);
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

BeaconEditor::ChannelControls::ChannelControls (BeaconProcessor& p, bool isSide)
    : input     (p.apvts, controlId (pid::input, isSide),     panel::trimAccent, Size::small, trimLegends()),
      threshold (p.apvts, controlId (pid::threshold, isSide), panel::compAccent, Size::large,
                 { { -40.0f, "-40" }, { -30.0f, "-30" }, { -20.0f, "-20" }, { -10.0f, "-10" }, { 0.0f, "OFF" } }),
      attack    (p.apvts, controlId (pid::attack, isSide),    panel::compAccent, Size::small,
                 { { 0.5f, ".5" }, { 3.0f, "3" }, { 15.0f, "15" }, { 100.0f, "100" } }),
      release   (p.apvts, controlId (pid::release, isSide),   panel::compAccent, Size::small,
                 { { 0.1f, ".1" }, { 0.3f, ".3" }, { 1.0f, "1" }, { 3.0f, "3" } }),
      hpf       (p.apvts, controlId (pid::hpf, isSide),       panel::scAccent,   Size::small,
                 { { 20.0f, "20" }, { 60.0f, "60" }, { 160.0f, "160" }, { 500.0f, "500" } }),
      bell      (p.apvts, controlId (pid::bell, isSide),      panel::scAccent,   Size::small,
                 { { 2000.0f, "2k" }, { 4500.0f, "4.5k" }, { 9000.0f, "9k" }, { 20000.0f, "20k" } }),
      thd       (p.apvts, controlId (pid::thd, isSide),       panel::heatAccent, Size::large,
                 { { 0.0f, "0" }, { 0.5f, ".5" }, { 1.0f, "1" }, { 2.0f, "2" }, { 4.0f, "4" }, { 8.0f, "8" } }),
      tube      (p.apvts, controlId (pid::tube, isSide),      panel::heatAccent, Size::small, trimLegends()),
      mix       (p.apvts, controlId (pid::mix, isSide),       panel::trimAccent, Size::small,
                 { { 0.0f, "DRY" }, { 50.0f, "50" }, { 100.0f, "WET" } }),
      output    (p.apvts, controlId (pid::output, isSide),    panel::trimAccent, Size::small, trimLegends()),
      dynLink     (p.apvts, controlId (pid::dynLink, isSide),  panel::trimAccent,  "LINK"),
      feedForward (p.apvts, controlId (pid::feedFwd, isSide),  panel::compAccent,  "FF"),
      limit       (p.apvts, controlId (pid::limit, isSide),    panel::limitAccent, "LIMIT"),
      hpfIn       (p.apvts, controlId (pid::hpfIn, isSide),    panel::scAccent,    "IN"),
      bellIn      (p.apvts, controlId (pid::bellIn, isSide),   panel::scAccent,    "IN"),
      listen      (p.apvts, controlId (pid::listen, isSide),   panel::scAccent,    "LISTEN"),
      tubeIn      (p.apvts, controlId (pid::tubeIn, isSide),   panel::heatAccent,  "IN")
{
    const auto parameter = [&p, isSide] (const char* name) { return p.apvts.getParameter (controlId (name, isSide)); };

    links.push_back (std::make_unique<Follow> (input, parameter (pid::dynLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { parameter (pid::threshold), 1.0f },
                                                                     { parameter (pid::output),   -1.0f } }));
    links.push_back (std::make_unique<Follow> (threshold, parameter (pid::dynLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { parameter (pid::input),   1.0f },
                                                                     { parameter (pid::output), -1.0f } }));

    if (isSide)
        return;

    auto* ironParameter = p.apvts.getParameter (pid::iron);
    links.push_back (std::make_unique<Follow> (output, p.apvts.getParameter (pid::ironLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { p.apvts.getParameter (pid::ironPad), 1.0f } },
        [ironParameter] { return ironParameter->getValue() >= 0.5f; }));
}

std::vector<juce::Component*> BeaconEditor::ChannelControls::components()
{
    // Lamps after knobs, so a button sitting in the open foot of a knob's scale
    // takes the click rather than the knob behind it.
    return { &input, &threshold, &attack, &release, &hpf, &bell, &thd, &tube, &mix, &output,
             &dynLink, &feedForward, &limit, &hpfIn, &bellIn, &listen, &tubeIn };
}

// ---------------------------------------------------------------------------

BeaconEditor::Face::Face (BeaconProcessor& p)
    : processor (p),
      main (p, false),
      side (p, true),
      ironPad  (p.apvts, pid::ironPad,  panel::limitAccent, panel::Knob::Size::small,
                { { 0.0f, "0" }, { 5.0f, "5" }, { 10.0f, "10" }, { 15.0f, "15" } }),
      iron     (p.apvts, pid::iron,     panel::limitAccent, "IRON"),
      ironLink (p.apvts, pid::ironLink, panel::limitAccent, "LINK"),
      bypass   (p.apvts, pid::bypass,   panel::limitAccent, "BYPASS"),
      heat     (panel::heatAccent),
      view     ({ "MID", "SIDE" }, panel::scAccent),
      mode     (p.apvts, pid::mode)
{
    for (auto* child : main.components())
        addAndMakeVisible (child);

    for (auto* child : side.components())
        addChildComponent (child);

    for (auto* child : sharedComponents())
        addAndMakeVisible (child);

    addChildComponent (view);
    addAndMakeVisible (mode);

    // Which half of a mid/side setting is on show is kept with the session,
    // so the panel reopens where it was left.
    view.setSelected ((int) processor.apvts.state.getProperty (viewProperty, 0));
    view.onChange = [this] (int index)
    {
        processor.apvts.state.setProperty (viewProperty, index, nullptr);
        refresh();
    };

    refresh();
}

std::vector<juce::Component*> BeaconEditor::Face::sharedComponents()
{
    return { &ironPad, &iron, &ironLink, &bypass, &heat, &meter };
}

bool BeaconEditor::Face::editingSide() const
{
    return static_cast<ChannelMode> (mode.getSelected()) == ChannelMode::midSide && view.getSelected() == 1;
}

void BeaconEditor::Face::resized()
{
    mode.setBounds (380, 28, 440, 34);

    layoutChannel (main);
    layoutChannel (side);

    meter.setBounds (juce::Rectangle<float> (meterLeft + 20.0f, topRow + 14.0f, 340.0f, wellHeight - 28.0f).toNearestInt());
    view  .setBounds (juce::Rectangle<float> (112.0f, 34.0f).withCentre ({ meterLeft + 432.0f, topRow + 64.0f }).toNearestInt());
    bypass.setBounds (lampAt (meterLeft + 432.0f, topRow + 146.0f));

    heat.setBounds (juce::Rectangle<float> (24.0f, 24.0f)
                        .withCentre ({ satLeft + satWidth - 20.0f, bottomRow + 18.0f }).toNearestInt());

    const auto outStep = outWidth / 3.0f;
    ironPad .setBounds (square (outLeft + outStep * 2.5f, bottomRow + smallKnobY, smallSide));
    iron    .setBounds (lampAt (outLeft + outStep * 1.5f, bottomRow + lampY));
    ironLink.setBounds (lampAt (outLeft + outStep * 2.5f, bottomRow + lampY));
}

void BeaconEditor::Face::layoutChannel (ChannelControls& c)
{
    c.input      .setBounds (square (compLeft + 72.0f,  topRow + smallKnobY, smallSide));
    c.threshold  .setBounds (square (compLeft + 214.0f, topRow + largeKnobY, largeSide));
    c.attack     .setBounds (square (compLeft + 364.0f, topRow + smallKnobY, smallSide));
    c.release    .setBounds (square (compLeft + 506.0f, topRow + smallKnobY, smallSide));
    c.dynLink    .setBounds (lampAt (compLeft + 72.0f,  topRow + lampY));
    c.feedForward.setBounds (lampAt (compLeft + 364.0f, topRow + lampY));
    c.limit      .setBounds (lampAt (compLeft + 506.0f, topRow + lampY));

    c.hpf   .setBounds (square (scLeft + 66.0f,  bottomRow + smallKnobY, smallSide));
    c.bell  .setBounds (square (scLeft + 182.0f, bottomRow + smallKnobY, smallSide));
    c.hpfIn .setBounds (lampAt (scLeft + 66.0f,  bottomRow + lampY));
    c.bellIn.setBounds (lampAt (scLeft + 182.0f, bottomRow + lampY));
    c.listen.setBounds (lampAt (scLeft + 274.0f, bottomRow + lampY));

    c.thd   .setBounds (square (satLeft + 86.0f,  bottomRow + largeKnobY, largeSide));
    c.tube  .setBounds (square (satLeft + 228.0f, bottomRow + smallKnobY, smallSide));
    c.tubeIn.setBounds (lampAt (satLeft + 228.0f, bottomRow + lampY));

    const auto outStep = outWidth / 3.0f;
    c.mix   .setBounds (square (outLeft + outStep * 0.5f, bottomRow + smallKnobY, smallSide));
    c.output.setBounds (square (outLeft + outStep * 1.5f, bottomRow + smallKnobY, smallSide));
}

const float BeaconEditor::Face::heatScale
    = 0.5f / (level::nominalAmplitude() * tube::ThdCalibration::get().driveFor (1.0f));

void BeaconEditor::Face::refresh()
{
    const auto midSide = static_cast<ChannelMode> (mode.getSelected()) == ChannelMode::midSide;
    const auto showSide = editingSide();

    // The meter and the heater lamp follow the channel on show in mid/side;
    // otherwise they show whichever channel is working harder. The lamp is
    // just glowing at 1 % THD on the operating level, fully lit by 8 %.
    std::array<float, 2> reduction {}, drive {};

    for (size_t ch = 0; ch < reduction.size(); ++ch)
    {
        reduction[ch] = MeterSource::take (processor.meters.channels[ch].reductionDb);
        drive[ch]     = MeterSource::take (processor.meters.channels[ch].thdDrive);
    }

    const auto pick = [midSide, showSide] (const std::array<float, 2>& v)
    {
        return midSide ? v[showSide ? 1 : 0] : juce::jmax (v[0], v[1]);
    };

    meter.push (pick (reduction));
    heat.push (pick (drive) * heatScale);

    // The PAD and its link act only in RED mode.
    const auto red = iron.getToggleState();

    if (red != shownRed)
    {
        shownRed = red;
        ironPad .setEnabled (red);
        ironLink.setEnabled (red);
    }

    const auto modeIndex = mode.getSelected();

    if (modeIndex == shownMode && showSide == shownSide)
        return;

    shownMode = modeIndex;
    shownSide = showSide;

    view.setVisible (midSide);

    for (auto* child : main.components())
        child->setVisible (! showSide);

    for (auto* child : side.components())
        child->setVisible (showSide);

    meter.setLegend (midSide ? (showSide ? "SIDE  GR" : "MID  GR") : "GAIN REDUCTION");
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

    panel::drawTrackedLabel (g, "MODE", { 300.0f, 28.0f, 70.0f, 34.0f }, panel::lampLegendHeight, 2.0f,
                             juce::Justification::centredLeft, panel::engravingDim);
    panel::drawTrackedLabel (g, "INDIEWAVE AUDIO", { 860.0f, 28.0f, contentRight - 860.0f, 34.0f }, 12.0f, 3.0f,
                             juce::Justification::centredRight, panel::engravingDim);

    // The seam under the header.
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRect (juce::Rectangle<float> (contentLeft, headerBottom, contentRight - contentLeft, 1.2f));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (juce::Rectangle<float> (contentLeft, headerBottom + 1.2f, contentRight - contentLeft, 1.0f));

    const auto label = [&g] (const char* text, float centreX, float y, bool primary = false)
    {
        panel::drawTrackedLabel (g, text, { centreX - 60.0f, y, 120.0f, 16.0f }, panel::controlLabel, 1.8f,
                                 juce::Justification::centred, primary ? panel::engraving : panel::engravingDim);
    };

    // The saturation well keeps its top right corner for the heater lamp.
    struct Well { float left, top, width; const char* name; const char* subtitle; float rightInset = 14.0f; };

    // In mid/side, each section says whose controls are on show.
    const auto midSide = static_cast<ChannelMode> (juce::jmax (0, shownMode)) == ChannelMode::midSide;
    const juce::String tag = midSide ? (shownSide ? "SIDE" : "MID") : "";

    for (const auto& w : { Well { compLeft, topRow,    compWidth, "COMPRESSOR", "VARI-MU" },
                           Well { scLeft,   bottomRow, scWidth,   "SIDECHAIN",  "DETECTOR" },
                           Well { satLeft,  bottomRow, satWidth,  "SATURATION", "", 46.0f },
                           Well { outLeft,  bottomRow, outWidth,  "OUTPUT",     "IRON = RED STAGE" } })
    {
        panel::drawWell (g, { w.left, w.top, w.width, wellHeight });
        panel::drawTrackedLabel (g, w.name, { w.left + 14.0f, w.top + 9.0f, 180.0f, 19.0f }, panel::sectionLabel, 2.2f,
                                 juce::Justification::centredLeft);

        if (tag.isNotEmpty())
            panel::drawTrackedLabel (g, tag, { w.left + 14.0f, w.top + 9.0f, w.width - 14.0f - w.rightInset, 19.0f }, panel::sectionLabel, 2.2f,
                                     juce::Justification::centredRight, panel::scAccent);
        else
            panel::drawTrackedLabel (g, w.subtitle, { w.left + 14.0f, w.top + 9.0f, w.width - 28.0f, 19.0f }, 11.0f, 1.8f,
                                     juce::Justification::centredRight, panel::engravingDim);
    }

    if (midSide)
        panel::drawTrackedLabel (g, "EDIT", { meterLeft + 376.0f, topRow + 24.0f, 112.0f, 16.0f }, panel::lampLegendHeight, 2.0f,
                                 juce::Justification::centred, panel::engravingDim);

    label ("INPUT",     compLeft + 72.0f,  topRow + smallLabelY, true);
    label ("THRESHOLD", compLeft + 214.0f, topRow + largeLabelY, true);
    label ("ATTACK",    compLeft + 364.0f, topRow + smallLabelY);
    label ("RELEASE",   compLeft + 506.0f, topRow + smallLabelY);

    label ("HPF",  scLeft + 66.0f,  bottomRow + smallLabelY);
    label ("BELL", scLeft + 182.0f, bottomRow + smallLabelY);

    label ("THD",  satLeft + 86.0f,  bottomRow + largeLabelY, true);
    label ("TUBE", satLeft + 228.0f, bottomRow + smallLabelY);

    const auto outStep = outWidth / 3.0f;
    label ("MIX",    outLeft + outStep * 0.5f, bottomRow + smallLabelY, true);
    label ("OUTPUT", outLeft + outStep * 1.5f, bottomRow + smallLabelY, true);
    label ("PAD",    outLeft + outStep * 2.5f, bottomRow + smallLabelY);
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
