#include "PluginEditor.h"

namespace hinge
{

namespace ui = indiewave::ui;

namespace
{
    // Sand enamel with black screen printing; the low shelf's scales are
    // printed in red, the high shelf's in blue, the dynamics' in olive.
    const juce::Colour plateTop    { 0xffe6e0d1 };
    const juce::Colour plateBottom { 0xffd2cbba };
    const juce::Colour ink         { 0xff26231f };
    const juce::Colour dim         { 0xff6a6357 };
    const juce::Colour frame       { 0xff8f887a };
    const juce::Colour lowInk      { 0xffb44a2c };
    const juce::Colour highInk     { 0xff2f6f9a };
    const juce::Colour dynInk      { 0xff6f7a2a };
    const juce::Colour amber       { 0xfff2a65a };

    constexpr float earWidth = 64.0f;
    constexpr float rowCentres[] = { 98.0f, 256.0f };

    // Horizontal plan of a row, left to right.
    constexpr float letterX = 280.0f;
    constexpr float lowX = 312.0f, highX = 680.0f, toneW = 352.0f, dynX = 1048.0f, dynW = 300.0f;
    constexpr float lowCutX = 366.0f, lowShelfX = 462.0f, lowLevelX = 580.0f;
    constexpr float highLevelX = 762.0f, highShelfX = 880.0f, highCutX = 976.0f;
    constexpr float thresholdX = 1104.0f, lowDynX = 1198.0f, highDynX = 1292.0f;
    constexpr float outputX = 1416.0f, engageX = 1482.0f, meterX = 1530.0f;

    std::vector<std::pair<float, juce::String>> positions (std::initializer_list<const char*> names)
    {
        std::vector<std::pair<float, juce::String>> out;
        float index = 0.0f;
        for (auto* name : names)
            out.push_back ({ index++, name });
        return out;
    }

    std::vector<std::pair<float, juce::String>> levelLegends()
    {
        return { { -5.0f, "-5" }, { 0.0f, "0" }, { 5.0f, "+5" } };
    }

    std::vector<std::pair<float, juce::String>> outputLegends()
    {
        auto at = [] (int step)
        {
            const auto db = juce::Decibels::gainToDecibels (network::outputGain (step));
            return std::pair { (float) step, (db > 0.05 ? "+" : "") + juce::String (db, std::abs (db) < 0.05 ? 0 : 1) };
        };
        return { at (-10), at (0), at (10) };
    }

    ui::BakeliteKnob::Style knob (float radius, juce::Colour dots, std::vector<std::pair<float, juce::String>> legends,
                                  juce::String name)
    {
        ui::BakeliteKnob::Style s;
        s.radius = radius;
        s.dotColour = dots;
        s.ink = ink;
        s.dim = dim;
        s.legends = std::move (legends);
        s.legendRadius = radius + (radius > 30.0f ? 15.0f : 12.0f);
        s.legendHeight = radius > 30.0f ? 8.0f : 7.0f;
        s.name = std::move (name);
        s.nameHeight = 8.5f;
        s.nameGap = radius > 30.0f ? 12.0f : 10.0f;
        return s;
    }

    ui::Key::Style modeKey (juce::String text)
    {
        ui::Key::Style s;
        s.kind = ui::Key::Kind::white;
        s.lit = amber;
        s.ink = ink;
        s.dim = dim;
        s.capSize = 20.0f;
        s.capWidth = 48.0f;
        s.capLabel = std::move (text);
        s.capLabelOn = juce::Colour (0xff2b261f);
        s.capLabelOff = juce::Colour (0xff2b261f);
        return s;
    }

