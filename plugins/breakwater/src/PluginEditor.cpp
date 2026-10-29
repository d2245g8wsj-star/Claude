#include "PluginEditor.h"

namespace breakwater
{

namespace
{
    using Size = panel::Knob::Size;

    // Vertical plan, simple view.
    constexpr float headerHeight = 58.0f;
    constexpr float styleTop     = 66.0f;
    constexpr float scopeTop     = 108.0f;
    constexpr float scopeBottom  = 440.0f;
    constexpr float mainTop      = 452.0f;
    constexpr float mainHeight   = 172.0f;

    // Vertical plan, advanced panel.
    constexpr float advancedTop  = 636.0f;
    constexpr float rowTop[]     = { 648.0f, 852.0f };
    constexpr float rowHeight    = 196.0f;

    // Horizontal plan.
    constexpr float margin    = 24.0f;
    constexpr float sideX     = 920.0f;
    constexpr float sideWidth = 256.0f;

    struct Group { float x, top, width; const char* name; };
    constexpr Group timingGroup     { 24.0f,  rowTop[0], 712.0f, "LIMITER" };
    constexpr Group toneGroup       { 748.0f, rowTop[0], 428.0f, "TONE" };
    constexpr Group compressorGroup { 24.0f,  rowTop[1], 712.0f, "COMPRESSOR" };
    constexpr Group outputGroup     { 748.0f, rowTop[1], 428.0f, "OUTPUT" };

    /** Where the editor remembers whether the advanced panel was open. */
    const juce::Identifier advancedProperty { "advancedView" };

    juce::Rectangle<int> box (float x, float y, float w, float h)
    {
        return juce::Rectangle<float> (x, y, w, h).toNearestInt();
    }

    juce::Rectangle<int> knobIn (float x, float top, float width, Size size)
    {
        return size == Size::large ? box (x, top + 6.0f, width, 160.0f)
                                   : box (x, top + 32.0f, width, 138.0f);
    }

    void paintGroup (juce::Graphics& g, juce::Rectangle<float> area, const char* name)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        if (name != nullptr)
            panel::drawLabel (g, name, { area.getX() + 14.0f, area.getY() + 8.0f, 200.0f, 16.0f }, 9.0f,
                              panel::textFaint, juce::Justification::centredLeft, true, 2.4f);
    }
}

// ---------------------------------------------------------------------------

