#include "PluginEditor.h"

namespace lantern
{

namespace ui = indiewave::ui;

namespace
{
    // Black plates and panels, as Beacon's, so the two sit together in a
    // rack; the band colours run warm to cool, low to high.
    const juce::Colour plateTop    { 0xff19191a };
    const juce::Colour plateBottom { 0xff111112 };
    const juce::Colour panelTop    { 0xff202022 };
    const juce::Colour panelBottom { 0xff161618 };
    const juce::Colour ink         { 0xfff1efe9 };
    const juce::Colour dim         { 0xff9d9b96 };
    const juce::Colour red         { 0xffff3b2f };
    const juce::Colour amber       { 0xffff8a1e };
    const juce::Colour green       { 0xff3dff6a };

    const std::array<juce::Colour, eq::numBands> bandColour {
        juce::Colour (0xffff4a32), juce::Colour (0xffffa020), juce::Colour (0xff3de0a0), juce::Colour (0xff4ab4ff)
    };

    const std::array<const char*, eq::numBands> bandTitle { "LOW", "LOW MID", "HIGH MID", "HIGH" };
    const std::array<std::pair<const char*, const char*>, eq::numBands> bandSpan {
        { { "22", "1k" }, { "90", "4.8k" }, { "260", "11k" }, { "600", "28k" } }
    };

    constexpr float earWidth    = 64.0f;
    constexpr float panelTopY   = 22.0f;
    constexpr float panelWidth  = 660.0f;
    constexpr float panelHeight = 426.0f;
    constexpr float firstLeft   = 90.0f;
    constexpr float centreLeft  = 774.0f;
    constexpr float centreWidth = 196.0f;
    constexpr float secondLeft  = 998.0f;

    constexpr float freqRow = 120.0f, gainRow = 252.0f, row3 = 388.0f, headerY = 334.0f;
    constexpr std::array<float, eq::numBands> bandX { 90.0f, 250.0f, 410.0f, 570.0f };

    const juce::Identifier msOpenProperty { "midSideOpen" };

    juce::String controlId (const juce::String& name, bool second)
    {
        return second ? pid::side (name) : name;
    }

    ui::RingKnob::Style knobStyle (float radius, juce::String name, juce::String lo, juce::String hi,
                                   std::function<juce::Colour (float)> colour, int leds = 0, bool bipolar = false)
    {
        ui::RingKnob::Style s;
        s.knobRadius = radius;
        s.leds = leds > 0 ? leds : radius > 25.0f ? 21 : 15;
        s.ledRadius = radius > 30.0f ? 2.1f : radius > 25.0f ? 1.9f : 1.7f;
        s.ledColour = std::move (colour);
        s.bipolar = bipolar;
        s.ink = ink;
        s.dim = dim;
        s.name = std::move (name);
        s.lowLegend = std::move (lo);
        s.highLegend = std::move (hi);
        s.nameHeight = radius > 25.0f ? 12.0f : 10.5f;
        s.legendHeight = radius > 25.0f ? 9.0f : 8.0f;
        return s;
    }

    std::function<juce::Colour (float)> solid (juce::Colour c) { return [c] (float) { return c; }; }
    std::function<juce::Colour (float)> reds() { return [] (float t) { return t > 0.85f ? amber : red; }; }

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

    std::vector<float> ladderSteps()
    {
        std::vector<float> steps;
        for (auto dbu : { 0.0f, 4.0f, 8.0f, 12.0f, 16.0f, 20.0f, 24.0f, 26.0f })
            steps.push_back (dbu + level::dbfsAtZeroDbu);
        return steps;
    }

    juce::StringArray ladderLabels() { return { "0", "4", "8", "12", "16", "20", "24", "26" }; }

    juce::Rectangle<int> keyAt (float cx, float top, float w, float h)
    {
        return juce::Rectangle<float> (cx - w * 0.5f, top, w, h).toNearestInt();
    }

