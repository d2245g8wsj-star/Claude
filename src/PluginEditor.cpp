#include "PluginEditor.h"

namespace crest
{

namespace
{
    // The panel is divided into sections the way a front panel is: by milled
    // lines, not by whitespace.
    constexpr float headerBottom = 70.0f;
    constexpr float railTop      = 372.0f;

    void drawDivider (juce::Graphics& g, float x, float top, float bottom)
    {
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.drawVerticalLine (juce::roundToInt (x), top, bottom);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawVerticalLine (juce::roundToInt (x) + 1, top, bottom);
    }

    /** The mark: one cycle of a sine whose crest has been taken off. */
    void drawMark (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto plot = area.reduced (3.0f, 5.0f);

        juce::Path wave;

        constexpr int   steps = 52;
        constexpr float limit = 0.60f;

        for (int i = 0; i <= steps; ++i)
        {
            const auto phase = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
            const auto value = juce::jlimit (-limit, limit, std::sin (phase) * 1.4f);

            const juce::Point<float> point { juce::jmap ((float) i, 0.0f, (float) steps,
                                                          plot.getX(), plot.getRight()),
                                             plot.getCentreY() - value * plot.getHeight() * 0.5f };

            if (i == 0) wave.startNewSubPath (point);
            else        wave.lineTo (point);
        }

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded),
                      juce::AffineTransform::translation (0.0f, 1.0f));