BreakwaterEditor::Face::Face (BreakwaterProcessor& p)
    : processor (p),
      engine       (p.apvts, pid::engine),
      style        (p.apvts, pid::style),
      truePeak     (p.apvts, pid::truePeak, "True Peak"),
      bypass       (p.apvts, pid::bypass,   "Bypass", panel::warning),
      match        (p.apvts, pid::match,    "Gain Match"),
      delta        (p.apvts, pid::delta,    "Delta", panel::reduction),
      gain         (p.apvts, pid::gain,       "Gain",       Size::large),
      ceiling      (p.apvts, pid::ceiling,    "Ceiling",    Size::large),
      transients   (p.apvts, pid::transients, "Transients", Size::small, true),
      lowPunch     (p.apvts, pid::lowPunch,   "Low Punch",  Size::small, true),
      detail       (p.apvts, pid::detail,     "Detail",     Size::small),
      target       (p.apvts, pid::target,     "Target",     Size::small),
      scope    (p.meters),
      meters   (p.meters),
      loudness (p.meters),
      oversampling (p.apvts, pid::oversampling, { "1x", "2x", "4x", "8x", "16x", "32x" }),
      compMode     (p.apvts, pid::compMode, { "Stereo", "M/S" }),
      wordLength   (p.apvts, pid::wordLength, { "Off", "24", "20", "16" }),
      dither       (p.apvts, pid::dither, { "None", "TPDF", "Shape" }),
      adaptiveLink (p.apvts, pid::adaptiveLink, "Adaptive Link"),
      compOn       (p.apvts, pid::compOn,       "Comp In"),
      dcFilter     (p.apvts, pid::dcFilter,     "DC Filter"),
      sidechain    (p.apvts, pid::sidechain,    "Ext Sidechain"),
      lookahead      (p.apvts, pid::lookahead,      "Lookahead",  Size::small),
      attack         (p.apvts, pid::attack,         "Attack",     Size::small),
      release        (p.apvts, pid::release,        "Release",    Size::small),
      window         (p.apvts, pid::window,         "Window",     Size::small),
      linkTransients (p.apvts, pid::linkTransients, "Link Trans", Size::small),
      linkRelease    (p.apvts, pid::linkRelease,    "Link Rel",   Size::small),
      balance        (p.apvts, pid::balance,        "Balance",    Size::small, true),
      clip           (p.apvts, pid::clip,           "Clip",       Size::small),
      compThreshold  (p.apvts, pid::compThreshold,  "Threshold",  Size::small),
      compRatio      (p.apvts, pid::compRatio,      "Ratio",      Size::small),
      compAttack     (p.apvts, pid::compAttack,     "Attack",     Size::small),
      compRelease    (p.apvts, pid::compRelease,    "Release",    Size::small),
      compHighPass   (p.apvts, pid::compHighPass,   "SC HPF",     Size::small)
{
    juce::StringArray styleDescriptions;
    for (const auto& s : styles)
        styleDescriptions.add (s.description);

    style.setDescriptions (styleDescriptions);
    engine.setDescriptions ({ "Lookahead limiting with a leveller and eight styles",
                              "No lookahead: transients keep their shape, like a circuit" });

    advanced.setTooltip ("Show the timing, compressor, tone and output settings");
    transients.setTooltip ("Pushes drum fronts up before the limiter, or softens them");
    lowPunch.setTooltip ("The same, for the kick and bass only");
    detail.setTooltip ("Lifts the quiet details between the hits");
    target.setTooltip ("The loudness the integrated reading is compared against");
    window.setTooltip ("Holds the detector for a quarter of a note's period, so a fast limiter "
                       "does not follow the cycles of a bass note. C0 is the longest.");
    balance.setTooltip ("How the low end and the rest share the reduction: towards Lows the top stays "
                        "bright, towards Highs the low end stays full");
    adaptiveLink.setTooltip ("Links the channels more as they become more alike, measured by correlation");
    sidechain.setTooltip ("Detect on the sidechain input instead of the programme. "
                          "The true peak stage still watches the programme.");

    for (auto* c : std::initializer_list<juce::Component*> {
             &scope, &meters, &loudness, &engine, &style, &advanced,
             &truePeak, &bypass, &match, &delta,
             &gain, &ceiling, &transients, &lowPunch, &detail, &target })
        addAndMakeVisible (c);

    for (auto* c : advancedComponents())
        addChildComponent (c);

    refresh();
}

std::vector<juce::Component*> BreakwaterEditor::Face::advancedComponents()
{
    return { &oversampling, &compMode, &wordLength, &dither,
             &adaptiveLink, &compOn, &dcFilter, &sidechain,
             &lookahead, &attack, &release, &window, &linkTransients, &linkRelease,
             &balance, &clip,
             &compThreshold, &compRatio, &compAttack, &compRelease, &compHighPass };
}

void BreakwaterEditor::Face::showAdvanced (bool shouldShow)
{
    advancedShown = shouldShow;
    advanced.setToggleState (shouldShow, juce::dontSendNotification);

    for (auto* c : advancedComponents())
        c->setVisible (shouldShow);

    setSize (designWidth, shouldShow ? designHeightAdvanced : designHeight);
    repaint();
}