    void place (ui::RingKnob& k, float cx, float cy, float r)
    {
        ui::RingKnob::Style s;
        s.knobRadius = r;
        k.setBounds (ui::RingKnob::boundsAt ({ cx, cy }, s));
    }
}

// ---------------------------------------------------------------------------

LanternEditor::Follow::Follow (juce::Slider& knob, juce::RangedAudioParameter* enableParameter,
                               juce::RangedAudioParameter* target, std::function<bool()> condition)
    : source (knob), enable (enableParameter), follower (target), extraCondition (std::move (condition))
{
    source.onDragStart = [this]
    {
        dragging = enable->getValue() >= 0.5f && extraCondition();
        if (! dragging)
            return;

        sourceStart = (float) source.getValue();
        followerStart = follower->convertFrom0to1 (follower->getValue());
        follower->beginChangeGesture();
    };

    source.onValueChange = [this]
    {
        if (! dragging)
            return;

        const auto value = follower->getNormalisableRange().snapToLegalValue (followerStart + (float) source.getValue() - sourceStart);
        follower->setValueNotifyingHost (follower->convertTo0to1 (value));
    };

    source.onDragEnd = [this]
    {
        if (dragging)
            follower->endChangeGesture();
        dragging = false;
    };
}

// ---------------------------------------------------------------------------

float LanternEditor::Curve::frequencyAt (int point) noexcept
{
    return 20.0f * std::pow (1000.0f, (float) point / (float) (points - 1));   // 20 Hz to 20 kHz
}

void LanternEditor::Curve::setCurves (std::vector<float> a, std::vector<float> b, float spanDb)
{
    if (a == first && b == second && std::abs (spanDb - span) < 1.0e-3f)
        return;

    first = std::move (a);
    second = std::move (b);
    span = spanDb;
    repaint();
}

void LanternEditor::Curve::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff070807));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

    const auto plot = r.reduced (6.0f, 6.0f);
    const auto xFor = [plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const auto yFor = [plot, this] (float db) { return plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / span); };

    const auto glow = juce::Colour (0xff3dff9a);
    g.setColour (glow.withAlpha (0.10f));
    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) xFor (hz), plot.getY(), plot.getBottom());
    for (auto fraction : { -0.5f, 0.5f })
        g.drawHorizontalLine ((int) yFor (fraction * span), plot.getX(), plot.getRight());
    g.setColour (glow.withAlpha (0.22f));
    g.drawHorizontalLine ((int) yFor (0.0f), plot.getX(), plot.getRight());

    const auto trace = [&] (const std::vector<float>& curve, juce::Colour colour)
    {
        if (curve.size() != (size_t) points)
            return;

        juce::Path p;
        for (int i = 0; i < points; ++i)
        {
            const juce::Point<float> at { xFor (frequencyAt (i)), yFor (curve[(size_t) i]) };
            if (i == 0) p.startNewSubPath (at); else p.lineTo (at);
        }

        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    trace (second, juce::Colour (0xffffb03a));
    trace (first, glow);

    ui::drawLabel (g, juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (juce::roundToInt (span)) + " dB", r.reduced (5.0f, 3.0f).removeFromTop (9.0f),
                   6.5f, glow.withAlpha (0.55f), juce::Justification::topRight, true, 0.4f);
}

// ---------------------------------------------------------------------------

LanternEditor::BandControls::BandControls (LanternProcessor& p, int b, bool s)
{
    const auto colour = bandColour[(size_t) b];
    const auto outer = pid::isOuter (b);

    freq = std::make_unique<Knob> (p.apvts, controlId (pid::band (b, "freq"), s),
                                   knobStyle (24.0f, "FREQ", bandSpan[(size_t) b].first, bandSpan[(size_t) b].second,
                                              solid (colour), eq::positions));
    gain = std::make_unique<Knob> (p.apvts, controlId (pid::band (b, "gain"), s),
                                   knobStyle (30.0f, "GAIN", "CUT", "BOOST", solid (colour), 21, true));
    in = std::make_unique<Key> (p.apvts, controlId (pid::band (b, "in"), s), darkKey ({}, {}, false, 11.0f, colour));
    variant = std::make_unique<Key> (p.apvts, controlId (pid::band (b, outer ? "bell" : "hiq"), s),
                                     darkKey (outer ? "BELL" : "HI-Q", {}, true, 16.0f));
}