        g.setColour (hw::lampAmber);
        g.strokePath (wave, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
}

// ---------------------------------------------------------------------------

CrestEditor::Panel::Panel (CrestProcessor& p)
    : processor (p),
      inputMeter  (p.meters, false, "IN"),
      outputMeter (p.meters, true,  "OUT"),
      scope       (p.apvts, p.meters, p.displayKnee),
      clipMeter   (p.apvts, p.meters, p.sidechainRunning),
      inputKnob     (p.apvts, pid::input,     "Input"),
      ceilingKnob   (p.apvts, pid::ceiling,   "Ceiling"),
      outputKnob    (p.apvts, pid::output,    "Output"),
      kneeKnob      (p.apvts, pid::knee,      "Knee"),
      adaptKnob     (p.apvts, pid::adapt,     "Adapt"),
      biasKnob      (p.apvts, pid::bias,      "Bias", true),
      colourKnob    (p.apvts, pid::colour,    "Colour"),
      harmonicsKnob (p.apvts, pid::harmonics, "Harmonics"),
      seriesKnob    (p.apvts, pid::series,    "Series", true),
      sidechainKnob (p.apvts, pid::sidechain, "SC LPF"),
      mixKnob       (p.apvts, pid::mix,       "Mix"),
      characterSwitch (p.apvts, pid::mode, "CHARACTER"),
      osBank     (p.apvts, pid::os,     "OVERSAMPLING"),
      wordBank   (p.apvts, pid::word,   "WORD"),
      ditherBank (p.apvts, pid::dither, "DITHER"),
      autoLamp   (p.apvts, pid::autoGain, "AUTO"),
      tpSafeLamp (p.apvts, pid::tpSafe,   "TP SAFE"),
      deltaLamp  (p.apvts, pid::delta,    "DELTA"),
      linkLamp   (p.apvts, pid::link,     "LINK"),
      bypassLamp (p.apvts, pid::bypass,   "BYPASS", hw::lampRed),
      matchLamp  (p.apvts, pid::match,    "MATCH",  hw::lampGreen),
      blindLamp  (p.apvts, pid::blind,    "BLIND",  hw::lampGreen),
      revealButton ("REVEAL", [this] { if (onReveal != nullptr) onReveal(); })
{
    matchLamp.setCompact (true);
    blindLamp.setCompact (true);

    inputKnob  .setDiameter (100.0f);
    ceilingKnob.setDiameter (100.0f);

    for (auto* knob : { &kneeKnob, &adaptKnob, &biasKnob, &colourKnob, &harmonicsKnob,
                        &seriesKnob, &sidechainKnob, &mixKnob, &outputKnob })
        knob->setDiameter (62.0f);

    for (auto* child : std::initializer_list<juce::Component*> {
             &inputMeter, &outputMeter, &scope, &clipMeter,
             &inputKnob, &ceilingKnob, &outputKnob, &kneeKnob, &adaptKnob, &biasKnob,
             &colourKnob, &harmonicsKnob, &seriesKnob, &sidechainKnob, &mixKnob,
             &characterSwitch,
             &osBank, &wordBank, &ditherBank,
             &autoLamp, &tpSafeLamp, &deltaLamp, &linkLamp, &bypassLamp,
             &matchLamp, &blindLamp, &revealButton })
    {
        addAndMakeVisible (child);
    }
}

void CrestEditor::Panel::setStatusText (const juce::String& text)
{
    if (statusText == text)
        return;

    statusText = text;
    repaint (getLocalBounds().removeFromBottom (30));
}

void CrestEditor::Panel::setBlindText (const juce::String& text)
{
    if (blindText == text)
        return;

    blindText = text;
    repaint (686, 316, 190, 18);
}

void CrestEditor::Panel::resized()
{
    // The brushed textures are generated once per size rather than per frame:
    // a grain pattern that moves reads as noise, not as metal.
    faceplate   = hw::renderBrushedPanel (getWidth(), getHeight(), hw::panelDark, 0x51ce);
    headerPlate = hw::renderBrushedPanel (getWidth(), (int) headerBottom, hw::panelLight, 0x27a1);
    railPlate   = hw::renderBrushedPanel (getWidth(), getHeight() - (int) railTop,
                                          hw::panelDeep, 0x9f30);

    inputMeter .setBounds (  20,  88,  72, 268);
    outputMeter.setBounds (1552,  88,  72, 268);

    inputKnob      .setBounds ( 100, 104, 132, 178);
    characterSwitch.setBounds ( 240,  88, 172, 208);
    kneeKnob       .setBounds ( 418, 124,  88, 138);
    adaptKnob      .setBounds ( 506, 124,  88, 138);
    biasKnob       .setBounds ( 594, 124,  88, 138);

    scope    .setBounds (690,  88, 178, 178);
    clipMeter.setBounds (690, 274, 178,  42);

    matchLamp   .setBounds (690, 334, 57, 24);
    blindLamp   .setBounds (749, 334, 57, 24);
    revealButton.setBounds (808, 334, 60, 24);

    colourKnob   .setBounds ( 876, 124,  88, 138);
    harmonicsKnob.setBounds ( 964, 124,  88, 138);
    seriesKnob   .setBounds (1052, 124,  88, 138);
    sidechainKnob.setBounds (1140, 124,  88, 138);

    mixKnob    .setBounds (1236, 124,  88, 138);
    ceilingKnob.setBounds (1324, 104, 132, 178);
    outputKnob .setBounds (1456, 124,  88, 138);

    osBank    .setBounds ( 20, 382, 236, 42);
    wordBank  .setBounds (272, 382, 188, 42);
    ditherBank.setBounds (476, 382, 160, 42);

    {
        constexpr int y = 396, width = 150, gap = 16;

        hw::LampButton* lamps[] { &autoLamp, &tpSafeLamp, &deltaLamp, &linkLamp, &bypassLamp };

        for (int i = 0; i < 5; ++i)
            lamps[i]->setBounds (780 + i * (width + gap), y, width, 28);
    }
}

void CrestEditor::Panel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (faceplate.isValid())
        g.drawImageAt (faceplate, 0, 0);

    // ------------------------------------------------------------- header
    if (headerPlate.isValid())
        g.drawImageAt (headerPlate, 0, 0);

    auto header = bounds.withHeight (headerBottom);

    drawMark (g, juce::Rectangle<float> (46.0f, 22.0f, 30.0f, 26.0f));

    hw::drawEngraved (g, "INDIEWAVE AUDIO",
                      juce::Rectangle<float> (88.0f, 22.0f, 260.0f, 26.0f),
                      juce::Justification::left, 11.0f, 3.0f);

