#include "PluginEditor.h"

namespace beacon
{

namespace ui = indiewave::ui;

namespace
{
    // The plate is matte black, the channel panels raised a shade lighter,
    // the printing white, as on the hardware this follows.
    const juce::Colour plateTop    { 0xff19191a };
    const juce::Colour plateBottom { 0xff111112 };
    const juce::Colour panelTop    { 0xff202022 };
    const juce::Colour panelBottom { 0xff161618 };
    const juce::Colour ink         { 0xfff1efe9 };
    const juce::Colour dim         { 0xff9d9b96 };
    const juce::Colour red         { 0xffff3b2f };
    const juce::Colour amber       { 0xffff8a1e };
    const juce::Colour green       { 0xff3dff6a };

    constexpr float earWidth    = 64.0f;
    constexpr float panelTopY   = 22.0f;
    constexpr float panelWidth  = 612.0f;
    constexpr float panelHeight = 406.0f;
    constexpr float firstLeft   = 90.0f;
    constexpr float centreLeft  = 714.0f;
    constexpr float centreWidth = 196.0f;
    constexpr float secondLeft  = 938.0f;

    constexpr float row1 = 108.0f, row2 = 234.0f, row3 = 358.0f, headerY = 306.0f;

    const juce::Identifier msOpenProperty { "midSideOpen" };

    juce::String controlId (const char* name, bool second)
    {
        return second ? pid::side (name) : juce::String (name);
    }

    /** The settings each channel panel has its own of. */
    const std::array<const char*, 17> perChannel {
        pid::input, pid::threshold, pid::attack, pid::release, pid::feedFwd, pid::limit,
        pid::hpf, pid::hpfIn, pid::bell, pid::bellIn, pid::listen,
        pid::thd, pid::tube, pid::tubeIn, pid::mix, pid::output, pid::dynLink
    };

    ui::RingKnob::Style knobStyle (float radius, juce::String name, juce::String lo, juce::String hi,
                                   std::function<juce::Colour (float)> colour, juce::String sub = {})
    {
        ui::RingKnob::Style s;
        s.knobRadius = radius;
        s.leds = radius > 25.0f ? 21 : 15;
        s.ledRadius = radius > 30.0f ? 2.1f : radius > 25.0f ? 1.9f : 1.7f;
        s.ledColour = std::move (colour);
        s.ink = ink;
        s.dim = dim;
        s.name = std::move (name);
        s.subName = std::move (sub);
        s.lowLegend = std::move (lo);
        s.highLegend = std::move (hi);
        s.nameHeight = radius > 25.0f ? 12.0f : 10.5f;
        s.legendHeight = radius > 25.0f ? 9.0f : 8.0f;
        return s;
    }

    std::function<juce::Colour (float)> reds()  { return [] (float t) { return t > 0.85f ? amber : red; }; }
    std::function<juce::Colour (float)> mixes() { return [] (float)   { return amber; }; }
    std::function<juce::Colour (float)> greens(){ return [] (float t) { return t > 0.9f ? juce::Colour (0xffffc23a) : green; }; }

    ui::Key::Style darkKey (juce::String below, juce::String below2 = {}, bool onLamp = true, float cap = 22.0f,
                            juce::Colour lit = juce::Colour (0xfffffdf4))
    {
        ui::Key::Style s;
        s.kind = ui::Key::Kind::dark;
        s.ink = ink;
        s.dim = dim;
        s.below = std::move (below);
        s.below2 = std::move (below2);
        s.onLamp = onLamp;
        s.capSize = cap;
        s.lit = lit;
        s.legendHeight = 8.5f;
        return s;
    }

    ui::Key::Style whiteKey (juce::String below, juce::String below2, juce::Colour lit)
    {
        ui::Key::Style s;
        s.kind = ui::Key::Kind::white;
        s.ink = ink;
        s.dim = dim;
        s.below = std::move (below);
        s.below2 = std::move (below2);
        s.lit = lit;
        s.capSize = 30.0f;
        s.legendHeight = 8.5f;
        return s;
    }