    float toDb (float linear) { return juce::Decibels::gainToDecibels (linear, -100.0f); }
}

// ---------------------------------------------------------------------------

HingeEditor::Strip::Strip (HingeProcessor& p, int index)
    : lowCut    (p.apvts, pid::forStrip (index, pid::lowCut),    knob (21.0f, lowInk,
                 positions ({ "OUT", "12", "18", "24", "30", "36", "43", "54" }), "CUT Hz")),
      lowShelf  (p.apvts, pid::forStrip (index, pid::lowShelf),  knob (21.0f, lowInk,
                 positions ({ "74", "", "", "116", "", "", "", "361" }), "SHELF Hz")),
      lowLevel  (p.apvts, pid::forStrip (index, pid::lowLevel),  knob (32.0f, lowInk, levelLegends(), "LEVEL dB")),
      highLevel (p.apvts, pid::forStrip (index, pid::highLevel), knob (32.0f, highInk, levelLegends(), "LEVEL dB")),
      highShelf (p.apvts, pid::forStrip (index, pid::highShelf), knob (21.0f, highInk,
                 positions ({ "18k", "", "", "3.4k", "", "", "", "1.6k" }), "SHELF Hz")),
      highCut   (p.apvts, pid::forStrip (index, pid::highCut),   knob (21.0f, highInk,
                 positions ({ "OUT", "", "", "18k", "", "", "", "7.5k" }), "CUT Hz")),
      threshold (p.apvts, pid::forStrip (index, pid::threshold), knob (21.0f, dynInk,
                 { { -48.0f, "-48" }, { -24.0f, "-24" }, { 0.0f, "0" } }, "THRESHOLD")),
      lowDyn    (p.apvts, pid::forStrip (index, pid::lowDyn),    knob (21.0f, lowInk,  levelLegends(), "LOW dB")),
      highDyn   (p.apvts, pid::forStrip (index, pid::highDyn),   knob (21.0f, highInk, levelLegends(), "HIGH dB")),
      output    (p.apvts, pid::forStrip (index, pid::output),    knob (26.0f, ink, outputLegends(), "OUTPUT")),
      engage    (p.apvts, pid::forStrip (index, pid::engage),
                 [] { auto s = modeKey ({}); s.capWidth = 34.0f; s.below = "ENGAGE"; s.legendHeight = 7.0f; return s; }()),
      meter     ({ -36.0f, -30.0f, -24.0f, -18.0f, -15.0f, -12.0f, -9.0f, -6.0f, -4.0f, -2.0f, -1.0f, 0.0f }, {}, true, ink)
{
}

std::vector<juce::Component*> HingeEditor::Strip::components()
{
    return { &lowCut, &lowShelf, &lowLevel, &highLevel, &highShelf, &highCut,
             &threshold, &lowDyn, &highDyn, &output, &engage, &meter };
}

void HingeEditor::Strip::layout (float y)
{
    lowCut   .setBounds (Knob::boundsAt ({ lowCutX,    y + 4.0f }, 21.0f));
    lowShelf .setBounds (Knob::boundsAt ({ lowShelfX,  y + 4.0f }, 21.0f));
    lowLevel .setBounds (Knob::boundsAt ({ lowLevelX,  y + 2.0f }, 32.0f));
    highLevel.setBounds (Knob::boundsAt ({ highLevelX, y + 2.0f }, 32.0f));
    highShelf.setBounds (Knob::boundsAt ({ highShelfX, y + 4.0f }, 21.0f));
    highCut  .setBounds (Knob::boundsAt ({ highCutX,   y + 4.0f }, 21.0f));
    threshold.setBounds (Knob::boundsAt ({ thresholdX, y + 4.0f }, 21.0f));
    lowDyn   .setBounds (Knob::boundsAt ({ lowDynX,    y + 4.0f }, 21.0f));
    highDyn  .setBounds (Knob::boundsAt ({ highDynX,   y + 4.0f }, 21.0f));
    output   .setBounds (Knob::boundsAt ({ outputX,    y - 2.0f }, 26.0f));
    engage   .setBounds (juce::Rectangle<float> (engageX - 26.0f, y - 22.0f, 52.0f, 44.0f).toNearestInt());
    meter    .setBounds (juce::Rectangle<float> (meterX + 2.0f, y - 60.0f, 16.0f, 120.0f).toNearestInt());
}

// ---------------------------------------------------------------------------

HingeEditor::Face::Face (HingeProcessor& p)
    : processor (p),
      modeKeys { ui::Key (modeKey ("STEREO")), ui::Key (modeKey ("DUAL")), ui::Key (modeKey ("M/S")) },
      fold (ui::FoldTab::Edge::bottom, ink, juce::Colour (0xffc9c2b1))
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<Strip> (p, i);
        for (auto* child : strips[(size_t) i]->components())
            i == 0 ? addAndMakeVisible (child) : addChildComponent (child);
    }

    for (int i = 0; i < 3; ++i)
    {
        addAndMakeVisible (modeKeys[(size_t) i]);
        modeKeys[(size_t) i].onClick = [this, i] { setMode (i); };
    }

    addAndMakeVisible (fold);
    fold.setCaption ("M/S · DUAL", "FOLD");
    fold.onClick = [this]
    {
        // Folding links the rows; unfolding unlinks them. In mid/side that is
        // one setting for both halves or one each; otherwise stereo or dual.
        auto* link = processor.apvts.getParameter (pid::link);
        link->beginChangeGesture();
        link->setValueNotifyingHost (open ? 1.0f : 0.0f);
        link->endChangeGesture();
    };

    refresh();
}