void BreakwaterEditor::Face::resized()
{
    // Header.
    engine  .setBounds (box (380.0f, 16.0f, 180.0f, 28.0f));
    truePeak.setBounds (box (760.0f, 16.0f, 120.0f, 28.0f));
    bypass  .setBounds (box (892.0f, 16.0f, 120.0f, 28.0f));
    advanced.setBounds (box (1024.0f, 16.0f, 152.0f, 28.0f));

    style.setBounds (box (margin + 64.0f, styleTop, 908.0f - margin - 64.0f, 32.0f));

    scope   .setBounds (box (margin, scopeTop, 884.0f, scopeBottom - scopeTop));
    meters  .setBounds (box (sideX, styleTop, sideWidth, 170.0f));
    loudness.setBounds (box (sideX, 244.0f, sideWidth, scopeBottom - 244.0f));

    // The main row: drive and ceiling, the three shapers, monitoring, target.
    gain      .setBounds (knobIn (48.0f,  mainTop, 120.0f, Size::large));
    ceiling   .setBounds (knobIn (180.0f, mainTop, 120.0f, Size::large));
    transients.setBounds (knobIn (340.0f, mainTop, 96.0f, Size::small).translated (0, -14));
    lowPunch  .setBounds (knobIn (444.0f, mainTop, 96.0f, Size::small).translated (0, -14));
    detail    .setBounds (knobIn (548.0f, mainTop, 96.0f, Size::small).translated (0, -14));
    match     .setBounds (box (690.0f, mainTop + 48.0f, 150.0f, 30.0f));
    delta     .setBounds (box (690.0f, mainTop + 94.0f, 150.0f, 30.0f));
    target    .setBounds (knobIn (920.0f, mainTop, 256.0f, Size::small).translated (0, -14));

    // Advanced: limiter timing and linking.
    {
        const auto x = timingGroup.x, top = timingGroup.top;
        auto kx = x + 16.0f;

        for (auto* k : { &lookahead, &attack, &release, &window, &linkTransients, &linkRelease })
        {
            k->setBounds (knobIn (kx, top, 80.0f, Size::small));
            kx += 86.0f;
        }

        adaptiveLink.setBounds (box (x + 548.0f, top + 84.0f, 148.0f, 28.0f));
    }

    // Advanced: tone.
    {
        const auto x = toneGroup.x, top = toneGroup.top;
        balance.setBounds (knobIn (x + 16.0f,  top, 90.0f, Size::small));
        clip   .setBounds (knobIn (x + 112.0f, top, 90.0f, Size::small));

        dcFilter .setBounds (box (x + 232.0f, top + 62.0f,  180.0f, 28.0f));
        sidechain.setBounds (box (x + 232.0f, top + 104.0f, 180.0f, 28.0f));
    }

    // Advanced: compressor.
    {
        const auto x = compressorGroup.x, top = compressorGroup.top;
        compOn  .setBounds (box (x + 16.0f, top + 48.0f, 120.0f, 28.0f));
        compMode.setBounds (box (x + 16.0f, top + 118.0f, 120.0f, 28.0f));

        auto kx = x + 150.0f;
        for (auto* k : { &compThreshold, &compRatio, &compAttack, &compRelease, &compHighPass })
        {
            k->setBounds (knobIn (kx, top, 80.0f, Size::small));
            kx += 80.0f;
        }
    }

    // Advanced: output.
    {
        const auto x = outputGroup.x, top = outputGroup.top;
        oversampling.setBounds (box (x + 16.0f,  top + 54.0f,  396.0f, 28.0f));
        wordLength  .setBounds (box (x + 16.0f,  top + 130.0f, 190.0f, 28.0f));
        dither      .setBounds (box (x + 222.0f, top + 130.0f, 190.0f, 28.0f));
    }
}

juce::String BreakwaterEditor::Face::caption() const
{
    if (processor.apvts.getRawParameterValue (pid::delta)->load() > 0.5f)
        return "DELTA - listening to what the limiter removes";

    if (engine.getSelected() == static_cast<int> (Engine::analog))
        return "ANALOG - no lookahead: transients pass with their shape, the true peak stage catches the rest";

    const auto& s = styles[(size_t) juce::jlimit (0, (int) styles.size() - 1, style.getSelected())];
    return juce::String (s.name).toUpperCase() + " - " + s.description;
}

juce::String BreakwaterEditor::Face::footer() const
{
    auto text = "CORRELATION " + juce::String (processor.meters.correlation.load(), 2)
                + "     LINK " + juce::String (juce::roundToInt (processor.meters.link.load() * 100.0f)) + " %";

    if (processor.apvts.getRawParameterValue (pid::compOn)->load() > 0.5f)
        text += "     COMP " + juce::String (-compressorReductionDb, 1) + " dB";

    if (processor.apvts.getRawParameterValue (pid::sidechain)->load() > 0.5f)
        text += "     SIDECHAIN";

    return text;
}

