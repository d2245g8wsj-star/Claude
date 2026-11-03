#include "PluginEditor.h"

namespace crest
{

namespace ui = indiewave::ui;

namespace
{
    // Brushed champagne aluminium, black screen printing, amber light.
    const juce::Colour plateTop    { 0xffddd7ca };
    const juce::Colour plateBottom { 0xffbfb7a7 };
    const juce::Colour ink         { 0xff26231f };
    const juce::Colour dim         { 0xff615a50 };
    const juce::Colour lit         { 0xffe9873d };
    const juce::Colour glow        { 0xffffb46a };
    const juce::Colour screen      { 0xff0d0a08 };

    constexpr float earWidth = 64.0f;
    constexpr float leftMarginX = 121.0f, rightMarginX = 1521.0f;

    const juce::Rectangle<float> clipSection   { 178.0f, 26.0f, 270.0f, 326.0f };
    const juce::Rectangle<float> toneSection   { 1028.0f, 26.0f, 252.0f, 326.0f };
    const juce::Rectangle<float> outputSection { 1298.0f, 26.0f, 168.0f, 326.0f };
    const juce::Rectangle<float> display       { 466.0f, 40.0f, 544.0f, 260.0f };
    const juce::Rectangle<float> transferInset { 870.0f, 160.0f, 132.0f, 132.0f };

    constexpr float readoutY = 328.0f, readoutPitch = 139.0f, readoutWidth = 128.0f;
    constexpr float keyRowY = 382.0f;

    // The LED column sits 6 units in from the ladder's edge, its scale beyond
    // the dark strip behind it.
    const juce::Rectangle<float> inScreen  { leftMarginX  - 6.0f, 180.0f, 12.0f, 186.0f };
    const juce::Rectangle<float> outScreen { rightMarginX - 6.0f, 180.0f, 12.0f, 186.0f };

    using Legends = std::vector<std::pair<float, juce::String>>;

    ui::BakeliteKnob::Style knob (float radius, int dots, Legends legends, juce::String name)
    {
        ui::BakeliteKnob::Style s;
        s.radius = radius;
        s.dots = dots;
        s.dotColour = ink;
        s.ink = ink;
        s.dim = dim;
        s.legends = std::move (legends);
        s.legendHeight = radius > 30.0f ? 8.5f : 7.5f;
        s.name = std::move (name);
        s.nameHeight = 9.0f;
        return s;
    }

    std::vector<float> ladderSteps()
    {
        return { -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -9.0f, -6.0f, -4.0f, -3.0f, -2.0f, -1.0f, -0.1f };
    }

    juce::StringArray ladderLabels() { return { "48", "", "24", "", "12", "", "6", "", "3", "", "", "0" }; }

    ui::Key::Style keyStyle (juce::String label, float width)
    {
        ui::Key::Style s;
        s.kind = ui::Key::Kind::white;
        s.lit = lit;
        s.ink = ink;
        s.dim = dim;
        s.capSize = 20.0f;
        s.capWidth = width - 6.0f;
        s.capLabel = std::move (label);
        s.capLabelOn = juce::Colour (0xff2a1405);
        s.capLabelOff = juce::Colour (0xff3a342d);
        return s;
    }