    hw::drawEngraved (g, "CREST", header.withTrimmedBottom (4.0f),
                      juce::Justification::horizontallyCentred, 27.0f, 11.0f,
                      hw::engraved.brighter (0.25f));

    hw::drawEngraved (g, "CONVERTER . CLIPPER",
                      juce::Rectangle<float> (bounds.getRight() - 300.0f, 22.0f, 268.0f, 26.0f),
                      juce::Justification::right, 10.0f, 2.6f, hw::engraved.withAlpha (0.85f));

    // Milled seam under the header
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawHorizontalLine ((int) headerBottom, 0.0f, bounds.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawHorizontalLine ((int) headerBottom + 1, 0.0f, bounds.getRight());

    // --------------------------------------------------------------- rail
    if (railPlate.isValid())
        g.drawImageAt (railPlate, 0, (int) railTop);

    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawHorizontalLine ((int) railTop, 0.0f, bounds.getRight());
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawHorizontalLine ((int) railTop + 1, 0.0f, bounds.getRight());

    // ----------------------------------------------------------- sections
    for (auto x : { 96.0f, 236.0f, 686.0f, 872.0f, 1232.0f, 1548.0f })
        drawDivider (g, x, headerBottom + 12.0f, railTop - 12.0f);

    struct Caption { const char* text; float from; float to; };

    for (auto caption : { Caption { "INPUT",    96.0f,  236.0f },
                          Caption { "CLIP",     236.0f,  686.0f },
                          Caption { "MONITOR",  686.0f,  872.0f },
                          Caption { "COLOUR",   872.0f, 1232.0f },
                          Caption { "OUTPUT",  1232.0f, 1548.0f } })
    {
        const auto y = railTop - 30.0f;
        const auto area = juce::Rectangle<float> (caption.from, y, caption.to - caption.from, 14.0f);

        // Silkscreen rule with the section name set into it, the way a legend
        // is printed on a real panel.
        const auto textWidth = 16.0f + 9.0f * (float) juce::String (caption.text).length();
        const auto gap = juce::Rectangle<float> (textWidth, 14.0f).withCentre (area.getCentre());

        for (auto rule : { juce::Rectangle<float> (area.getX() + 26.0f, area.getCentreY(),
                                                   gap.getX() - area.getX() - 26.0f, 1.0f),
                           juce::Rectangle<float> (gap.getRight(), area.getCentreY(),
                                                   area.getRight() - gap.getRight() - 26.0f, 1.0f) })
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRect (rule);
            g.setColour (hw::engraved.withAlpha (0.18f));
            g.fillRect (rule.translated (0.0f, 1.0f));
        }

        hw::drawEngraved (g, caption.text, area, juce::Justification::horizontallyCentred,
                          9.0f, 3.2f, hw::engraved.withAlpha (0.62f));
    }

    // -------------------------------------------------------------- screws
    for (auto corner : { juce::Point<float> {   16.0f,  35.0f },
                         juce::Point<float> { 1628.0f,  35.0f },
                         juce::Point<float> {   16.0f, 424.0f },
                         juce::Point<float> { 1628.0f, 424.0f } })
    {
        hw::drawScrew (g, corner, 7.5f, corner.x < 100.0f ? 0.6f : -0.4f);
    }

    // ------------------------------------------------------- blind readout
    hw::drawEngraved (g, blindText, juce::Rectangle<float> (690.0f, 318.0f, 178.0f, 14.0f),
                      juce::Justification::horizontallyCentred, 9.0f, 1.6f,
                      blindText.contains ("PROCESSED") ? hw::lampAmber : hw::lampGreen);

    // -------------------------------------------------------------- status
    hw::drawEngraved (g, statusText,
                      juce::Rectangle<float> (0.0f, 432.0f, bounds.getWidth(), 16.0f),
                      juce::Justification::horizontallyCentred, 9.0f, 1.2f,
                      hw::engravedDim.withAlpha (0.6f));
}