void BreakwaterEditor::Face::refresh()
{
    const auto ceilingDb = processor.apvts.getRawParameterValue (pid::ceiling)->load();
    const auto targetIndex = juce::jlimit (0, (int) loudnessTargets.size() - 1,
                                           juce::roundToInt (processor.apvts.getRawParameterValue (pid::target)->load()));
    const auto& t = loudnessTargets[(size_t) targetIndex];

    const auto compGr = processor.meters.compressorReduction.take();
    compressorReductionDb = compGr > compressorReductionDb ? compGr : juce::jmax (compGr, compressorReductionDb - 0.5f);

    meters.refresh();
    scope.refresh (ceilingDb, meters.getReductionDb(), caption(), footer());
    loudness.refresh (ceilingDb, t.lufs, t.name);

    // Controls that do nothing in the current mode stay in place, greyed, so
    // the panel does not rearrange under the hand.
    const auto engineIndex = engine.getSelected();
    const auto compressorIn = processor.apvts.getRawParameterValue (pid::compOn)->load() > 0.5f;

    if (engineIndex != shownEngine || compressorIn != shownCompressor)
    {
        shownEngine = engineIndex;
        shownCompressor = compressorIn;
        const auto modern = engineIndex == static_cast<int> (Engine::modern);

        style.setEnabled (modern);
        lookahead.setEnabled (modern);

        for (auto* c : std::initializer_list<juce::Component*> { &compMode, &compThreshold, &compRatio,
                                                                 &compAttack, &compRelease, &compHighPass })
            c->setEnabled (compressorIn);

        repaint();
    }

    if (advancedShown)
        repaint (box (compressorGroup.x + compressorGroup.width - 136.0f, compressorGroup.top + 40.0f, 120.0f, 130.0f));
}

void BreakwaterEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (panel::faceTop, 0.0f, 0.0f, panel::faceBottom, 0.0f, (float) designHeight, false));
    g.fillRect (bounds);

    // Header band.
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillRect (bounds.withHeight (headerHeight));
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (0.0f, headerHeight, bounds.getWidth(), 1.0f);

    // Wordmark: a swell running into a wall.
    juce::Path wave;
    wave.startNewSubPath (margin - 2.0f, 38.0f);
    wave.cubicTo (margin + 4.0f, 22.0f, margin + 12.0f, 20.0f, margin + 18.0f, 30.0f);
    wave.cubicTo (margin + 20.0f, 34.0f, margin + 15.0f, 36.0f, margin + 14.0f, 31.0f);
    g.setColour (panel::accent);
    g.strokePath (wave, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.fillRect (margin + 22.0f, 14.0f, 4.0f, 30.0f);
    g.fillRect (margin - 4.0f, 42.0f, 30.0f, 2.0f);

    panel::drawLabel (g, "BREAKWATER", { margin + 38.0f, 10.0f, 300.0f, 28.0f }, 24.0f, panel::text,
                      juce::Justification::centredLeft, true, 5.0f);
    panel::drawLabel (g, "MASTERING LIMITER  /  INDIEWAVE AUDIO", { margin + 39.0f, 36.0f, 280.0f, 12.0f }, 8.5f,
                      panel::textDim, juce::Justification::centredLeft, true, 1.6f);

    panel::drawLabel (g, "ENGINE", { 312.0f, 16.0f, 62.0f, 28.0f }, 9.0f, panel::textDim, juce::Justification::centredRight);

    panel::drawLabel (g, "STYLE", { margin, styleTop, 58.0f, 32.0f }, 9.5f,
                      shownEngine == static_cast<int> (Engine::modern) ? panel::textDim : panel::textFaint,
                      juce::Justification::centredLeft);

    // The main row, with hairlines between its three parts.
    paintGroup (g, { margin, mainTop, 1152.0f, mainHeight }, nullptr);

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    for (auto x : { 320.0f, 664.0f, 866.0f })
        g.fillRect (x, mainTop + 18.0f, 1.0f, mainHeight - 36.0f);

    panel::drawLabel (g, "MONITOR", { 690.0f, mainTop + 22.0f, 150.0f, 14.0f }, 8.5f, panel::textFaint,
                      juce::Justification::centred, true, 2.4f);

    if (! advancedShown)
        return;

    // The advanced panel.
    g.setColour (juce::Colours::black.withAlpha (0.22f));
    g.fillRect (0.0f, advancedTop, bounds.getWidth(), bounds.getHeight() - advancedTop);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (0.0f, advancedTop, bounds.getWidth(), 1.0f);

    for (const auto& group : { timingGroup, toneGroup, compressorGroup, outputGroup })
        paintGroup (g, { group.x, group.top, group.width, rowHeight - 8.0f }, group.name);

    // Compressor labels and its reduction.
    {
        const auto x = compressorGroup.x, top = compressorGroup.top;
        panel::drawLabel (g, "MODE", { x + 16.0f, top + 100.0f, 120.0f, 14.0f }, 8.5f,
                          shownCompressor ? panel::textDim : panel::textFaint);

        const juce::Rectangle<float> grArea (x + compressorGroup.width - 136.0f, top + 40.0f, 120.0f, 130.0f);
        panel::drawWell (g, grArea, 6.0f);
        panel::drawLabel (g, "COMP GR", grArea.withHeight (24.0f).translated (0.0f, 8.0f), 9.0f);
        panel::drawLabel (g, shownCompressor ? juce::String (-compressorReductionDb, 1) : juce::String ("--"),
                          grArea.withTrimmedTop (34.0f).withHeight (40.0f), 28.0f,
                          shownCompressor ? panel::reduction : panel::textFaint, juce::Justification::centred, true, 0.2f);
        panel::drawLabel (g, "dB", grArea.withTrimmedTop (74.0f).withHeight (16.0f), 9.0f, panel::textFaint);

        // A short bar under the number, 12 dB full scale.
        const auto bar = juce::Rectangle<float> (grArea.getX() + 14.0f, grArea.getBottom() - 26.0f, grArea.getWidth() - 28.0f, 6.0f);
        g.setColour (panel::well.brighter (0.15f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (panel::reduction);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, compressorReductionDb / 12.0f)), 3.0f);
    }

    // Output labels.
    {
        const auto x = outputGroup.x, top = outputGroup.top;
        panel::drawLabel (g, "OVERSAMPLING", { x + 16.0f,  top + 36.0f,  396.0f, 14.0f }, 8.5f);
        panel::drawLabel (g, "WORD LENGTH",  { x + 16.0f,  top + 112.0f, 190.0f, 14.0f }, 8.5f);
        panel::drawLabel (g, "DITHER",       { x + 222.0f, top + 112.0f, 190.0f, 14.0f }, 8.5f);
    }
}

// ---------------------------------------------------------------------------

BreakwaterEditor::BreakwaterEditor (BreakwaterProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      face (p)
{
    addAndMakeVisible (face);
    face.setBounds (0, 0, designWidth, designHeight);

    face.advanced.onClick = [this] { setAdvanced (face.advanced.getToggleState()); };

    setResizable (true, true);
    setSize (designWidth * 4 / 5, designHeight * 4 / 5);
    setAdvanced (processor.apvts.state.getProperty (advancedProperty, false));

    startTimerHz (30);
}

BreakwaterEditor::~BreakwaterEditor()
{
    stopTimer();
}

void BreakwaterEditor::setAdvanced (bool shouldShow)
{
    processor.apvts.state.setProperty (advancedProperty, shouldShow, nullptr);

    const auto scale = (double) getWidth() / designWidth;
    const auto height = shouldShow ? designHeightAdvanced : designHeight;

    face.showAdvanced (shouldShow);

    setResizeLimits (designWidth / 2, height / 2, designWidth * 2, height * 2);

    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) designWidth / height);

    setSize (getWidth(), juce::roundToInt (scale * height));
    resized();
}

void BreakwaterEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::faceBottom);
}

void BreakwaterEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) designWidth,
                                                     (float) getHeight() / (float) face.getHeight()));
}

void BreakwaterEditor::timerCallback()
{
    face.refresh();
}

} // namespace breakwater