    // The ladders read in dBu, as the unit's do: 0 dBu is the operating level.
    std::vector<float> ladderSteps()
    {
        std::vector<float> steps;
        for (auto dbu : { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 12.0f, 16.0f, 20.0f, 24.0f, 26.0f })
            steps.push_back (dbu + level::dbfsAtZeroDbu);
        return steps;
    }

    juce::StringArray ladderLabels() { return { "0", "2", "4", "6", "8", "12", "16", "20", "24", "26" }; }

    juce::Rectangle<int> keyAt (float cx, float top, float w, float h)
    {
        return juce::Rectangle<float> (cx - w * 0.5f, top, w, h).toNearestInt();
    }

    float toDb (float linear) { return juce::Decibels::gainToDecibels (linear, -100.0f); }

    /** Gain reduction to scale position: 16 dB at the left, 0 at the right. */
    float reductionPosition (float db) { return 1.0f - juce::jlimit (0.0f, 16.0f, db) / 16.0f; }
}

// ---------------------------------------------------------------------------

BeaconEditor::Follow::Follow (juce::Slider& knob, juce::RangedAudioParameter* enableParameter,
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

BeaconEditor::ChannelControls::ChannelControls (BeaconProcessor& p, bool s)
    : threshold (p.apvts, controlId (pid::threshold, s), knobStyle (36.0f, "THRESHOLD", "MIN", "MAX", reds(), "0 = OFF")),
      mix       (p.apvts, controlId (pid::mix, s),       knobStyle (36.0f, "MIX", "DRY", "WET", mixes())),
      attack    (p.apvts, controlId (pid::attack, s),    knobStyle (30.0f, "ATTACK", "FAST", "SLOW", reds())),
      input     (p.apvts, controlId (pid::input, s),     knobStyle (30.0f, "INPUT", "-15", "+15", reds())),
      release   (p.apvts, controlId (pid::release, s),   knobStyle (30.0f, "RELEASE", "FAST", "SLOW", reds())),
      thd       (p.apvts, controlId (pid::thd, s),       knobStyle (20.0f, "THD", "0", "8%", reds())),
      tube      (p.apvts, controlId (pid::tube, s),      knobStyle (20.0f, "TUBE", "-15", "+15", reds())),
      output    (p.apvts, controlId (pid::output, s),    knobStyle (20.0f, "OUTPUT", "-15", "+15", reds())),
      hpf       (p.apvts, controlId (pid::hpf, s),       knobStyle (20.0f, "S.C. HPF", "20", "500", greens())),
      bell      (p.apvts, controlId (pid::bell, s),      knobStyle (20.0f, "BELL", "2k", "20k", greens())),
      limit       (p.apvts, controlId (pid::limit, s),   darkKey ("LIMIT")),
      feedForward (p.apvts, controlId (pid::feedFwd, s), darkKey ("FEED", "FORWARD")),
      dynLink     (p.apvts, controlId (pid::dynLink, s), darkKey ("LINK", {}, false, 12.0f, red)),
      tubeIn      (p.apvts, controlId (pid::tubeIn, s),  darkKey ({}, {}, false, 11.0f, red)),
      hpfIn       (p.apvts, controlId (pid::hpfIn, s),   darkKey ({}, {}, false, 11.0f, green)),
      bellIn      (p.apvts, controlId (pid::bellIn, s),  darkKey ({}, {}, false, 11.0f, green)),
      listen      (p.apvts, controlId (pid::listen, s),  darkKey ({}, {}, false, 12.0f, juce::Colour (0xfffffdf4))),
      meter (std::vector<std::pair<float, juce::String>> { { 0.0f, "16" }, { 0.375f, "10" }, { 0.6875f, "5" }, { 0.875f, "2" }, { 1.0f, "0" } },
             reductionPosition, "GAIN REDUCTION"),
      inLadder  (ladderSteps(), ladderLabels(), true,  ink),
      outLadder (ladderSteps(), ladderLabels(), false, ink)
{
    const auto parameter = [&p, s] (const char* name) { return p.apvts.getParameter (controlId (name, s)); };

    // Dynamic link: input and threshold move together, output the other way,
    // so the drive changes and the level does not.
    links.push_back (std::make_unique<Follow> (input, parameter (pid::dynLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { parameter (pid::threshold), 1.0f },
                                                                     { parameter (pid::output),   -1.0f } }));
    links.push_back (std::make_unique<Follow> (threshold, parameter (pid::dynLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { parameter (pid::input),   1.0f },
                                                                     { parameter (pid::output), -1.0f } }));

    if (s)
        return;

    // Iron link: output and pad move together in RED mode.
    auto* ironParameter = p.apvts.getParameter (pid::iron);
    links.push_back (std::make_unique<Follow> (output, p.apvts.getParameter (pid::ironLink),
        std::vector<std::pair<juce::RangedAudioParameter*, float>> { { p.apvts.getParameter (pid::ironPad), 1.0f } },
        [ironParameter] { return ironParameter->getValue() >= 0.5f; }));
}

std::vector<juce::Component*> BeaconEditor::ChannelControls::components()
{
    // Keys after knobs, so a key sitting by a knob takes the click.
    return { &meter, &inLadder, &outLadder,
             &threshold, &mix, &attack, &input, &release, &thd, &tube, &output, &hpf, &bell,
             &limit, &feedForward, &dynLink, &tubeIn, &hpfIn, &bellIn, &listen };
}

void BeaconEditor::ChannelControls::layout (float x)
{
    const auto place = [] (Knob& k, float cx, float cy, float r)
    {
        Knob::Style s;
        s.knobRadius = r;
        k.setBounds (Knob::boundsAt ({ cx, cy }, s));
    };

    place (threshold, x + 82.0f,  row1, 36.0f);
    place (mix,       x + 530.0f, row1, 36.0f);
    meter    .setBounds (juce::Rectangle<float> (x + 228.0f, 54.0f, 156.0f, 104.0f).toNearestInt());
    inLadder .setBounds (juce::Rectangle<float> (x + 170.0f, 56.0f, 44.0f, 104.0f).toNearestInt());
    outLadder.setBounds (juce::Rectangle<float> (x + 398.0f, 56.0f, 44.0f, 104.0f).toNearestInt());

    place (attack,  x + 82.0f,  row2, 30.0f);
    place (input,   x + 306.0f, row2, 30.0f);
    place (release, x + 530.0f, row2, 30.0f);
    limit      .setBounds (keyAt (x + 172.0f, row2 - 40.0f, 70.0f, 84.0f));
    feedForward.setBounds (keyAt (x + 440.0f, row2 - 40.0f, 70.0f, 94.0f));
    dynLink    .setBounds (keyAt (x + 362.0f, row2 + 46.0f, 40.0f, 32.0f));

    place (thd,    x + 58.0f,  row3, 20.0f);
    place (tube,   x + 154.0f, row3, 20.0f);
    place (output, x + 256.0f, row3, 20.0f);
    place (hpf,    x + 458.0f, row3, 20.0f);
    place (bell,   x + 554.0f, row3, 20.0f);
    tubeIn.setBounds (keyAt (x + 184.0f, row3 - 40.0f, 18.0f, 18.0f));
    hpfIn .setBounds (keyAt (x + 488.0f, row3 - 40.0f, 18.0f, 18.0f));
    bellIn.setBounds (keyAt (x + 584.0f, row3 - 40.0f, 18.0f, 18.0f));
    listen.setBounds (keyAt (x + 588.0f, headerY - 10.0f, 18.0f, 18.0f));
}

// ---------------------------------------------------------------------------

BeaconEditor::Face::Face (BeaconProcessor& p)
    : processor (p),
      main (p, false),
      second (p, true),
      ironPad  (p.apvts, pid::ironPad, knobStyle (20.0f, "IRON PAD", "0", "15", reds())),
      ironLink (p.apvts, pid::ironLink, darkKey ("LINK", {}, false, 12.0f, red)),
      iron     (p.apvts, pid::iron,     whiteKey ("IRON", "RED STAGE", red)),
      bypass   (p.apvts, pid::bypass,   whiteKey ("BYPASS", {}, juce::Colour (0xfffffdf4))),
      mode     (p.apvts, pid::mode, { "STEREO", "DUAL", "M/S" },
                { (int) ChannelMode::stereo, (int) ChannelMode::dualMono, (int) ChannelMode::midSide }, ink),
      fold     (ui::FoldTab::Edge::right, ink, juce::Colour (0xff232325))
{
    for (auto* c : main.components())   addAndMakeVisible (c);
    for (auto* c : second.components()) addChildComponent (c);

    for (auto* c : std::initializer_list<juce::Component*> { &ironPad, &ironLink, &iron, &bypass, &mode, &display })
        addAndMakeVisible (c);

    addChildComponent (fold);
    fold.onClick = [this] { foldTabClicked(); };

    open = wantsOpen();
    refresh();
}

ChannelMode BeaconEditor::Face::currentMode() const
{
    return static_cast<ChannelMode> (mode.getSelected());
}

bool BeaconEditor::Face::wantsOpen() const
{
    switch (currentMode())
    {
        case ChannelMode::midSide:  return (bool) processor.apvts.state.getProperty (msOpenProperty, true);
        case ChannelMode::dualMono: return processor.apvts.getRawParameterValue (pid::dualSplit)->load() >= 0.5f;
        case ChannelMode::stereo:   break;
    }
    return false;
}

void BeaconEditor::Face::foldTabClicked()
{
    if (currentMode() == ChannelMode::midSide)
    {
        processor.apvts.state.setProperty (msOpenProperty, ! open, nullptr);
        return;
    }

    auto* split = processor.apvts.getParameter (pid::dualSplit);
    const auto splitting = split->getValue() < 0.5f;

    // Splitting starts the right channel where the left is, so nothing moves
    // until a control is turned.
    if (splitting)
        for (auto* name : perChannel)
        {
            auto* from = processor.apvts.getParameter (name);
            auto* to   = processor.apvts.getParameter (pid::side (name));
            to->beginChangeGesture();
            to->setValueNotifyingHost (from->getValue());
            to->endChangeGesture();
        }

    split->beginChangeGesture();
    split->setValueNotifyingHost (splitting ? 1.0f : 0.0f);
    split->endChangeGesture();
}

void BeaconEditor::Face::resized()
{
    main.layout (firstLeft);
    second.layout (secondLeft);

    const auto cx = centreLeft + centreWidth * 0.5f;
    mode   .setBounds (juce::Rectangle<float> (cx - 80.0f, 42.0f, 160.0f, 60.0f).toNearestInt());
    display.setBounds (juce::Rectangle<float> (cx - 68.0f, 128.0f, 136.0f, 48.0f).toNearestInt());
    iron   .setBounds (keyAt (cx - 36.0f, 198.0f, 70.0f, 64.0f));
    bypass .setBounds (keyAt (cx + 36.0f, 198.0f, 70.0f, 64.0f));

    Knob::Style s;
    s.knobRadius = 20.0f;
    ironPad .setBounds (Knob::boundsAt ({ firstLeft + 352.0f, row3 }, s));
    ironLink.setBounds (keyAt (firstLeft + 304.0f, row3 - 14.0f, 40.0f, 32.0f));

    fold.setBounds (juce::Rectangle<float> (open ? 1553.0f : 915.0f, 120.0f, 20.0f, 210.0f).toNearestInt());
}

void BeaconEditor::Face::refresh()
{
    // Meters: folded, the panel shows whichever channel works harder; open,
    // each panel shows its own.
    std::array<float, 2> reduction {}, drive {}, in {}, out {};
    for (size_t ch = 0; ch < 2; ++ch)
    {
        auto& m = processor.meters.channels[ch];
        reduction[ch] = MeterSource::take (m.reductionDb);
        drive[ch]     = MeterSource::take (m.thdDrive);
        in[ch]        = MeterSource::take (m.inPeak);
        out[ch]       = MeterSource::take (m.outPeak);
    }

    const auto first = [this] (const std::array<float, 2>& v) { return open ? v[0] : juce::jmax (v[0], v[1]); };

    main.meter.push (first (reduction));
    main.inLadder.push (toDb (first (in)));
    main.outLadder.push (toDb (first (out)));
    second.meter.push (reduction[1]);
    second.inLadder.push (toDb (in[1]));
    second.outLadder.push (toDb (out[1]));

    // The heater lamp is just glowing at 1 % THD on the operating level and
    // fully lit by 8 %.
    static const float heatScale = 0.5f / (level::nominalAmplitude() * tube::ThdCalibration::get().driveFor (1.0f));
    const auto heatFirst = first (drive) * heatScale, heatSecond = drive[1] * heatScale;
    if (std::abs (heatFirst - heat[0]) > 0.01f || std::abs (heatSecond - heat[1]) > 0.01f)
    {
        heat[0] = heatFirst;
        heat[1] = heatSecond;
        repaint (juce::Rectangle<float> (firstLeft + 30.0f, headerY - 10.0f, 20.0f, 20.0f).toNearestInt());
        repaint (juce::Rectangle<float> (secondLeft + 30.0f, headerY - 10.0f, 20.0f, 20.0f).toNearestInt());
    }

    const auto m = currentMode();
    const auto gr = [] (float db) { return (db < 0.05f ? juce::String ("0.0") : juce::String (-db, 1)) + " dB"; };
    if (m == ChannelMode::midSide)
        display.setLines ("MID " + gr (reduction[0]), "SIDE " + gr (reduction[1]));
    else if (m == ChannelMode::dualMono)
        display.setLines ("LEFT  " + gr (reduction[0]), "RIGHT " + gr (reduction[1]));
    else
        display.setLines ("GAIN REDUCTION", gr (juce::jmax (reduction[0], reduction[1])));

    // The PAD and its link act only in RED mode.
    const auto red = iron.getToggleState();
    ironPad.setEnabled (red);
    ironLink.setEnabled (red);

    // Fold out or away when the mode or the split changes.
    const auto shouldOpen = wantsOpen();
    const auto modeIndex = mode.getSelected();

    if (shouldOpen == open && modeIndex == shownMode)
        return;

    shownMode = modeIndex;
    const auto changed = shouldOpen != open;
    open = shouldOpen;

    for (auto* c : second.components())
        c->setVisible (open);

    fold.setVisible (m != ChannelMode::stereo);
    if (m == ChannelMode::midSide) fold.setCaption ("SIDE", "FOLD");
    else                           fold.setCaption ("SPLIT L/R", "LINK L/R");
    fold.setOpen (open);

    resized();
    repaint();

    if (changed && onFoldChanged)
        onFoldChanged();
}

void BeaconEditor::Face::paintChannel (juce::Graphics& g, float x, const juce::String& title, bool isSecond)
{
    ui::drawSubpanel (g, { x, panelTopY, panelWidth, panelHeight }, 22.0f, panelTop, panelBottom);
    ui::drawLabel (g, title, { x, panelTopY + 8.0f, panelWidth, 16.0f }, 11.0f, ink, juce::Justification::centred, true, 1.2f);

    ui::drawLabel (g, "IN",  { x + 170.0f, 166.0f, 44.0f, 12.0f }, 8.5f, ink);
    ui::drawLabel (g, "OUT", { x + 398.0f, 166.0f, 44.0f, 12.0f }, 8.5f, ink);

    for (auto [text, cx] : { std::pair<const char*, float> { "SATURATION", 107.0f }, { "OUTPUT STAGE", 304.0f }, { "SIDECHAIN", 492.0f } })
        ui::drawLabel (g, text, { x + cx - 70.0f, headerY - 6.0f, 140.0f, 12.0f }, 8.5f, dim, juce::Justification::centred, true, 1.2f);

    ui::drawLabel (g, "LISTEN", { x + 530.0f, headerY - 6.0f, 46.0f, 12.0f }, 7.5f, dim, juce::Justification::centredRight, true, 0.6f);
    ui::drawLed (g, { x + 40.0f, headerY }, 3.0f, juce::Colour (0xffff7a1a), heat[isSecond ? 1 : 0]);

    // The iron works on left and right in every mode, so the second panel
    // has no pad of its own.
    if (isSecond)
    {
        ui::drawLabel (g, "IRON PAD", { x + 312.0f, row3 - 14.0f, 80.0f, 12.0f }, 8.5f, dim);
        ui::drawLabel (g, "ON THE FIRST PANEL", { x + 302.0f, row3 + 2.0f, 100.0f, 10.0f }, 6.5f, dim);
    }
}

void BeaconEditor::Face::paint (juce::Graphics& g)
{
    const auto width = (float) (open ? designWidthOpen : designWidthFolded);
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, width, (float) designHeight);

    ui::drawPlate (g, bounds, plateTop, plateBottom);
    ui::drawRackEar (g, bounds.withWidth (earWidth), true);
    ui::drawRackEar (g, bounds.withLeft (width - earWidth), false);

    const auto m = currentMode();
    juce::String firstTitle = "STEREO", secondTitle;
    if (m == ChannelMode::midSide)  { firstTitle = "MID";  secondTitle = "SIDE"; }
    if (m == ChannelMode::dualMono) { firstTitle = open ? "LEFT" : "LEFT + RIGHT"; secondTitle = "RIGHT"; }

    paintChannel (g, firstLeft, firstTitle, false);
    if (open)
        paintChannel (g, secondLeft, secondTitle, true);

    // The centre: modes, the display, the output keys, the maker.
    const auto cx = centreLeft + centreWidth * 0.5f;
    ui::drawSubpanel (g, { centreLeft, panelTopY, centreWidth, panelHeight }, 22.0f, panelTop, panelBottom);
    ui::drawLabel (g, "MODE", { centreLeft, panelTopY + 8.0f, centreWidth, 14.0f }, 9.5f, ink, juce::Justification::centred, true, 1.2f);

    const auto note = m == ChannelMode::stereo   ? "ONE SET FOR BOTH SIDES"
                    : m == ChannelMode::midSide  ? (open ? juce::String (juce::CharPointer_UTF8 ("MID LEFT · SIDE RIGHT")) : "SIDE FOLDED AWAY")
                                                 : (open ? "LEFT AND RIGHT SPLIT" : "RIGHT FOLLOWS LEFT");
    ui::drawLabel (g, note, { centreLeft, 106.0f, centreWidth, 10.0f }, 6.5f, dim, juce::Justification::centred, true, 0.6f);

    g.setColour (dim.withAlpha (0.6f));
    g.fillRect (juce::Rectangle<float> (cx - 70.0f, 120.0f, 140.0f, 0.8f));
    g.fillRect (juce::Rectangle<float> (cx - 70.0f, 188.0f, 140.0f, 0.8f));

    ui::drawLogo (g, { cx, 322.0f }, 1.05f, ink, dim);
    ui::drawLabel (g, "BEACON", { centreLeft, 370.0f, centreWidth, 20.0f }, 17.0f, ink, juce::Justification::centred, true, 4.0f);
    ui::drawLabel (g, "VARI-MU TUBE COMPRESSOR", { centreLeft, 391.0f, centreWidth, 10.0f }, 7.0f, dim, juce::Justification::centred, true, 1.3f);
}

// ---------------------------------------------------------------------------

BeaconEditor::BeaconEditor (BeaconProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      face (p)
{
    addAndMakeVisible (face);
    face.onFoldChanged = [this] { applyFold(); };

    setResizable (true, true);
    const auto width = face.isOpen() ? designWidthOpen : designWidthFolded;
    setResizeLimits (width / 2, designHeight / 2, width * 8 / 5, designHeight * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) width / designHeight);

    setSize (width * 9 / 10, designHeight * 9 / 10);
    startTimerHz (30);
}

BeaconEditor::~BeaconEditor()
{
    stopTimer();
}

void BeaconEditor::applyFold()
{
    // Keep the scale the panel is drawn at; only the width changes.
    const auto scale = (double) getHeight() / designHeight;
    const auto width = face.isOpen() ? designWidthOpen : designWidthFolded;

    setResizeLimits (width / 2, designHeight / 2, width * 8 / 5, designHeight * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) width / designHeight);

    setSize (juce::roundToInt (width * scale), getHeight());
}

void BeaconEditor::paint (juce::Graphics& g)
{
    g.fillAll (plateBottom);
}

void BeaconEditor::resized()
{
    const auto width = face.isOpen() ? designWidthOpen : designWidthFolded;
    face.setBounds (0, 0, width, designHeight);
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) width,
                                                     (float) getHeight() / (float) designHeight));
}

void BeaconEditor::timerCallback()
{
    face.refresh();
}

} // namespace beacon
