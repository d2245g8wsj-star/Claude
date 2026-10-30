#include "PluginEditor.h"

namespace lacquer
{

namespace
{
    using Size = panel::SteppedKnob::Size;
    using panel::SteppedKnob;

    constexpr float rowTop[]     = { 22.0f, 350.0f };
    constexpr float stripHeight  = 268.0f;

    // Horizontal plan of a channel, left to right.
    constexpr float lowBoostX    = 140.0f, lowCutX    = 268.0f;
    constexpr float midBoostX    = 420.0f, midCutX    = 548.0f;
    constexpr float trebleBoostX = 700.0f, trebleQX   = 806.0f, trebleCutX = 912.0f;
    constexpr float toggleX[]    = { 1046.0f, 1120.0f, 1194.0f, 1268.0f };
    constexpr float numeralX     = 1322.0f;

    // Vertical plan, from the top of a channel.
    constexpr float glyphY  = 18.0f;
    constexpr float freqY   = 78.0f;
    constexpr float qY      = 137.0f;
    constexpr float amountY = 196.0f;
    constexpr float toggleGlyphY = 92.0f;
    constexpr float toggleY      = 150.0f;

    constexpr float freqSide = 104.0f, amountSide = 124.0f, qSide = 88.0f;

    constexpr float earWidth = 44.0f;
    constexpr float middleTop = 292.0f, middleBottom = 348.0f;

    juce::Rectangle<int> square (float centreX, float centreY, float side)
    {
        return juce::Rectangle<float> (side, side).withCentre ({ centreX, centreY }).toNearestInt();
    }

    std::vector<SteppedKnob::Mark> amountMarks (panel::Ink ink)
    {
        return SteppedKnob::rampMarks (voicing::amountSteps + 1, ink);
    }

    std::vector<SteppedKnob::Mark> midCutMarks()
    {
        auto marks = SteppedKnob::evenMarks ((int) voicing::midCutHz.size(), panel::Ink::midCut);
        marks[0] = { panel::Ink::neutral, 1.0f, true };    // OUT
        return marks;
    }

    std::vector<SteppedKnob::Mark> trebleBoostMarks()
    {
        std::vector<SteppedKnob::Mark> marks;

        for (int i = 0; i < (int) voicing::trebleBoostHz.size(); ++i)
        {
            const auto bell = voicing::trebleIsBell (i);
            // The two 10 kHz bells differ by their coil: the high inductance
            // one is drawn larger.
            const auto size = i == 3 ? 0.75f : i == 4 ? 1.3f : 1.0f;
            marks.push_back ({ bell ? panel::Ink::trebleBell : panel::Ink::trebleShelf, size, false });
        }

        return marks;
    }