void HingeEditor::Face::setMode (int index)
{
    // STEREO: linked, left/right. DUAL: unlinked, left/right. M/S: unlinked, mid/side.
    const float link = index == 0 ? 1.0f : 0.0f, midSide = index == 2 ? 1.0f : 0.0f;

    for (auto [id, value] : { std::pair<const char*, float> { pid::link, link }, { pid::midSide, midSide } })
    {
        auto* parameter = processor.apvts.getParameter (id);
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost (value);
        parameter->endChangeGesture();
    }
}

void HingeEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        strips[(size_t) i]->layout (rowCentres[i]);

    for (int i = 0; i < 3; ++i)
        modeKeys[(size_t) i].setBounds (juce::Rectangle<float> (104.0f + 54.0f * (float) i - 26.0f, 142.0f, 52.0f, 26.0f).toNearestInt());

    const auto height = (float) (open ? designHeightOpen : designHeightFolded);
    fold.setBounds (juce::Rectangle<float> (760.0f, height - 19.0f, 120.0f, 16.0f).toNearestInt());
}

void HingeEditor::Face::refresh()
{
    auto& a = processor.meters.channels[0];
    auto& b = processor.meters.channels[1];
    const float peaks[2] = { MeterSource::take (a.outputPeak), MeterSource::take (b.outputPeak) };
    float moves[2][2];
    for (int s = 0; s < 2; ++s)
    {
        moves[0][s] = MeterSource::take (a.shelfMovement[(size_t) s]);
        moves[1][s] = MeterSource::take (b.shelfMovement[(size_t) s]);
    }

    // Folded, the one row shows whichever channel is louder and moving more.
    strips[0]->meter.push (toDb (open ? peaks[0] : juce::jmax (peaks[0], peaks[1])));
    strips[1]->meter.push (toDb (peaks[1]));

    for (int s = 0; s < 2; ++s)
    {
        const auto first = open ? moves[0][s] : juce::jmax (moves[0][s], moves[1][s]);
        for (auto [row, value] : { std::pair<int, float> { 0, first }, { 1, moves[1][s] } })
        {
            const auto brightness = juce::jlimit (0.0f, 1.0f, value / 3.0f);
            auto& shown = strips[(size_t) row]->movement[s];
            if (std::abs (brightness - shown) > 0.02f)
            {
                shown = brightness;
                repaint (juce::Rectangle<float> (1280.0f + 26.0f * (float) s, rowCentres[row] - 74.0f, 40.0f, 20.0f).toNearestInt());
            }
        }
    }

    const auto linked  = processor.apvts.getRawParameterValue (pid::link)->load() >= 0.5f;
    const auto midSide = processor.apvts.getRawParameterValue (pid::midSide)->load() >= 0.5f;
    const auto modeIndex = midSide ? 2 : linked ? 0 : 1;

    for (int i = 0; i < 3; ++i)
        modeKeys[(size_t) i].setToggleState (i == modeIndex, juce::dontSendNotification);

    const auto shouldOpen = ! linked;
    if (shouldOpen == open && modeIndex == shownMode)
        return;

    shownMode = modeIndex;
    const auto changed = shouldOpen != open;
    open = shouldOpen;

    for (auto* child : strips[1]->components())
        child->setVisible (open);

    fold.setCaption (midSide ? "SPLIT M · S" : "DUAL L · R", "FOLD");
    fold.setOpen (open);

    resized();
    repaint();

    if (changed && onFoldChanged)
        onFoldChanged();
}