    juce::String figure (float value, float floorDb)
    {
        return value <= floorDb ? juce::String ("-inf") : juce::String (value, 1);
    }
}

// ---------------------------------------------------------------------------

CrestEditor::KeyRow::KeyRow (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, float width)
    : keyWidth (width),
      parameter (*state.getParameter (parameterID))
{
    const auto choices = parameter.getAllValueStrings();

    for (int i = 0; i < choices.size(); ++i)
    {
        auto key = std::make_unique<Key> (keyStyle (choices[i], keyWidth));
        key->setTooltip (parameter.getName (64) + ": " + choices[i]);
        key->onClick = [this, i]
        {
            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost (parameter.convertTo0to1 ((float) i));
            parameter.endChangeGesture();
            refresh();
        };
        addAndMakeVisible (*key);
        keys.push_back (std::move (key));
    }

    refresh();
}

void CrestEditor::KeyRow::resized()
{
    for (size_t i = 0; i < keys.size(); ++i)
        keys[i]->setBounds (juce::Rectangle<float> (keyWidth * (float) i, 0.0f, keyWidth, (float) getHeight()).toNearestInt());
}

void CrestEditor::KeyRow::refresh()
{
    const auto selected = juce::roundToInt (parameter.convertFrom0to1 (parameter.getValue()));

    for (size_t i = 0; i < keys.size(); ++i)
        keys[i]->setToggleState ((int) i == selected, juce::dontSendNotification);
}

// ---------------------------------------------------------------------------

CrestEditor::Face::Face (CrestProcessor& p)
    : processor (p),
      inputKnob     (p.apvts, pid::input,     knob (36.0f, 31, { { -12.0f, "-12" }, { 0.0f, "0" }, { 18.0f, "+18" } }, "INPUT")),
      characterKnob (p.apvts, pid::mode,      knob (26.0f, 0, { { 0.0f, "GLASS" }, { 1.0f, "EDGE" }, { 2.0f, "SILK" }, { 3.0f, "TUBE" } },
                                                    "CHARACTER")),
      kneeKnob      (p.apvts, pid::knee,      knob (24.0f, 11, { { 0.0f, "0" }, { 24.0f, "24" } }, "KNEE")),
      adaptKnob     (p.apvts, pid::adapt,     knob (24.0f, 11, { { 0.0f, "0" }, { 100.0f, "100" } }, "ADAPT")),
      biasKnob      (p.apvts, pid::bias,      knob (24.0f, 11, { { -100.0f, "-" }, { 0.0f, "0" }, { 100.0f, "+" } }, "BIAS")),
      colourKnob    (p.apvts, pid::colour,    knob (24.0f, 11, { { 0.0f, "0" }, { 100.0f, "100" } }, "COLOUR")),
      harmonicsKnob (p.apvts, pid::harmonics, knob (24.0f, 11, { { 0.0f, "0" }, { 100.0f, "100" } }, "HARMONICS")),
      seriesKnob    (p.apvts, pid::series,    knob (26.0f, 11, { { -100.0f, "EVEN" }, { 100.0f, "ODD" } }, "SERIES")),
      sidechainKnob (p.apvts, pid::sidechain, knob (22.0f, 11, { { 15.0f, "OFF" }, { 800.0f, "800" } }, "SC LPF")),
      mixKnob       (p.apvts, pid::mix,       knob (22.0f, 11, { { 0.0f, "0" }, { 100.0f, "100" } }, "MIX")),
      ceilingKnob   (p.apvts, pid::ceiling,   knob (34.0f, 25, { { -24.0f, "-24" }, { -12.0f, "-12" }, { 0.0f, "0" } }, "CEILING")),
      outputKnob    (p.apvts, pid::output,    knob (34.0f, 37, { { -24.0f, "-24" }, { 0.0f, "0" }, { 12.0f, "+12" } }, "OUTPUT")),
      inputMeter  (ladderSteps(), ladderLabels(), true,  ink),
      outputMeter (ladderSteps(), ladderLabels(), false, ink),
      waveform    (p.waveform),
      scope       (p.apvts, p.meters, p.displayKnee),
      osKeys      (p.apvts, pid::os,     40.0f),
      wordKeys    (p.apvts, pid::word,   40.0f),
      ditherKeys  (p.apvts, pid::dither, 52.0f),
      autoKey     (p.apvts, pid::autoGain, keyStyle ("AUTO",    62.0f)),
      tpSafeKey   (p.apvts, pid::tpSafe,   keyStyle ("TP SAFE", 62.0f)),
      deltaKey    (p.apvts, pid::delta,    keyStyle ("DELTA",   62.0f)),
      linkKey     (p.apvts, pid::link,     keyStyle ("LINK",    62.0f)),
      bypassKey   (p.apvts, pid::bypass,   keyStyle ("BYPASS",  62.0f))
{
    waveform.setOnDark (true);
    scope.setOnDark (true);

    for (auto& f : figures)
        f = "--";

    for (auto* child : std::initializer_list<juce::Component*> {
             &inputMeter, &outputMeter, &waveform, &scope,
             &inputKnob, &characterKnob, &kneeKnob, &adaptKnob, &biasKnob,
             &colourKnob, &harmonicsKnob, &seriesKnob, &sidechainKnob, &mixKnob,
             &ceilingKnob, &outputKnob,
             &osKeys, &wordKeys, &ditherKeys,
             &autoKey, &tpSafeKey, &deltaKey, &linkKey, &bypassKey })
    {
        addAndMakeVisible (child);
    }
}

void CrestEditor::Face::resized()
{
    inputKnob    .setBounds (Knob::boundsAt ({ 250.0f, 112.0f }, 36.0f));
    characterKnob.setBounds (Knob::boundsAt ({ 384.0f, 112.0f }, 26.0f));
    kneeKnob     .setBounds (Knob::boundsAt ({ 230.0f, 270.0f }, 24.0f));
    adaptKnob    .setBounds (Knob::boundsAt ({ 313.0f, 270.0f }, 24.0f));
    biasKnob     .setBounds (Knob::boundsAt ({ 396.0f, 270.0f }, 24.0f));

    colourKnob   .setBounds (Knob::boundsAt ({ 1092.0f, 104.0f }, 24.0f));
    harmonicsKnob.setBounds (Knob::boundsAt ({ 1214.0f, 104.0f }, 24.0f));
    seriesKnob   .setBounds (Knob::boundsAt ({ 1153.0f, 190.0f }, 26.0f));
    sidechainKnob.setBounds (Knob::boundsAt ({ 1092.0f, 280.0f }, 22.0f));
    mixKnob      .setBounds (Knob::boundsAt ({ 1214.0f, 280.0f }, 22.0f));

    ceilingKnob  .setBounds (Knob::boundsAt ({ 1382.0f, 112.0f }, 34.0f));
    outputKnob   .setBounds (Knob::boundsAt ({ 1382.0f, 262.0f }, 34.0f));

    const auto inner = display.reduced (4.0f);
    waveform.setBounds (inner.toNearestInt());
    waveform.setExcluded (transferInset.translated (-inner.getX(), -inner.getY()).expanded (2.0f).toNearestInt());
    scope   .setBounds (transferInset.withTrimmedTop (18.0f).reduced (4.0f, 2.0f).toNearestInt());

    inputMeter .setBounds (juce::Rectangle<float> (inScreen.getRight() - 50.0f, inScreen.getY(), 50.0f, inScreen.getHeight()).toNearestInt());
    outputMeter.setBounds (juce::Rectangle<float> (outScreen.getX(), outScreen.getY(), 50.0f, outScreen.getHeight()).toNearestInt());


    const auto row = [] (KeyRow& keys, float x)
    {
        keys.setBounds (juce::Rectangle<float> (x, keyRowY, keys.keyWidth * (float) keys.getNumChildComponents(), 26.0f).toNearestInt());
    };
    row (osKeys,     178.0f);
    row (wordKeys,   410.0f);
    row (ditherKeys, 596.0f);

    Key* toggles[] { &autoKey, &tpSafeKey, &deltaKey, &linkKey, &bypassKey };

    for (int i = 0; i < 5; ++i)
        toggles[i]->setBounds (juce::Rectangle<float> (800.0f + 72.0f * (float) i, keyRowY, 62.0f, 26.0f).toNearestInt());
}

void CrestEditor::Face::refresh (const juce::String& status)
{
    auto& m = processor.meters;
    const auto inDb  = juce::Decibels::gainToDecibels (juce::jmax (m.inputPeak[0].take(),  m.inputPeak[1].take()),  -100.0f);
    const auto outDb = juce::Decibels::gainToDecibels (juce::jmax (m.outputPeak[0].take(), m.outputPeak[1].take()), -100.0f);

    inputMeter .push (inDb);
    outputMeter.push (outDb);

    // The figure under each ladder holds its peak for a second and a half,
    // then falls, so it can be read.
    const auto hold = [] (float db, float& held, int& ticks)
    {
        if (db >= held)
        {
            held = db;
            ticks = 45;
        }
        else if (--ticks <= 0)
        {
            held = juce::jmax (-100.0f, held - 0.6f);
        }
    };
    hold (inDb,  inputHold,  inputHoldTicks);
    hold (outDb, outputHold, outputHoldTicks);

    const auto removed = juce::jlimit (0.0f, 0.999f, m.reduction.take());
    const auto clip = -juce::Decibels::gainToDecibels (1.0f - removed, -60.0f);
    clipDb = clip > clipDb ? clip : juce::jmax (0.0f, clipDb - 0.1f);

    juce::String next[6] {
        figure (inputHold, -60.0f),
        figure (outputHold, -60.0f),
        clipDb < 0.02f ? juce::String ("0.0") : juce::String (clipDb, 1),
        figure (processor.displayMomentary .load(), -99.0f),
        figure (processor.displayShortTerm .load(), -99.0f),
        figure (processor.displayIntegrated.load(), -99.0f)
    };

    const auto power = processor.apvts.getRawParameterValue (pid::bypass)->load() < 0.5f;
    auto changed = status != statusText || power != powered;

    for (int i = 0; i < 6; ++i)
        if (next[i] != figures[i])
        {
            figures[i] = next[i];
            changed = true;
        }

    osKeys.refresh();
    wordKeys.refresh();
    ditherKeys.refresh();

    if (changed)
    {
        statusText = status;
        powered = power;
        repaint (0, 26, 178, 400);
        repaint (1466, 26, 120, 400);
        repaint (display.withY (310.0f).withHeight (46.0f).toNearestInt());
        repaint (1150, 376, 330, 40);
    }
}

void CrestEditor::Face::paintScreen (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, float textHeight) const
{
    g.setColour (juce::Colours::white.withAlpha (0.35f));
    g.drawRoundedRectangle (r.translated (0.0f, 1.0f), 3.0f, 1.0f);
    g.setColour (screen);
    g.fillRoundedRectangle (r, 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.5f), 0.0f, r.getY(),
                                             juce::Colours::transparentBlack, 0.0f, r.getY() + 6.0f, false));
    g.fillRoundedRectangle (r, 3.0f);

    g.setColour (glow);
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), textHeight, juce::Font::bold)));
    g.drawText (text, r, juce::Justification::centred, false);
}

void CrestEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    ui::drawPlate (g, bounds, plateTop, plateBottom, 0x435253);
    ui::drawRackEar (g, bounds.withWidth (earWidth), true);
    ui::drawRackEar (g, bounds.withLeft (bounds.getRight() - earWidth), false);

    // Left margin: the mark, the name, the input ladder.
    ui::drawLogo (g, { leftMarginX, 52.0f }, 0.9f, ink, dim);
    ui::drawLabel (g, "Crest", { leftMarginX - 56.0f, 100.0f, 112.0f, 32.0f }, 28.0f, ink, juce::Justification::centred, false);
    ui::drawLabel (g, "CONVERTER", { leftMarginX - 56.0f, 134.0f, 112.0f, 10.0f }, 7.5f, dim, juce::Justification::centred, false, 2.0f);
    ui::drawLabel (g, "CLIPPER",   { leftMarginX - 56.0f, 146.0f, 112.0f, 10.0f }, 7.5f, dim, juce::Justification::centred, false, 2.0f);

    // Right margin: the power lamp and the output ladder.
    ui::drawLed (g, { rightMarginX, 60.0f }, 4.0f, juce::Colour (0xffff9a3d), powered ? 1.0f : 0.0f);
    ui::drawLabel (g, "POWER", { rightMarginX - 40.0f, 74.0f, 80.0f, 12.0f }, 7.0f, dim, juce::Justification::centred, true, 1.6f);

    for (auto [area, name, value] : { std::tuple { inScreen, "IN", figures[0] }, std::tuple { outScreen, "OUT", figures[1] } })
    {
        g.setColour (juce::Colour (0xff0b0a09));
        g.fillRoundedRectangle (area, 4.0f);
        ui::drawLabel (g, name, { area.getCentreX() - 30.0f, 376.0f, 60.0f, 12.0f }, 9.0f, ink, juce::Justification::centred, true);
        paintScreen (g, juce::Rectangle<float> (52.0f, 20.0f).withCentre ({ area.getCentreX(), 402.0f }), value, 10.0f);
    }

    // The three control sections.
    for (auto [area, title] : { std::pair { clipSection, "CLIP" }, std::pair { toneSection, "TONE" },
                                std::pair { outputSection, "OUTPUT" } })
    {
        ui::drawSubpanel (g, area, 10.0f, juce::Colour (0xffe2ddd1), juce::Colour (0xffcbc4b5));
        ui::drawLabel (g, title, { area.getX(), area.getY() + 8.0f, area.getWidth(), 14.0f }, 10.0f, ink,
                       juce::Justification::centred, true, 2.0f);
    }

    // The display: a black screen with a faint grid, the ceiling marked.
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.drawRoundedRectangle (display.translated (0.0f, 1.0f), 6.0f, 1.0f);
    g.setColour (screen);
    g.fillRoundedRectangle (display, 6.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (display.toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.05f));

        for (int i = 1; i < 8; ++i)
            g.fillRect (display.getX() + display.getWidth() * (float) i / 8.0f, display.getY(), 1.0f, display.getHeight());

        for (int i = 1; i < 4; ++i)
            g.fillRect (display.getX(), display.getY() + display.getHeight() * (float) i / 4.0f, display.getWidth(), 1.0f);
    }
    ui::drawLabel (g, "WAVEFORM", { display.getX() + 18.0f, display.getY() + 8.0f, 120.0f, 12.0f }, 8.0f,
                   juce::Colour (0xffe08a44), juce::Justification::centredLeft, true, 1.4f);

    g.setColour (juce::Colour (0xff120d09));
    g.fillRoundedRectangle (transferInset, 4.0f);
    g.setColour (juce::Colour (0xffe08a44).withAlpha (0.4f));
    g.drawRoundedRectangle (transferInset, 4.0f, 1.0f);
    ui::drawLabel (g, "TRANSFER", transferInset.withHeight (18.0f).translated (0.0f, 3.0f), 7.0f,
                   juce::Colour (0xffe08a44), juce::Justification::centred, true, 1.2f);

    // The four figures under the display.
    const char* names[] { "CLIP dB", "MOMENTARY", "SHORT TERM", "INTEGRATED" };

    for (int i = 0; i < 4; ++i)
    {
        const auto x = display.getX() + readoutPitch * (float) i;
        ui::drawLabel (g, names[i], { x, readoutY - 16.0f, readoutWidth, 12.0f }, 8.0f, ink, juce::Justification::centred, true, 1.0f);
        paintScreen (g, { x, readoutY, readoutWidth, 24.0f }, figures[2 + i], 13.0f);
    }

    // Headings over the converter row.
    for (auto [x, width, title] : { std::tuple { 178.0f, 200.0f, "OVERSAMPLING" }, std::tuple { 410.0f, 160.0f, "WORD" },
                                    std::tuple { 596.0f, 156.0f, "DITHER" } })
        ui::drawLabel (g, title, { x, keyRowY - 14.0f, width, 11.0f }, 8.0f, dim, juce::Justification::centred, true, 1.4f);

    ui::drawLabel (g, statusText, { 1170.0f, keyRowY + 4.0f, 300.0f, 14.0f }, 8.0f, dim, juce::Justification::centred, false, 0.8f);
}

