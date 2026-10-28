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
    constexpr float controlsTop  = 452.0f;
    constexpr float controlsBottom = 646.0f;

    // Horizontal plan.
    constexpr float margin     = 24.0f;
    constexpr float sideX      = 920.0f;
    constexpr float sideWidth  = 256.0f;

    struct Group { float x, width; const char* name; };
    constexpr Group limiterGroup { 24.0f,  612.0f, "LIMITER" };
    constexpr Group shapeGroup   { 648.0f, 256.0f, "SHAPE" };
    constexpr Group outputGroup  { 916.0f, 260.0f, "OUTPUT" };

    juce::Rectangle<int> knobBounds (float x, float width, Size size)
    {
        const auto top    = controlsTop + (size == Size::large ? 22.0f : 30.0f);
        const auto height = size == Size::large ? 160.0f : 138.0f;
        return juce::Rectangle<float> (x, top, width, height).toNearestInt();
    }
}

// ---------------------------------------------------------------------------

BreakwaterEditor::Face::Face (BreakwaterProcessor& p)
    : processor (p),
      engine       (p.apvts, pid::engine),
      oversampling (p.apvts, pid::oversampling, { "1x", "2x", "4x", "8x", "16x" }),
      style        (p.apvts, pid::style),
      wordLength   (p.apvts, pid::wordLength, { "Off", "24", "20", "16" }),
      dither       (p.apvts, pid::dither, { "None", "TPDF", "Shape" }),
      truePeak     (p.apvts, pid::truePeak, "True Peak"),
      bypass       (p.apvts, pid::bypass,   "Bypass", panel::warning),
      dcFilter     (p.apvts, pid::dcFilter, "DC Filter"),
      match        (p.apvts, pid::match,    "Gain Match"),
      delta        (p.apvts, pid::delta,    "Delta", panel::reduction),
      gain           (p.apvts, pid::gain,           "Gain",      Size::large),
      ceiling        (p.apvts, pid::ceiling,        "Ceiling",   Size::large),
      lookahead      (p.apvts, pid::lookahead,      "Lookahead", Size::small),
      attack         (p.apvts, pid::attack,         "Attack",    Size::small),
      release        (p.apvts, pid::release,        "Release",   Size::small),
      linkTransients (p.apvts, pid::linkTransients, "Link Trans", Size::small),
      linkRelease    (p.apvts, pid::linkRelease,    "Link Rel",  Size::small),
      transients     (p.apvts, pid::transients,     "Transients", Size::small, true),
      detail         (p.apvts, pid::detail,         "Detail",    Size::small),
      clip           (p.apvts, pid::clip,           "Clip",      Size::small),
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

    for (auto* c : std::initializer_list<juce::Component*> {
             &scope, &meters, &loudness,
             &engine, &oversampling, &style, &wordLength, &dither,
             &truePeak, &bypass, &dcFilter, &match, &delta,
             &gain, &ceiling, &lookahead, &attack, &release, &linkTransients, &linkRelease,
             &transients, &detail, &clip })
        addAndMakeVisible (c);

    refresh();
}

void BreakwaterEditor::Face::resized()
{
    engine      .setBounds (juce::Rectangle<float> (330.0f, 16.0f, 190.0f, 28.0f).toNearestInt());
    oversampling.setBounds (juce::Rectangle<float> (636.0f, 16.0f, 220.0f, 28.0f).toNearestInt());
    truePeak    .setBounds (juce::Rectangle<float> (920.0f, 16.0f, 120.0f, 28.0f).toNearestInt());
    bypass      .setBounds (juce::Rectangle<float> (1056.0f, 16.0f, 120.0f, 28.0f).toNearestInt());

    style.setBounds (juce::Rectangle<float> (margin + 64.0f, styleTop, 908.0f - margin - 64.0f, 32.0f).toNearestInt());

    scope   .setBounds (juce::Rectangle<float> (margin, scopeTop, 884.0f, scopeBottom - scopeTop).toNearestInt());
    meters  .setBounds (juce::Rectangle<float> (sideX, styleTop, sideWidth, 196.0f).toNearestInt());
    loudness.setBounds (juce::Rectangle<float> (sideX, 270.0f, sideWidth, scopeBottom - 270.0f).toNearestInt());

    gain   .setBounds (knobBounds (limiterGroup.x + 8.0f,   100.0f, Size::large));
    ceiling.setBounds (knobBounds (limiterGroup.x + 112.0f, 100.0f, Size::large));

    float x = limiterGroup.x + 220.0f;
    for (auto* k : { &lookahead, &attack, &release, &linkTransients, &linkRelease })
    {
        k->setBounds (knobBounds (x, 76.0f, Size::small));
        x += 78.0f;
    }

    x = shapeGroup.x + 8.0f;
    for (auto* k : { &transients, &detail, &clip })
    {
        k->setBounds (knobBounds (x, 76.0f, Size::small));
        x += 82.0f;
    }

    const auto toggleX = outputGroup.x + 12.0f;
    dcFilter.setBounds (juce::Rectangle<float> (toggleX, controlsTop + 38.0f,  108.0f, 28.0f).toNearestInt());
    match   .setBounds (juce::Rectangle<float> (toggleX, controlsTop + 82.0f,  108.0f, 28.0f).toNearestInt());
    delta   .setBounds (juce::Rectangle<float> (toggleX, controlsTop + 126.0f, 108.0f, 28.0f).toNearestInt());

    const auto selectX = outputGroup.x + 130.0f;
    wordLength.setBounds (juce::Rectangle<float> (selectX, controlsTop + 52.0f,  outputGroup.width - 142.0f, 26.0f).toNearestInt());
    dither    .setBounds (juce::Rectangle<float> (selectX, controlsTop + 116.0f, outputGroup.width - 142.0f, 26.0f).toNearestInt());
}

