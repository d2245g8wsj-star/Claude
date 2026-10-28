#include "PluginEditor.h"

namespace breakwater
{

namespace
{
    using Size = panel::Knob::Size;

    // Vertical plan.
    constexpr float headerHeight = 58.0f;
    constexpr float styleTop     = 66.0f;
    constexpr float scopeTop     = 108.0f;
    constexpr float scopeBottom  = 440.0f;
    constexpr float rowTop[]     = { 452.0f, 658.0f };
    constexpr float rowHeight    = 196.0f;

    // Horizontal plan.
    constexpr float margin    = 24.0f;
    constexpr float sideX     = 920.0f;
    constexpr float sideWidth = 256.0f;

    struct Group { float x, top, width; const char* name; };
    constexpr Group limiterGroup    { 24.0f,  rowTop[0], 712.0f, "LIMITER" };
    constexpr Group shapeGroup      { 748.0f, rowTop[0], 428.0f, "SHAPE" };
    constexpr Group compressorGroup { 24.0f,  rowTop[1], 712.0f, "COMPRESSOR" };
    constexpr Group outputGroup     { 748.0f, rowTop[1], 428.0f, "OUTPUT" };

    juce::Rectangle<int> knobBounds (float x, float top, float width, Size size)
    {
        const auto y      = top + (size == Size::large ? 24.0f : 32.0f);
        const auto height = size == Size::large ? 160.0f : 138.0f;
        return juce::Rectangle<float> (x, y, width, height).toNearestInt();
    }