// ---------------------------------------------------------------------------

CrestEditor::Tooltips::Tooltips()
{
    setColour (juce::TooltipWindow::backgroundColourId, plateTop);
    setColour (juce::TooltipWindow::textColourId,       ink);
    setColour (juce::TooltipWindow::outlineColourId,    ink);
}

void CrestEditor::Tooltips::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (plateTop);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (ink.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    g.setColour (ink);
    g.setFont (ui::font (12.0f, false));
    g.drawFittedText (text, bounds.reduced (9.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 3);
}

// ---------------------------------------------------------------------------

CrestEditor::CrestEditor (CrestProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), face (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (face);

    setResizable (true, true);

    if (auto* sizeConstrainer = getConstrainer())
    {
        sizeConstrainer->setFixedAspectRatio ((double) designWidth / (double) designHeight);
        sizeConstrainer->setSizeLimits (designWidth * 50 / 100, designHeight * 50 / 100,
                                        designWidth * 150 / 100, designHeight * 150 / 100);
    }

    // The width the editor was last left at is kept with the session. Sessions
    // from before the wider panel stored a width for the old one, which lands
    // at about the same height here.
    const auto saved = (int) processor.apvts.state.getProperty ("editorWidth", designWidth * 4 / 5);
    const auto width = juce::jlimit (designWidth * 50 / 100, designWidth * 150 / 100, saved);

    setSize (width, juce::roundToInt ((double) width * designHeight / designWidth));

    startTimerHz (30);
}

CrestEditor::~CrestEditor()
{
    setLookAndFeel (nullptr);
}

void CrestEditor::paint (juce::Graphics& g)
{
    g.fillAll (plateBottom);
}

void CrestEditor::resized()
{
    const auto scale = (float) getWidth() / (float) designWidth;

    face.setBounds (0, 0, designWidth, designHeight);
    face.setTransform (juce::AffineTransform::scale (scale));

    processor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}

void CrestEditor::timerCallback()
{
    const auto rate    = processor.getSampleRate();
    const auto latency = processor.getLatencySamples();

    juce::String status;
    status << juce::String (rate / 1000.0, 1) << " kHz  " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
           << "  LATENCY " << latency << " smp  " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"))
           << "  v" << JucePlugin_VersionString;

    if (JucePlugin_VersionCode < 0x10000)
        status << " BETA";

    face.refresh (status);
}

} // namespace crest