LanternEditor::ChannelControls::ChannelControls (LanternProcessor& p, bool s)
    : hpf    (p.apvts, controlId (pid::hpf, s),    knobStyle (20.0f, "HPF", "12", "300", solid (green))),
      lpf    (p.apvts, controlId (pid::lpf, s),    knobStyle (20.0f, "LPF", "6k", "50k", solid (green))),
      thd    (p.apvts, controlId (pid::thd, s),    knobStyle (20.0f, "THD", "0", "100", reds())),
      output (p.apvts, controlId (pid::output, s), knobStyle (20.0f, "OUTPUT", "-15", "+15", reds())),
      hpfIn    (p.apvts, controlId (pid::hpfIn, s),    darkKey ({}, {}, false, 11.0f, green)),
      hpfSteep (p.apvts, controlId (pid::hpfSteep, s), darkKey ({}, {}, false, 11.0f, juce::Colour (0xfffffdf4))),
      lpfIn    (p.apvts, controlId (pid::lpfIn, s),    darkKey ({}, {}, false, 11.0f, green)),
      lpfSteep (p.apvts, controlId (pid::lpfSteep, s), darkKey ({}, {}, false, 11.0f, juce::Colour (0xfffffdf4))),
      outLadder (ladderSteps(), ladderLabels(), true, ink)
{
    for (int b = 0; b < eq::numBands; ++b)
        bands.push_back (std::make_unique<BandControls> (p, b, s));
}

std::vector<juce::Component*> LanternEditor::ChannelControls::components()
{
    // Keys after knobs, so a key sitting by a knob takes the click.
    std::vector<juce::Component*> list { &outLadder, &hpf, &lpf, &thd, &output };
    for (auto& b : bands)
    {
        list.push_back (b->freq.get());
        list.push_back (b->gain.get());
    }
    for (auto& b : bands)
    {
        list.push_back (b->in.get());
        list.push_back (b->variant.get());
    }
    for (auto* k : { &hpfIn, &hpfSteep, &lpfIn, &lpfSteep })
        list.push_back (k);
    return list;
}

void LanternEditor::ChannelControls::layout (float x)
{
    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto cx = x + bandX[b];
        place (*bands[b]->freq, cx, freqRow, 24.0f);
        place (*bands[b]->gain, cx, gainRow, 30.0f);
        bands[b]->in->setBounds (keyAt (cx + 40.0f, freqRow - 54.0f, 18.0f, 18.0f));
        bands[b]->variant->setBounds (keyAt (cx + 58.0f, freqRow + 52.0f, 40.0f, 56.0f));
    }

    place (hpf,    x + 62.0f,  row3, 20.0f);
    place (lpf,    x + 170.0f, row3, 20.0f);
    place (thd,    x + 300.0f, row3, 20.0f);
    place (output, x + 412.0f, row3, 20.0f);

    hpfSteep.setBounds (keyAt (x + 62.0f - 30.0f,  row3 - 40.0f, 18.0f, 18.0f));
    hpfIn   .setBounds (keyAt (x + 62.0f + 30.0f,  row3 - 40.0f, 18.0f, 18.0f));
    lpfSteep.setBounds (keyAt (x + 170.0f - 30.0f, row3 - 40.0f, 18.0f, 18.0f));
    lpfIn   .setBounds (keyAt (x + 170.0f + 30.0f, row3 - 40.0f, 18.0f, 18.0f));

    outLadder.setBounds (juce::Rectangle<float> (x + 596.0f, headerY + 4.0f, 44.0f, 84.0f).toNearestInt());
}

// ---------------------------------------------------------------------------