    juce::Rectangle<int> box (float x, float y, float w, float h)
    {
        return juce::Rectangle<float> (x, y, w, h).toNearestInt();
    }
}

// ---------------------------------------------------------------------------

BreakwaterEditor::Face::Face (BreakwaterProcessor& p)
    : processor (p),
      engine       (p.apvts, pid::engine),
      oversampling (p.apvts, pid::oversampling, { "1x", "2x", "4x", "8x", "16x", "32x" }),
      style        (p.apvts, pid::style),
      compMode     (p.apvts, pid::compMode, { "Stereo", "M/S" }),
      wordLength   (p.apvts, pid::wordLength, { "Off", "24", "20", "16" }),
      dither       (p.apvts, pid::dither, { "None", "TPDF", "Shape" }),
      truePeak     (p.apvts, pid::truePeak,     "True Peak"),
      bypass       (p.apvts, pid::bypass,       "Bypass", panel::warning),
      adaptiveLink (p.apvts, pid::adaptiveLink, "Adaptive Link"),
      compOn       (p.apvts, pid::compOn,       "Comp In"),
      dcFilter     (p.apvts, pid::dcFilter,     "DC Filter"),
      match        (p.apvts, pid::match,        "Gain Match"),
      delta        (p.apvts, pid::delta,        "Delta", panel::reduction),
      sidechain    (p.apvts, pid::sidechain,    "Ext Sidechain"),
      gain           (p.apvts, pid::gain,           "Gain",       Size::large),
      ceiling        (p.apvts, pid::ceiling,        "Ceiling",    Size::large),
      lookahead      (p.apvts, pid::lookahead,      "Lookahead",  Size::small),
      attack         (p.apvts, pid::attack,         "Attack",     Size::small),
      release        (p.apvts, pid::release,        "Release",    Size::small),
      window         (p.apvts, pid::window,         "Window",     Size::small),
      linkTransients (p.apvts, pid::linkTransients, "Link Trans", Size::small),
      linkRelease    (p.apvts, pid::linkRelease,    "Link Rel",   Size::small),
      transients     (p.apvts, pid::transients,     "Transients", Size::small, true),
      lowPunch       (p.apvts, pid::lowPunch,       "Low Punch",  Size::small, true),
      detail         (p.apvts, pid::detail,         "Detail",     Size::small),
      balance        (p.apvts, pid::balance,        "Balance",    Size::small, true),
      clip           (p.apvts, pid::clip,           "Clip",       Size::small),
      compThreshold  (p.apvts, pid::compThreshold,  "Threshold",  Size::small),
      compRatio      (p.apvts, pid::compRatio,      "Ratio",      Size::small),
      compAttack     (p.apvts, pid::compAttack,     "Attack",     Size::small),
      compRelease    (p.apvts, pid::compRelease,    "Release",    Size::small),
      compHighPass   (p.apvts, pid::compHighPass,   "SC HPF",     Size::small),
      target         (p.apvts, pid::target,         "Target",     Size::small),
      scope    (p.meters),
      meters   (p.meters),
      loudness (p.meters)
{
    juce::StringArray styleDescriptions;
    for (const auto& s : styles)
        styleDescriptions.add (s.description);

    style.setDescriptions (styleDescriptions);
    engine.setDescriptions ({ "Lookahead limiting with a leveller and eight styles",
                              "No lookahead: transients keep their shape, like a circuit" });

    window.setTooltip ("Holds the detector for a quarter of a note's period, so a fast limiter "
                       "does not follow the cycles of a bass note. C0 is the longest.");
    balance.setTooltip ("How the low end and the rest share the reduction: towards Lows the top stays "
                        "bright, towards Highs the low end stays full");
    adaptiveLink.setTooltip ("Links the channels more as they become more alike, measured by correlation");
    sidechain.setTooltip ("Detect on the sidechain input instead of the programme. "
                          "The true peak stage still watches the programme.");

    for (auto* c : std::initializer_list<juce::Component*> {
             &scope, &meters, &loudness,
             &engine, &oversampling, &style, &compMode, &wordLength, &dither,
             &truePeak, &bypass, &adaptiveLink, &compOn, &dcFilter, &match, &delta, &sidechain,
             &gain, &ceiling, &lookahead, &attack, &release, &window, &linkTransients, &linkRelease,
             &transients, &lowPunch, &detail, &balance, &clip,
             &compThreshold, &compRatio, &compAttack, &compRelease, &compHighPass, &target })
        addAndMakeVisible (c);

    refresh();
}

void BreakwaterEditor::Face::resized()
{
    engine      .setBounds (box (380.0f, 16.0f, 180.0f, 28.0f));
    oversampling.setBounds (box (660.0f, 16.0f, 240.0f, 28.0f));
    truePeak    .setBounds (box (920.0f, 16.0f, 120.0f, 28.0f));
    bypass      .setBounds (box (1056.0f, 16.0f, 120.0f, 28.0f));

    style.setBounds (box (margin + 64.0f, styleTop, 908.0f - margin - 64.0f, 32.0f));

    scope   .setBounds (box (margin, scopeTop, 884.0f, scopeBottom - scopeTop));
    meters  .setBounds (box (sideX, styleTop, sideWidth, 170.0f));
    loudness.setBounds (box (sideX, 244.0f, sideWidth, scopeBottom - 244.0f));

    // Limiter.
    {
        const auto x = limiterGroup.x, top = limiterGroup.top;
        gain   .setBounds (knobBounds (x + 8.0f,   top, 100.0f, Size::large));
        ceiling.setBounds (knobBounds (x + 112.0f, top, 100.0f, Size::large));

        auto kx = x + 220.0f;
        for (auto* k : { &lookahead, &attack, &release, &window, &linkTransients, &linkRelease })
        {
            k->setBounds (knobBounds (kx, top, 76.0f, Size::small));
            kx += 80.0f;
        }

        adaptiveLink.setBounds (box (x + limiterGroup.width - 150.0f, top + 6.0f, 140.0f, 24.0f));
    }

    // Shape.
    {
        auto kx = shapeGroup.x + 6.0f;
        for (auto* k : { &transients, &lowPunch, &detail, &balance, &clip })
        {
            k->setBounds (knobBounds (kx, shapeGroup.top, 80.0f, Size::small));
            kx += 84.0f;
        }
    }

    // Compressor.
    {
        const auto x = compressorGroup.x, top = compressorGroup.top;
        compOn  .setBounds (box (x + 16.0f, top + 48.0f, 120.0f, 28.0f));
        compMode.setBounds (box (x + 16.0f, top + 118.0f, 120.0f, 28.0f));

        auto kx = x + 150.0f;
        for (auto* k : { &compThreshold, &compRatio, &compAttack, &compRelease, &compHighPass })
        {
            k->setBounds (knobBounds (kx, top, 80.0f, Size::small));
            kx += 80.0f;
        }
    }

    // Output.
    {
        const auto x = outputGroup.x, top = outputGroup.top;
        dcFilter .setBounds (box (x + 12.0f,  top + 36.0f, 116.0f, 28.0f));
        match    .setBounds (box (x + 12.0f,  top + 72.0f, 116.0f, 28.0f));
        delta    .setBounds (box (x + 136.0f, top + 36.0f, 116.0f, 28.0f));
        sidechain.setBounds (box (x + 136.0f, top + 72.0f, 116.0f, 28.0f));

        wordLength.setBounds (box (x + 12.0f,  top + 142.0f, 116.0f, 26.0f));
        dither    .setBounds (box (x + 136.0f, top + 142.0f, 116.0f, 26.0f));

        target.setBounds (knobBounds (x + 262.0f, top, 156.0f, Size::small));
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

    meters.refresh();
    scope.refresh (ceilingDb, meters.getReductionDb(), caption(), footer());
    loudness.refresh (ceilingDb, t.lufs, t.name);

    const auto compGr = processor.meters.compressorReduction.take();
    compressorReductionDb = compGr > compressorReductionDb ? compGr : juce::jmax (compGr, compressorReductionDb - 0.5f);

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
    }

    repaint (box (compressorGroup.x + compressorGroup.width - 160.0f, compressorGroup.top + 30.0f, 150.0f, 150.0f));
}

void BreakwaterEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (panel::faceTop, 0.0f, 0.0f, panel::faceBottom, 0.0f, bounds.getBottom(), false));
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
    panel::drawLabel (g, "OVERSAMPLING", { 564.0f, 16.0f, 90.0f, 28.0f }, 9.0f, panel::textDim, juce::Justification::centredRight);

    panel::drawLabel (g, "STYLE", { margin, styleTop, 58.0f, 32.0f }, 9.5f,
                      shownEngine == static_cast<int> (Engine::modern) ? panel::textDim : panel::textFaint,
                      juce::Justification::centredLeft);

    // Control groups.
    for (const auto& group : { limiterGroup, shapeGroup, compressorGroup, outputGroup })
    {
        const juce::Rectangle<float> area (group.x, group.top, group.width, rowHeight - 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        panel::drawLabel (g, group.name, { group.x + 14.0f, group.top + 8.0f, 200.0f, 16.0f }, 9.0f, panel::textFaint,
                          juce::Justification::centredLeft, true, 2.4f);
    }

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
        panel::drawLabel (g, "WORD LENGTH", { x + 12.0f,  top + 124.0f, 116.0f, 14.0f }, 8.5f);
        panel::drawLabel (g, "DITHER",      { x + 136.0f, top + 124.0f, 116.0f, 14.0f }, 8.5f);
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

    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);

    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) designWidth / designHeight);

    setSize (designWidth * 4 / 5, designHeight * 4 / 5);
    startTimerHz (30);
}

BreakwaterEditor::~BreakwaterEditor()
{
    stopTimer();
}

void BreakwaterEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::faceBottom);
}

void BreakwaterEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / designWidth,
                                                     (float) getHeight() / designHeight));
}

void BreakwaterEditor::timerCallback()
{
    face.refresh();
}

} // namespace breakwater