// ---------------------------------------------------------------------------

CrestEditor::Tooltips::Tooltips()
{
    setColour (juce::TooltipWindow::backgroundColourId, hw::panelLight);
    setColour (juce::TooltipWindow::textColourId,       hw::engraved);
    setColour (juce::TooltipWindow::outlineColourId,    juce::Colours::black);
}

void CrestEditor::Tooltips::drawTooltip (juce::Graphics& g, const juce::String& text,
                                         int width, int height)
{
    const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (hw::panelLight);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (juce::Colours::black.withAlpha (0.8f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    g.setColour (hw::engraved);
    g.setFont (hw::panelFont (12.0f, false));
    g.drawFittedText (text, bounds.reduced (9.0f, 6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 3);
}

// ---------------------------------------------------------------------------

CrestEditor::CrestEditor (CrestProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), panel (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (panel);

    panel.onReveal = [this] { revealSecondsLeft = 12; };   // three seconds at four hertz

    setResizable (true, true);

    if (auto* sizeConstrainer = getConstrainer())
    {
        sizeConstrainer->setFixedAspectRatio ((double) designWidth / (double) designHeight);
        sizeConstrainer->setSizeLimits (designWidth * 55 / 100, designHeight * 55 / 100,
                                        designWidth * 130 / 100, designHeight * 130 / 100);
    }

    const auto saved = (int) processor.apvts.state.getProperty ("editorWidth", designWidth * 8 / 10);
    const auto width = juce::jlimit (designWidth * 55 / 100, designWidth * 130 / 100, saved);

    setSize (width, juce::roundToInt ((double) width * designHeight / designWidth));

    startTimerHz (4);
}

CrestEditor::~CrestEditor()
{
    setLookAndFeel (nullptr);
}

void CrestEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void CrestEditor::resized()
{
    const auto scale = (float) getWidth() / (float) designWidth;

    panel.setBounds (0, 0, designWidth, designHeight);
    panel.setTransform (juce::AffineTransform::scale (scale));

    processor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}

void CrestEditor::timerCallback()
{
    const auto rate    = processor.getSampleRate();
    const auto latency = processor.getLatencySamples();

    auto lufs = [] (float value)
    {
        return value <= -99.0f ? juce::String ("--.-") : juce::String (value, 1);
    };

    juce::String status;
    status << "M " << lufs (processor.displayMomentary.load())
           << "   S " << lufs (processor.displayShortTerm.load())
           << "   I " << lufs (processor.displayIntegrated.load()) << " LUFS";

    const auto match = processor.displayMatchDb.load();

    if (std::abs (match) > 0.05f)
        status << "   .   MATCH " << juce::String (match, 1) << " DB";

    status << "   .   " << juce::String (rate / 1000.0, 1) << " kHz   .   LATENCY " << latency << " SMP";

    if (rate > 0.0)
        status << " / " << juce::String (1000.0 * latency / rate, 2) << " MS";

    panel.setStatusText (status);

    // ---- blind comparison state
    const auto blind = processor.apvts.getRawParameterValue (pid::blind)->load() > 0.5f;

    if (! blind)
    {
        revealSecondsLeft = 0;
        panel.revealButton.setLit (false);
        panel.revealButton.setEnabledLook (false);
        panel.setBlindText ({});
        return;
    }

    panel.revealButton.setEnabledLook (true);

    const auto onB = processor.apvts.getRawParameterValue (pid::bypass)->load() > 0.5f;

    if (revealSecondsLeft > 0)
    {
        panel.revealButton.setLit (true);
        panel.setBlindText (processor.blindInverted.load() ? "B IS PROCESSED" : "A IS PROCESSED");

        if (--revealSecondsLeft == 0)
        {
            // A new draw, so the next comparison starts from nothing known.
            processor.reshuffleBlind();
            panel.revealButton.setLit (false);
        }
    }
    else
    {
        panel.setBlindText (juce::String ("LISTENING TO ") + (onB ? "B" : "A"));
    }
}

} // namespace crest