LanternEditor::Face::Face (LanternProcessor& p)
    : processor (p),
      main (p, false),
      second (p, true),
      ironPad   (p.apvts, pid::ironPad, knobStyle (20.0f, "IRON PAD", "0", "15", reds())),
      ironLink  (p.apvts, pid::ironLink, darkKey ("LINK", {}, false, 12.0f, red)),
      iron      (p.apvts, pid::iron,     whiteKey ("IRON", "RED STAGE", red)),
      bypass    (p.apvts, pid::bypass,   whiteKey ("BYPASS", {}, juce::Colour (0xfffffdf4))),
      wide      (p.apvts, pid::gainRange, darkKey ("RANGE", juce::String (juce::CharPointer_UTF8 ("\xc2\xb1" "15 DB")), true, 20.0f)),
      constantQ (p.apvts, pid::qMode,     darkKey ("CONST", "Q", true, 20.0f)),
      mode      (p.apvts, pid::mode, { "STEREO", "DUAL", "M/S" },
                 { (int) ChannelMode::stereo, (int) ChannelMode::dualMono, (int) ChannelMode::midSide }, ink),
      fold      (ui::FoldTab::Edge::right, ink, juce::Colour (0xff232325))
{
    for (auto* c : main.components())   addAndMakeVisible (c);
    for (auto* c : second.components()) addChildComponent (c);

    for (auto* c : std::initializer_list<juce::Component*> { &ironPad, &ironLink, &iron, &bypass, &wide, &constantQ, &mode, &curve })
        addAndMakeVisible (c);

    addChildComponent (fold);
    fold.onClick = [this] { foldTabClicked(); };

    auto* ironParameter = p.apvts.getParameter (pid::iron);
    ironFollow = std::make_unique<Follow> (main.output, p.apvts.getParameter (pid::ironLink), p.apvts.getParameter (pid::ironPad),
                                           [ironParameter] { return ironParameter->getValue() >= 0.5f; });

    open = wantsOpen();
    refresh();
}

ChannelMode LanternEditor::Face::currentMode() const
{
    return static_cast<ChannelMode> (mode.getSelected());
}

bool LanternEditor::Face::wantsOpen() const
{
    switch (currentMode())
    {
        case ChannelMode::midSide:  return (bool) processor.apvts.state.getProperty (msOpenProperty, true);
        case ChannelMode::dualMono: return processor.apvts.getRawParameterValue (pid::dualSplit)->load() >= 0.5f;
        case ChannelMode::stereo:   break;
    }
    return false;
}

void LanternEditor::Face::foldTabClicked()
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
        for (const auto& name : pid::perChannel())
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

void LanternEditor::Face::resized()
{
    main.layout (firstLeft);
    second.layout (secondLeft);

    const auto cx = centreLeft + centreWidth * 0.5f;
    mode     .setBounds (juce::Rectangle<float> (cx - 80.0f, 42.0f, 160.0f, 60.0f).toNearestInt());
    curve    .setBounds (juce::Rectangle<float> (cx - 80.0f, 124.0f, 160.0f, 92.0f).toNearestInt());
    wide     .setBounds (keyAt (cx - 40.0f, 222.0f, 70.0f, 76.0f));
    constantQ.setBounds (keyAt (cx + 40.0f, 222.0f, 70.0f, 76.0f));
    iron     .setBounds (keyAt (cx - 36.0f, 302.0f, 70.0f, 64.0f));
    bypass   .setBounds (keyAt (cx + 36.0f, 302.0f, 70.0f, 64.0f));

    ui::RingKnob::Style s;
    s.knobRadius = 20.0f;
    ironPad .setBounds (ui::RingKnob::boundsAt ({ firstLeft + 516.0f, row3 }, s));
    ironLink.setBounds (keyAt (firstLeft + 464.0f, row3 - 14.0f, 40.0f, 32.0f));

    fold.setBounds (juce::Rectangle<float> (open ? (secondLeft + panelWidth + 3.0f) : (centreLeft + centreWidth + 5.0f),
                                            130.0f, 20.0f, 210.0f).toNearestInt());
}