    std::vector<SteppedKnob::Mark> qMarks()
    {
        std::vector<SteppedKnob::Mark> marks;

        for (int i = 0; i <= voicing::qSteps; ++i)
            marks.push_back ({ panel::Ink::neutral, 0.55f + 0.06f * (float) i, false });

        return marks;
    }
}

// ---------------------------------------------------------------------------

LacquerEditor::Strip::Strip (LacquerProcessor& p, int index)
    : lowBoostFreq      (p.apvts, pid::forStrip (index, pid::lowBoostFreq),      Size::medium,
                         SteppedKnob::evenMarks ((int) voicing::lowBoostHz.size(), panel::Ink::bass)),
      lowBoostAmount    (p.apvts, pid::forStrip (index, pid::lowBoostAmount),    Size::large, amountMarks (panel::Ink::bass)),
      lowCutFreq        (p.apvts, pid::forStrip (index, pid::lowCutFreq),        Size::medium,
                         SteppedKnob::evenMarks ((int) voicing::lowCutHz.size(), panel::Ink::bassCut)),
      lowCutAmount      (p.apvts, pid::forStrip (index, pid::lowCutAmount),      Size::large, amountMarks (panel::Ink::bassCut)),
      midBoostFreq      (p.apvts, pid::forStrip (index, pid::midBoostFreq),      Size::medium,
                         SteppedKnob::evenMarks ((int) voicing::midBoostHz.size(), panel::Ink::midBoost)),
      midBoostAmount    (p.apvts, pid::forStrip (index, pid::midBoostAmount),    Size::large, amountMarks (panel::Ink::midBoost)),
      midCutFreq        (p.apvts, pid::forStrip (index, pid::midCutFreq),        Size::medium, midCutMarks()),
      midCutAmount      (p.apvts, pid::forStrip (index, pid::midCutAmount),      Size::large, amountMarks (panel::Ink::midCut)),
      trebleBoostFreq   (p.apvts, pid::forStrip (index, pid::trebleBoostFreq),   Size::medium, trebleBoostMarks()),
      trebleQ           (p.apvts, pid::forStrip (index, pid::trebleQ),           Size::small,  qMarks()),
      trebleBoostAmount (p.apvts, pid::forStrip (index, pid::trebleBoostAmount), Size::large, amountMarks (panel::Ink::trebleBell)),
      trebleCutFreq     (p.apvts, pid::forStrip (index, pid::trebleCutFreq),     Size::medium,
                         SteppedKnob::evenMarks ((int) voicing::trebleCutHz.size(), panel::Ink::trebleCut)),
      trebleCutAmount   (p.apvts, pid::forStrip (index, pid::trebleCutAmount),   Size::large, amountMarks (panel::Ink::trebleCut)),
      shift             (p.apvts, pid::forStrip (index, pid::shift)),
      buffer            (p.apvts, pid::forStrip (index, pid::buffer)),
      transformer       (p.apvts, pid::forStrip (index, pid::transformer)),
      eqIn              (p.apvts, pid::forStrip (index, pid::eqIn))
{
}

std::vector<juce::Component*> LacquerEditor::Strip::components()
{
    return { &lowBoostFreq, &lowBoostAmount, &lowCutFreq, &lowCutAmount,
             &midBoostFreq, &midBoostAmount, &midCutFreq, &midCutAmount,
             &trebleBoostFreq, &trebleQ, &trebleBoostAmount, &trebleCutFreq, &trebleCutAmount,
             &shift, &buffer, &transformer, &eqIn };
}

// ---------------------------------------------------------------------------

LacquerEditor::Face::Face (LacquerProcessor& p)
    : processor (p),
      link   (p.apvts, pid::link),
      output (p.apvts, pid::output, Size::small, SteppedKnob::evenMarks (25, panel::Ink::neutral))
{
    finishSelector.onChange = [this] (panel::Finish f)
    {
        processor.setFinish ((int) f);
        applyFinish (f);
    };

    for (int i = 0; i < numChannelStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<Strip> (p, i);

        for (auto* child : strips[(size_t) i]->components())
            addAndMakeVisible (child);
    }

    addAndMakeVisible (link);
    addAndMakeVisible (output);
    addAndMakeVisible (pilot);
    addAndMakeVisible (finishSelector);
    applyFinish ((panel::Finish) processor.getFinish());
    refresh();
}

void LacquerEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        layoutStrip (*strips[(size_t) i], rowTop[i]);

    const auto middle = 0.5f * (middleTop + middleBottom);
    pilot .setBounds (square (112.0f, middle, 30.0f));
    link  .setBounds (juce::Rectangle<float> (40.0f, 50.0f).withCentre ({ 1112.0f, middle }).toNearestInt());
    output.setBounds (square (1240.0f, middle, 64.0f));
    finishSelector.setBounds (juce::Rectangle<float> (56.0f, 28.0f).withCentre ({ 300.0f, middle }).toNearestInt());
}

void LacquerEditor::Face::layoutStrip (Strip& s, float top)
{
    s.lowBoostFreq     .setBounds (square (lowBoostX,    top + freqY,   freqSide));
    s.lowBoostAmount   .setBounds (square (lowBoostX,    top + amountY, amountSide));
    s.lowCutFreq       .setBounds (square (lowCutX,      top + freqY,   freqSide));
    s.lowCutAmount     .setBounds (square (lowCutX,      top + amountY, amountSide));
    s.midBoostFreq     .setBounds (square (midBoostX,    top + freqY,   freqSide));
    s.midBoostAmount   .setBounds (square (midBoostX,    top + amountY, amountSide));
    s.midCutFreq       .setBounds (square (midCutX,      top + freqY,   freqSide));
    s.midCutAmount     .setBounds (square (midCutX,      top + amountY, amountSide));
    s.trebleBoostFreq  .setBounds (square (trebleBoostX, top + freqY,   freqSide));
    s.trebleQ          .setBounds (square (trebleQX,     top + qY,      qSide));
    s.trebleBoostAmount.setBounds (square (trebleBoostX, top + amountY, amountSide));
    s.trebleCutFreq    .setBounds (square (trebleCutX,   top + freqY,   freqSide));
    s.trebleCutAmount  .setBounds (square (trebleCutX,   top + amountY, amountSide));

    const std::array<panel::Toggle*, 4> toggles { &s.shift, &s.buffer, &s.transformer, &s.eqIn };

    for (size_t i = 0; i < toggles.size(); ++i)
        toggles[i]->setBounds (juce::Rectangle<float> (66.0f, 84.0f).withCentre ({ toggleX[i], top + toggleY }).toNearestInt());
}

