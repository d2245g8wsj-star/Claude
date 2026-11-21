#include "PluginEditor.h"

namespace lacquer
{

namespace ui = indiewave::ui;

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

    constexpr float modeKeyX[] = { 948.0f, 1004.0f, 1060.0f };
    constexpr float logoX = 560.0f;

    ui::Key::Style modeKey (juce::String text)
    {
        ui::Key::Style s;
        s.kind = ui::Key::Kind::white;
        s.lit = juce::Colour (0xfff2c46a);
        s.capSize = 20.0f;
        s.capWidth = 50.0f;
        s.capLabel = std::move (text);
        s.capLabelOn = juce::Colour (0xff2b261f);
        s.capLabelOff = juce::Colour (0xff2b261f);
        return s;
    }

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
      modeKeys { ui::Key (modeKey ("STEREO")), ui::Key (modeKey ("DUAL")), ui::Key (modeKey ("M/S")) },
      fold   (ui::FoldTab::Edge::bottom, juce::Colours::black, juce::Colours::grey),
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
            i == 0 ? addAndMakeVisible (child) : addChildComponent (child);
    }

    for (int i = 0; i < 3; ++i)
    {
        addAndMakeVisible (modeKeys[(size_t) i]);
        modeKeys[(size_t) i].onClick = [this, i] { setMode (i); };
    }

    addAndMakeVisible (fold);
    fold.onClick = [this]
    {
        // Folding links the channels; unfolding gives each its own controls.
        // Mid/side stays as it is: folded, one setting works on mid and side.
        auto* link = processor.apvts.getParameter (pid::link);
        link->beginChangeGesture();
        link->setValueNotifyingHost (open ? 1.0f : 0.0f);
        link->endChangeGesture();
    };

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

    for (int i = 0; i < 3; ++i)
        modeKeys[(size_t) i].setBounds (juce::Rectangle<float> (54.0f, 30.0f).withCentre ({ modeKeyX[i], middle }).toNearestInt());

    const auto height = (float) (open ? designHeightOpen : designHeightFolded);
    fold.setBounds (juce::Rectangle<float> (640.0f, height - 22.0f, 120.0f, 17.0f).toNearestInt());
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

    apply (&output);
    fold.setColours (palette->engraving, palette->plateBottom.contrasting (0.12f));
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

    const auto linked  = processor.apvts.getRawParameterValue (pid::link)   ->load() >= 0.5f;
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

    // Linked, channel 1 sets both sides and channel 2 folds away; its
    // settings stay where they were for when it comes back.
    for (auto* child : strips[1]->components())
        child->setVisible (open);

    fold.setCaption (midSide ? juce::String (juce::CharPointer_UTF8 ("SPLIT M · S")) : juce::String (juce::CharPointer_UTF8 ("DUAL L · R")), "FOLD");
    fold.setOpen (open);

    resized();
    repaint();

    if (changed && onFoldChanged)
        onFoldChanged();
}

void LacquerEditor::Face::setMode (int index)
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

    // The maker's mark and the name, cut into the bar.
    ui::drawLogo (g, { logoX, middle }, 0.62f, palette->engraving, palette->engravingDim, false);
    {
        juce::GlyphArrangement name;
        auto font = juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 32.0f,
                                                   juce::Font::bold | juce::Font::italic));
        name.addFittedText (font, "Lacquer", logoX + 26.0f, middle - 26.0f, 240.0f, 34.0f,
                            juce::Justification::centredLeft, 1);

        g.setColour (palette->lip);
        name.draw (g, juce::AffineTransform::translation (0.0f, 1.0f));
        g.setColour (palette->engraving);
        name.draw (g);
    }

    panel::drawTrackedLabel (g, *palette, "INDIEWAVE AUDIO", { logoX + 28.0f, middle + 10.0f, 240.0f, 10.0f }, 7.5f, 3.0f,
                             juce::Justification::centredLeft, palette->engravingDim);

    panel::drawTrackedLabel (g, *palette, "MODE", { modeKeyX[0] - 100.0f, middle - 5.0f, 66.0f, 10.0f }, 7.5f, 2.0f,
                             juce::Justification::centredRight, palette->engravingDim);

    panel::drawGlyph (g, *palette, panel::Glyph::power,  { 136.0f, middle - 9.0f, 18.0f, 18.0f }, palette->engraving);
    panel::drawGlyph (g, *palette, panel::Glyph::output, { 1180.0f, middle - 9.0f, 24.0f, 18.0f }, palette->engraving);

    paintStripMarkings (g, 0, rowTop[0]);

    if (open)
        paintStripMarkings (g, 1, rowTop[1]);
}

void LacquerEditor::Face::paintStripMarkings (juce::Graphics& g, int index, float top) const
{
    const auto tint = [] (juce::Colour c) { return c; };
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

    // Channel letter, when there are two channels to tell apart.
    if (! open)
        return;

    const auto midSide = shownMode == 2;
    juce::GlyphArrangement numeral;
    numeral.addFittedText (juce::Font (juce::FontOptions (juce::Font::getDefaultSerifFontName(), 30.0f, juce::Font::bold)),
                           index == 0 ? (midSide ? "M" : "L") : (midSide ? "S" : "R"), numeralX - 24.0f, top + 120.0f, 48.0f, 40.0f,
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
    face.onFoldChanged = [this] { applyFold(); };

    setResizable (true, true);
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;
    setResizeLimits (designWidth / 2, height / 2, designWidth * 2, height * 2);

    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) designWidth / height);

    setSize (designWidth * 4 / 5, height * 4 / 5);
    startTimerHz (15);
}

LacquerEditor::~LacquerEditor()
{
    stopTimer();
}

void LacquerEditor::applyFold()
{
    // Keep the scale the panel is drawn at; only the height changes.
    const auto scale  = (double) getWidth() / designWidth;
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;

    setResizeLimits (designWidth / 2, height / 2, designWidth * 2, height * 2);

    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) designWidth / height);

    setSize (getWidth(), juce::roundToInt (height * scale));
}

void LacquerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void LacquerEditor::resized()
{
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;
    face.setBounds (0, 0, designWidth, height);
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) designWidth,
                                                     (float) getHeight() / (float) height));
}

void LacquerEditor::timerCallback()
{
    face.refresh();
}

} // namespace lacquer