std::vector<float> LanternEditor::Face::curveOf (bool secondSet) const
{
    const auto settings = processor.settingsFor (secondSet);
    std::vector<float> response ((size_t) Curve::points);
    for (int i = 0; i < Curve::points; ++i)
        response[(size_t) i] = (float) eq::responseDb (settings, Curve::frequencyAt (i));
    return response;
}

void LanternEditor::Face::refresh()
{
    std::array<float, 2> drive {}, out {};
    for (size_t ch = 0; ch < 2; ++ch)
    {
        auto& m = processor.meters.channels[ch];
        MeterSource::take (m.reductionDb);
        MeterSource::take (m.inPeak);
        drive[ch] = MeterSource::take (m.thdDrive);
        out[ch]   = MeterSource::take (m.outPeak);
    }

    const auto toDb = [] (float linear) { return juce::Decibels::gainToDecibels (linear, -100.0f); };
    const auto first = [this] (const std::array<float, 2>& v) { return open ? v[0] : juce::jmax (v[0], v[1]); };
    main.outLadder.push (toDb (first (out)));
    second.outLadder.push (toDb (out[1]));

    // The heater lamp is just glowing at 1 % THD on the operating level.
    static const float heatScale = 0.5f / (level::nominalAmplitude() * tube::ThdCalibration::get().driveFor (1.0f));
    const auto heatFirst = first (drive) * heatScale, heatSecond = drive[1] * heatScale;
    if (std::abs (heatFirst - heat[0]) > 0.01f || std::abs (heatSecond - heat[1]) > 0.01f)
    {
        heat[0] = heatFirst;
        heat[1] = heatSecond;
        repaint (juce::Rectangle<float> (firstLeft + 250.0f, headerY - 10.0f, 20.0f, 20.0f).toNearestInt());
        repaint (juce::Rectangle<float> (secondLeft + 250.0f, headerY - 10.0f, 20.0f, 20.0f).toNearestInt());
    }

    const auto wideRange = wide.getToggleState();
    curve.setCurves (curveOf (false), open ? curveOf (true) : std::vector<float> {}, wideRange ? 16.0f : 6.0f);

    const auto red = iron.getToggleState();
    ironPad.setEnabled (red);
    ironLink.setEnabled (red);

    const auto shouldOpen = wantsOpen();
    const auto modeIndex = mode.getSelected();
    const auto m = currentMode();

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

void LanternEditor::Face::paintChannel (juce::Graphics& g, float x, const juce::String& title, bool isSecond)
{
    ui::drawSubpanel (g, { x, panelTopY, panelWidth, panelHeight }, 22.0f, panelTop, panelBottom);
    ui::drawLabel (g, title, { x, panelTopY + 8.0f, panelWidth, 16.0f }, 11.0f, ink, juce::Justification::centred, true, 1.2f);

    for (size_t b = 0; b < (size_t) eq::numBands; ++b)
    {
        const auto cx = x + bandX[b];
        ui::drawLabel (g, bandTitle[b], { cx - 60.0f, 54.0f, 100.0f, 12.0f }, 9.0f, bandColour[b], juce::Justification::centred, true, 1.2f);
        ui::drawLabel (g, "IN", { cx + 50.0f, 57.0f, 20.0f, 10.0f }, 7.0f, dim, juce::Justification::centredLeft, true, 0.4f);

        // A divider between the columns.
        if (b > 0)
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (cx - 80.0f, 60.0f, 1.0f, 240.0f));
            g.setColour (juce::Colours::white.withAlpha (0.04f));
            g.fillRect (juce::Rectangle<float> (cx - 79.0f, 60.0f, 1.0f, 240.0f));
        }
    }

    g.setColour (dim.withAlpha (0.35f));
    g.fillRect (juce::Rectangle<float> (x + 24.0f, headerY - 18.0f, panelWidth - 48.0f, 0.8f));

    for (auto [text, cx] : { std::pair<const char*, float> { "FILTERS", 116.0f }, { "TUBE", 300.0f }, { "OUTPUT STAGE", 464.0f } })
        ui::drawLabel (g, text, { x + cx - 70.0f, headerY - 6.0f, 140.0f, 12.0f }, 8.5f, dim, juce::Justification::centred, true, 1.2f);

    for (auto cx : { 62.0f, 170.0f })
    {
        ui::drawLabel (g, "24", { x + cx - 58.0f, row3 - 36.0f, 16.0f, 10.0f }, 7.0f, dim, juce::Justification::centredRight, true);
        ui::drawLabel (g, "IN", { x + cx + 42.0f, row3 - 36.0f, 16.0f, 10.0f }, 7.0f, dim, juce::Justification::centredLeft, true);
    }

    ui::drawLed (g, { x + 260.0f, headerY }, 3.0f, juce::Colour (0xffff7a1a), heat[isSecond ? 1 : 0]);
    ui::drawLabel (g, "OUT", { x + 596.0f, row3 + 36.0f, 44.0f, 10.0f }, 7.5f, ink);

    if (isSecond)
    {
        ui::drawLabel (g, "IRON PAD", { x + 476.0f, row3 - 14.0f, 80.0f, 12.0f }, 8.5f, dim);
        ui::drawLabel (g, "ON THE FIRST PANEL", { x + 466.0f, row3 + 2.0f, 100.0f, 10.0f }, 6.5f, dim);
    }
}