void LacquerEditor::Face::applyFinish (panel::Finish f)
{
    finish  = f;
    palette = &panel::Palette::forFinish (f);
    finishSelector.setFinish (f);

    const auto apply = [this] (juce::Component* c)
    {
        if (auto* knob = dynamic_cast<panel::SteppedKnob*> (c))  knob->setPalette (*palette);
        if (auto* toggle = dynamic_cast<panel::Toggle*> (c))     toggle->setPalette (*palette);
    };

    for (auto& strip : strips)
        for (auto* child : strip->components())
            apply (child);

    apply (&link);
    apply (&output);
    repaint();
}

void LacquerEditor::Face::refresh()
{
    // The finish lives in the plugin state, so a session or preset recall
    // can change it under an open editor.
    if (const auto stored = (panel::Finish) processor.getFinish(); stored != finish)
        applyFinish (stored);

    // The pilot is the power lamp: lit unless the host has bypassed the unit.
    pilot.setLit (processor.apvts.getRawParameterValue (pid::bypass)->load() < 0.5f);

    const auto linked = processor.apvts.getRawParameterValue (pid::link)->load() > 0.5f ? 1 : 0;

    if (linked == shownLink)
        return;

    shownLink = linked;

    // Linked, channel 1 sets both sides; channel 2's switches stay where they
    // were but are greyed, so they cannot look as if they did something.
    for (auto* child : strips[1]->components())
        child->setEnabled (linked == 0);

    repaint();
}

void LacquerEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (juce::ColourGradient (palette->plateTop, 0.0f, 0.0f,
                                             palette->plateBottom, 0.0f, bounds.getBottom(), false));
    g.fillRect (bounds);

    // The coating's speckle, seeded so the plate is the same plate every
    // time it is drawn.
    juce::Random grain (0x4c4143);

    for (int i = 0; i < 5200; ++i)
    {
        const auto x = grain.nextFloat() * bounds.getWidth();
        const auto y = grain.nextFloat() * bounds.getHeight();
        const auto dark = grain.nextBool();
        g.setColour (dark ? palette->speckDark : palette->speckLight);
        g.fillRect (x, y, 1.2f, 1.2f);
    }

    // A soft vignette, as a large flat plate falls off under studio light.
    g.setGradientFill (juce::ColourGradient (juce::Colours::transparentBlack, bounds.getCentreX(), bounds.getCentreY(),
                                             juce::Colours::black.withAlpha (0.06f), 0.0f, 0.0f, true));
    g.fillRect (bounds);

    // Gloss: the black lacquer carries a broad, soft reflection of the room
    // light across its upper half.
    if (finish == panel::Finish::black)
    {
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.0f), 0.0f, 0.0f,
                                    juce::Colours::white.withAlpha (0.0f), bounds.getWidth() * 0.55f, bounds.getHeight(), false);
        sheen.addColour (0.30, juce::Colours::white.withAlpha (0.055f));
        sheen.addColour (0.42, juce::Colours::white.withAlpha (0.015f));
        g.setGradientFill (sheen);
        g.fillRect (bounds);
    }

    // The edge of the plate.
    g.setColour (palette->lip);
    g.drawRect (bounds.reduced (0.5f), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.drawLine (0.0f, bounds.getBottom() - 0.5f, bounds.getRight(), bounds.getBottom() - 0.5f, 1.0f);

    panel::drawRackEar (g, *palette, bounds.withWidth (earWidth));
    panel::drawRackEar (g, *palette, bounds.withLeft (bounds.getRight() - earWidth));

    // The bar between the channels, engraved top and bottom.
    for (auto y : { middleTop, middleBottom })
    {
        g.setColour (palette->lip);
        g.fillRect (64.0f, y + 0.8f, bounds.getWidth() - 128.0f, 1.0f);
        g.setColour (palette->engravingDim.withAlpha (0.8f));
        g.fillRect (64.0f, y, bounds.getWidth() - 128.0f, 1.0f);
    }

    const auto middle = 0.5f * (middleTop + middleBottom);

    // Name, in a script cut into the bar.
    {
        juce::GlyphArrangement name;
        auto font = juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 34.0f,
                                                   juce::Font::bold | juce::Font::italic));
        name.addFittedText (font, "Lacquer", 540.0f, middle - 24.0f, 320.0f, 36.0f, juce::Justification::centred, 1);

        g.setColour (palette->lip);
        name.draw (g, juce::AffineTransform::translation (0.0f, 1.0f));
        g.setColour (palette->engraving);
        name.draw (g);
    }

    panel::drawTrackedLabel (g, *palette, "INDIEWAVE AUDIO", { 540.0f, middle + 11.0f, 320.0f, 10.0f }, 7.5f, 3.0f,
                             juce::Justification::centred, palette->engravingDim);

    panel::drawGlyph (g, *palette, panel::Glyph::power,  { 136.0f, middle - 9.0f, 18.0f, 18.0f }, palette->engraving);
    panel::drawGlyph (g, *palette, panel::Glyph::link,   { 1062.0f, middle - 10.0f, 30.0f, 20.0f }, palette->engraving);
    panel::drawGlyph (g, *palette, panel::Glyph::output, { 1180.0f, middle - 9.0f, 24.0f, 18.0f }, palette->engraving);

    for (int i = 0; i < numChannelStrips; ++i)
        paintStripMarkings (g, i, rowTop[i]);
}