juce::String BreakwaterEditor::Face::caption() const
{
    if (processor.apvts.getRawParameterValue (pid::delta)->load() > 0.5f)
        return "DELTA - listening to what the limiter removes";

    if (engine.getSelected() == static_cast<int> (Engine::analog))
        return "ANALOG - no lookahead: transients pass with their shape, the final stage catches the rest";

    const auto& s = styles[(size_t) juce::jlimit (0, (int) styles.size() - 1, style.getSelected())];
    return juce::String (s.name).toUpperCase() + " - " + s.description;
}

void BreakwaterEditor::Face::refresh()
{
    const auto ceilingDb = processor.apvts.getRawParameterValue (pid::ceiling)->load();

    meters.refresh();
    scope.refresh (ceilingDb, meters.getReductionDb(), caption());
    loudness.refresh (ceilingDb);

    // The analog engine has no lookahead and no styles: their controls stay
    // in place, greyed, so the panel does not rearrange under the hand.
    const auto engineIndex = engine.getSelected();

    if (engineIndex != shownEngine)
    {
        shownEngine = engineIndex;
        const auto modern = engineIndex == static_cast<int> (Engine::modern);

        style.setEnabled (modern);
        lookahead.setEnabled (modern);
        repaint();
    }
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

    // Wordmark: the name, and a peak meeting its ceiling.
    juce::Path mark;
    mark.startNewSubPath (margin, 42.0f);
    mark.lineTo (margin + 11.0f, 18.0f);
    mark.lineTo (margin + 22.0f, 42.0f);
    g.setColour (panel::accent);
    g.strokePath (mark, juce::PathStrokeType (3.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    g.fillRect (margin - 2.0f, 13.0f, 26.0f, 2.0f);

    panel::drawLabel (g, "BREAKWATER", { margin + 34.0f, 10.0f, 200.0f, 28.0f }, 26.0f, panel::text,
                      juce::Justification::centredLeft, true, 7.0f);
    panel::drawLabel (g, "MASTERING LIMITER  /  INDIEWAVE AUDIO", { margin + 35.0f, 36.0f, 260.0f, 12.0f }, 8.5f,
                      panel::textDim, juce::Justification::centredLeft, true, 1.6f);

    panel::drawLabel (g, "ENGINE", { 262.0f, 16.0f, 62.0f, 28.0f }, 9.0f, panel::textDim, juce::Justification::centredRight);
    panel::drawLabel (g, "OVERSAMPLING", { 540.0f, 16.0f, 90.0f, 28.0f }, 9.0f, panel::textDim, juce::Justification::centredRight);

    panel::drawLabel (g, "STYLE", { margin, styleTop, 58.0f, 32.0f }, 9.5f,
                      shownEngine == static_cast<int> (Engine::modern) ? panel::textDim : panel::textFaint,
                      juce::Justification::centredLeft);

    // Control groups.
    for (const auto& group : { limiterGroup, shapeGroup, outputGroup })
    {
        const juce::Rectangle<float> area (group.x, controlsTop, group.width, controlsBottom - controlsTop);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);

        panel::drawLabel (g, group.name, area.withHeight (20.0f).translated (0.0f, 4.0f), 9.0f, panel::textFaint,
                          juce::Justification::centred, true, 2.4f);
    }

    const auto selectX = outputGroup.x + 130.0f;
    const auto selectWidth = outputGroup.width - 142.0f;
    panel::drawLabel (g, "WORD LENGTH", { selectX, controlsTop + 36.0f, selectWidth, 14.0f }, 8.5f);
    panel::drawLabel (g, "DITHER",      { selectX, controlsTop + 100.0f, selectWidth, 14.0f }, 8.5f);
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

    setSize (designWidth, designHeight);
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