void LanternEditor::Face::paint (juce::Graphics& g)
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

    const auto cx = centreLeft + centreWidth * 0.5f;
    ui::drawSubpanel (g, { centreLeft, panelTopY, centreWidth, panelHeight }, 22.0f, panelTop, panelBottom);
    ui::drawLabel (g, "MODE", { centreLeft, panelTopY + 8.0f, centreWidth, 14.0f }, 9.5f, ink, juce::Justification::centred, true, 1.2f);

    const auto note = m == ChannelMode::stereo   ? "ONE SET FOR BOTH SIDES"
                    : m == ChannelMode::midSide  ? (open ? juce::String (juce::CharPointer_UTF8 ("MID GREEN \xc2\xb7 SIDE AMBER")) : "SIDE FOLDED AWAY")
                                                 : (open ? "LEFT GREEN, RIGHT AMBER" : "RIGHT FOLLOWS LEFT");
    ui::drawLabel (g, note, { centreLeft, 106.0f, centreWidth, 10.0f }, 6.5f, dim, juce::Justification::centred, true, 0.6f);

    ui::drawLogo (g, { cx, 390.0f }, 0.7f, ink, dim, false);
    ui::drawLabel (g, "LANTERN", { centreLeft, 409.0f, centreWidth, 20.0f }, 17.0f, ink, juce::Justification::centred, true, 4.0f);
    ui::drawLabel (g, "PASSIVE TUBE EQUALISER", { centreLeft, 429.0f, centreWidth, 10.0f }, 7.0f, dim, juce::Justification::centred, true, 1.3f);
}

// ---------------------------------------------------------------------------

LanternEditor::LanternEditor (LanternProcessor& p)
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

LanternEditor::~LanternEditor()
{
    stopTimer();
}

void LanternEditor::applyFold()
{
    const auto scale = (double) getHeight() / designHeight;
    const auto width = face.isOpen() ? designWidthOpen : designWidthFolded;

    setResizeLimits (width / 2, designHeight / 2, width * 8 / 5, designHeight * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) width / designHeight);

    setSize (juce::roundToInt (width * scale), getHeight());
}

void LanternEditor::paint (juce::Graphics& g)
{
    g.fillAll (plateBottom);
}

void LanternEditor::resized()
{
    const auto width = face.isOpen() ? designWidthOpen : designWidthFolded;
    face.setBounds (0, 0, width, designHeight);
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) width,
                                                     (float) getHeight() / (float) designHeight));
}

void LanternEditor::timerCallback()
{
    face.refresh();
}

} // namespace lantern