void LacquerEditor::Face::paintStripMarkings (juce::Graphics& g, int index, float top) const
{
    const auto dimmed = index == 1 && shownLink == 1;
    const auto tint = [dimmed] (juce::Colour c) { return dimmed ? c.withSaturation (0.0f).withAlpha (0.4f) : c; };
    const auto ink  = [this] (panel::Ink i) { return palette->ink (i); };

    const auto glyphBox = [top] (float x) { return juce::Rectangle<float> (46.0f, 22.0f).withCentre ({ x, top + glyphY }); };

    panel::drawGlyph (g, *palette, panel::Glyph::lowShelfBoost, glyphBox (lowBoostX),    tint (ink (panel::Ink::bass)));
    panel::drawGlyph (g, *palette, panel::Glyph::lowShelfCut,   glyphBox (lowCutX),      tint (ink (panel::Ink::bassCut)));
    panel::drawGlyph (g, *palette, panel::Glyph::bellBoost,     glyphBox (midBoostX),    tint (ink (panel::Ink::midBoost)));
    panel::drawGlyph (g, *palette, panel::Glyph::bellCut,       glyphBox (midCutX),      tint (ink (panel::Ink::midCut)));
    panel::drawGlyph (g, *palette, panel::Glyph::trebleBoost,   glyphBox (trebleBoostX), tint (ink (panel::Ink::trebleBell)));
    panel::drawGlyph (g, *palette, panel::Glyph::highShelfCut,  glyphBox (trebleCutX),   tint (ink (panel::Ink::trebleCut)));

    panel::drawGlyph (g, *palette, panel::Glyph::width,
                      juce::Rectangle<float> (40.0f, 18.0f).withCentre ({ trebleQX, top + qY - 52.0f }),
                      tint (ink (panel::Ink::neutral)));

    // Group rules: short engraved lines between bass, mid and treble.
    for (auto x : { 0.5f * (lowCutX + midBoostX), 0.5f * (midCutX + trebleBoostX), 0.5f * (trebleCutX + toggleX[0]) })
    {
        g.setColour (palette->lip);
        g.fillRect (x + 0.8f, top + 40.0f, 1.0f, stripHeight - 70.0f);
        g.setColour (palette->engravingDim.withAlpha (0.55f));
        g.fillRect (x, top + 40.0f, 1.0f, stripHeight - 70.0f);
    }

    const std::array<panel::Glyph, 4> switchGlyphs { panel::Glyph::shift, panel::Glyph::buffer,
                                                     panel::Glyph::transformer, panel::Glyph::power };

    for (size_t i = 0; i < switchGlyphs.size(); ++i)
        panel::drawGlyph (g, *palette, switchGlyphs[i],
                          juce::Rectangle<float> (42.0f, 26.0f).withCentre ({ toggleX[i], top + toggleGlyphY }),
                          tint (i == 0 ? palette->ink (panel::Ink::bass) : palette->engraving));

    // Channel numeral.
    juce::GlyphArrangement numeral;
    numeral.addFittedText (juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 30.0f, juce::Font::bold)),
                           index == 0 ? "I" : "II", numeralX - 24.0f, top + 120.0f, 48.0f, 40.0f,
                           juce::Justification::centred, 1);
    g.setColour (palette->lip);
    numeral.draw (g, juce::AffineTransform::translation (0.0f, 1.0f));
    g.setColour (tint (palette->engraving));
    numeral.draw (g);
}

// ---------------------------------------------------------------------------

LacquerEditor::LacquerEditor (LacquerProcessor& p)
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
    startTimerHz (15);
}

LacquerEditor::~LacquerEditor()
{
    stopTimer();
}

void LacquerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void LacquerEditor::resized()
{
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / designWidth,
                                                     (float) getHeight() / designHeight));
}

void LacquerEditor::timerCallback()
{
    face.refresh();
}

} // namespace lacquer