void HingeEditor::Face::paintRow (juce::Graphics& g, int index, float y) const
{
    const auto midSide = shownMode == 2;
    juce::String letter = midSide ? (index == 0 ? "M" : "S") : (index == 0 ? "L" : "R");
    if (! open) letter = midSide ? "M·S" : "";
    ui::drawLabel (g, letter, { letterX - 30.0f, y - 18.0f, 60.0f, 36.0f }, letter.length() > 1 ? 15.0f : 30.0f,
                   ink, juce::Justification::centred, false);

    const auto section = [&g, y] (float x, float w, const char* title)
    {
        g.setColour (frame);
        g.drawRoundedRectangle ({ x, y - 64.0f, w, 132.0f }, 10.0f, 1.1f);
        const auto titleWidth = ui::font (8.5f).getStringWidthFloat (title) + 22.0f;
        g.setGradientFill (juce::ColourGradient (plateTop, 0.0f, 0.0f, plateBottom, 0.0f, (float) designHeightOpen, false));
        g.fillRect (juce::Rectangle<float> (x + 18.0f, y - 67.0f, titleWidth, 6.0f));
        ui::drawLabel (g, title, { x + 18.0f, y - 70.0f, titleWidth, 12.0f }, 8.5f, ink, juce::Justification::centred, true, 1.6f);
    };

    section (lowX,  toneW, "LOW");
    section (highX, toneW, "HIGH");
    section (dynX,  dynW,  "DYNAMIC");

    const auto& s = *strips[(size_t) index];
    ui::drawLed (g, { 1300.0f, y - 64.0f }, 3.5f, juce::Colour (0xffff6a3d), s.movement[0]);
    ui::drawLed (g, { 1326.0f, y - 64.0f }, 3.5f, juce::Colour (0xff3d9dff), s.movement[1]);

    // The meter window.
    const auto window = juce::Rectangle<float> (meterX, y - 62.0f, 20.0f, 124.0f);
    g.setColour (juce::Colour (0xff050505));
    g.fillRoundedRectangle (window.expanded (2.0f), 4.0f);
    g.setColour (juce::Colour (0xff0b0a09));
    g.fillRoundedRectangle (window, 3.0f);
}

void HingeEditor::Face::paint (juce::Graphics& g)
{
    const auto height = (float) (open ? designHeightOpen : designHeightFolded);
    const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) designWidth, height);

    ui::drawPlate (g, bounds, plateTop, plateBottom, 0x48494e);
    ui::drawRackEar (g, bounds.withWidth (earWidth), true);
    ui::drawRackEar (g, bounds.withLeft (designWidth - earWidth), false);

    ui::drawLogo (g, { 158.0f, 42.0f }, 0.85f, ink, dim);
    ui::drawLabel (g, "Hinge", { 64.0f, 82.0f, 188.0f, 32.0f }, 28.0f, ink, juce::Justification::centred, false);
    ui::drawLabel (g, "SHELVING EQUALISER", { 64.0f, 114.0f, 188.0f, 10.0f }, 7.5f, dim, juce::Justification::centred, true, 2.0f);

    g.setColour (dim.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (252.0f, 22.0f, 1.0f, height - 44.0f));

    paintRow (g, 0, rowCentres[0]);
    if (open)
        paintRow (g, 1, rowCentres[1]);
}

// ---------------------------------------------------------------------------

HingeEditor::HingeEditor (HingeProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      face (p)
{
    addAndMakeVisible (face);
    face.onFoldChanged = [this] { applyFold(); };

    setResizable (true, true);
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;
    setResizeLimits (designWidth / 2, height / 2, designWidth * 8 / 5, height * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / height);

    setSize (designWidth * 4 / 5, height * 4 / 5);
    startTimerHz (30);
}

HingeEditor::~HingeEditor()
{
    stopTimer();
}

void HingeEditor::applyFold()
{
    // Keep the scale the panel is drawn at; only the height changes.
    const auto scale = (double) getWidth() / designWidth;
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;

    setResizeLimits (designWidth / 2, height / 2, designWidth * 8 / 5, height * 8 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) designWidth / height);

    setSize (getWidth(), juce::roundToInt (height * scale));
}

void HingeEditor::paint (juce::Graphics& g)
{
    g.fillAll (plateBottom);
}

void HingeEditor::resized()
{
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;
    face.setBounds (0, 0, designWidth, height);
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) designWidth,
                                                     (float) getHeight() / (float) height));
}

void HingeEditor::timerCallback()
{
    face.refresh();
}

} // namespace hinge
