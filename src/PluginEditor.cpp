#include "PluginEditor.h"
#include "BinaryData.h"

namespace crest
{

namespace
{
    /** Centres a square control on a point from the art, drawn larger than the
        control's nominal diameter because each filmstrip frame carries its own
        shadow and highlight outside the knob body. */
    juce::Rectangle<int> place (float centreX, float centreY, float diameter, float scale = 1.35f)
    {
        const auto side = diameter * scale;

        return juce::Rectangle<float> (side, side).withCentre ({ centreX, centreY }).toNearestInt();
    }
}

// ---------------------------------------------------------------------------

CrestEditor::Face::Face (CrestProcessor& p)
    : processor (p),
      knobStrip      (BinaryData::knob_large_png,           BinaryData::knob_large_pngSize),
      characterStrip (BinaryData::character_selector_5_png, BinaryData::character_selector_5_pngSize),
      inputKnob     (p.apvts, pid::input,     knobStrip),
      kneeKnob      (p.apvts, pid::knee,      knobStrip),
      adaptKnob     (p.apvts, pid::adapt,     knobStrip),
      biasKnob      (p.apvts, pid::bias,      knobStrip),
      colourKnob    (p.apvts, pid::colour,    knobStrip),
      harmonicsKnob (p.apvts, pid::harmonics, knobStrip),
      seriesKnob    (p.apvts, pid::series,    knobStrip),
      sidechainKnob (p.apvts, pid::sidechain, knobStrip),
      mixKnob       (p.apvts, pid::mix,       knobStrip),
      ceilingKnob   (p.apvts, pid::ceiling,   knobStrip),
      outputKnob    (p.apvts, pid::output,    knobStrip),
      characterSwitch (p.apvts, pid::mode,    characterStrip),
      inputMeter  (p.meters, false, ledOff, ledGreen, ledAmber, ledRed),
      outputMeter (p.meters, true,  ledOff, ledGreen, ledAmber, ledRed),
      scope       (p.apvts, p.meters, p.displayKnee),
      osBank      (p.apvts, pid::os,       "OVERSAMPLING"),
      wordBank    (p.apvts, pid::word,     "WORD"),
      ditherBank  (p.apvts, pid::dither,   "DITHER"),
      autoSwitch  (p.apvts, pid::autoGain, "AUTO"),
      tpSafeSwitch(p.apvts, pid::tpSafe,   "TP SAFE"),
      deltaSwitch (p.apvts, pid::delta,    "DELTA"),
      linkSwitch  (p.apvts, pid::link,     "LINK"),
      bypassSwitch(p.apvts, pid::bypass,   "BYPASS"),
      matchSwitch (p.apvts, pid::match,    "MATCH"),
      blindSwitch (p.apvts, pid::blind,    "BLIND"),
      revealButton ("REVEAL", [this] { if (onReveal != nullptr) onReveal(); })
{
    plate    = juce::ImageCache::getFromMemory (BinaryData::plate_png,     BinaryData::plate_pngSize);
    ledOff   = juce::ImageCache::getFromMemory (BinaryData::led_off_png,   BinaryData::led_off_pngSize);
    ledGreen = juce::ImageCache::getFromMemory (BinaryData::led_green_png, BinaryData::led_green_pngSize);
    ledAmber = juce::ImageCache::getFromMemory (BinaryData::led_amber_png, BinaryData::led_amber_pngSize);
    ledRed   = juce::ImageCache::getFromMemory (BinaryData::led_red_png,   BinaryData::led_red_pngSize);

    // The plate prints five detents but the plugin has four characters: at a
    // knee of zero every character already is a square corner, so a separate
    // SLICE position would be a fifth name for a setting the knee reaches. The
    // four are mapped onto the four upper detents and the SLICE detent at half
    // past seven is simply never selected.
    characterSwitch.setFrameMap ({ 1, 2, 3, 4 });

    // The faces of these three are printed on the plate.
    matchSwitch .setDrawFace (false);
    blindSwitch .setDrawFace (false);
    revealButton.setDrawFace (false);

    for (auto* child : std::initializer_list<juce::Component*> {
             &inputMeter, &outputMeter, &scope,
             &inputKnob, &kneeKnob, &adaptKnob, &biasKnob, &colourKnob, &harmonicsKnob,
             &seriesKnob, &sidechainKnob, &mixKnob, &ceilingKnob, &outputKnob,
             &characterSwitch,
             &osBank, &wordBank, &ditherBank,
             &autoSwitch, &tpSafeSwitch, &deltaSwitch, &linkSwitch, &bypassSwitch,
             &matchSwitch, &blindSwitch, &revealButton })
    {
        addAndMakeVisible (child);
    }
}

void CrestEditor::Face::setStatusText (const juce::String& text)
{
    if (statusText == text)
        return;

    statusText = text;
    repaint (0, 316, getWidth(), 20);
}

void CrestEditor::Face::setBlindText (const juce::String& text)
{
    if (blindText == text)
        return;

    blindText = text;
    repaint (560, 222, 180, 18);
}

void CrestEditor::Face::resized()
{
    // Centres and diameters come straight from the art's controls.json.
    inputKnob      .setBounds (place ( 130.0f, 150.0f, 75.0f));
    characterSwitch.setBounds (place ( 257.0f, 152.5f, 60.0f));
    kneeKnob       .setBounds (place ( 363.0f, 150.0f, 52.0f));
    adaptKnob      .setBounds (place ( 433.0f, 150.0f, 52.0f));
    biasKnob       .setBounds (place ( 503.5f, 150.0f, 52.0f));
    colourKnob     .setBounds (place ( 726.5f, 150.0f, 52.0f));
    harmonicsKnob  .setBounds (place ( 796.0f, 150.0f, 52.0f));
    seriesKnob     .setBounds (place ( 866.5f, 150.0f, 52.0f));
    sidechainKnob  .setBounds (place ( 935.0f, 150.0f, 52.0f));
    mixKnob        .setBounds (place (1010.0f, 150.0f, 52.0f));
    ceilingKnob    .setBounds (place (1098.5f, 150.0f, 75.0f));
    outputKnob     .setBounds (place (1185.5f, 150.0f, 75.0f));

    scope.setBounds (570, 100, 110, 115);

    inputMeter .setBounds (  24, 75, 28, 210);
    outputMeter.setBounds (1248, 75, 28, 210);

    // Sitting exactly on the shapes printed underneath them.
    matchSwitch .setBounds (570, 238, 34, 16);
    blindSwitch .setBounds (606, 238, 34, 16);
    revealButton.setBounds (643, 238, 34, 16);

    // Nothing on the plate covers the switched parameters, so they get the
    // empty band below the knobs, drawn in the plate's own colours.
    osBank    .setBounds ( 96, 272, 200, 32);
    wordBank  .setBounds (312, 272, 160, 32);
    ditherBank.setBounds (488, 272, 128, 32);

    {
        constexpr int y = 287, width = 92, gap = 8;

        panel::Switch* switches[] { &autoSwitch, &tpSafeSwitch, &deltaSwitch,
                                    &linkSwitch, &bypassSwitch };

        for (int i = 0; i < 5; ++i)
            switches[i]->setBounds (700 + i * (width + gap), y, width, 17);
    }
}

void CrestEditor::Face::paint (juce::Graphics& g)
{
    if (plate.isValid())
        g.drawImage (plate, getLocalBounds().toFloat(),
                     juce::RectanglePlacement::stretchToFit, false);
    else
        g.fillAll (panel::cream);

    // "LISTENING TO A" is printed on the plate, and it is only true while a
    // blind comparison is running. Patch over it with a clean piece of the same
    // brushed surface from lower down, then write whatever is actually the case.
    if (plate.isValid())
    {
        const auto scaleX = (float) plate.getWidth()  / (float) getWidth();
        const auto scaleY = (float) plate.getHeight() / (float) getHeight();

        const juce::Rectangle<int> target { 556, 222, 188, 17 };

        g.drawImage (plate,
                     target.getX(), target.getY(), target.getWidth(), target.getHeight(),
                     juce::roundToInt ((float) target.getX() * scaleX),
                     juce::roundToInt (268.0f * scaleY),
                     juce::roundToInt ((float) target.getWidth()  * scaleX),
                     juce::roundToInt ((float) target.getHeight() * scaleY),
                     false);
    }

    if (blindText.isNotEmpty())
    {
        g.setColour (panel::amber);
        g.setFont (panel::plateFont (9.0f));
        g.drawText (blindText, juce::Rectangle<int> (556, 222, 188, 17),
                    juce::Justification::centred, false);
    }

    g.setColour (panel::ink.withAlpha (0.68f));
    g.setFont (panel::plateFont (9.0f, false));
    g.drawText (statusText, juce::Rectangle<int> (0, 318, getWidth(), 16),
                juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------

CrestEditor::Tooltips::Tooltips()
{
    setColour (juce::TooltipWindow::backgroundColourId, panel::cream);
    setColour (juce::TooltipWindow::textColourId,       panel::ink);
    setColour (juce::TooltipWindow::outlineColourId,    panel::ink);
}

void CrestEditor::Tooltips::drawTooltip (juce::Graphics& g, const juce::String& text,
                                         int width, int height)
{
    const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (panel::cream);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (panel::ink.withAlpha (0.5f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 3.0f, 1.0f);

    g.setColour (panel::ink);
    g.setFont (panel::plateFont (12.0f, false));
    g.drawFittedText (text, bounds.reduced (9.0f, 6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 3);
}

// ---------------------------------------------------------------------------

CrestEditor::CrestEditor (CrestProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), face (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (face);

    face.onReveal = [this] { revealSecondsLeft = 12; };   // three seconds at four hertz

    setResizable (true, true);

    if (auto* sizeConstrainer = getConstrainer())
    {
        sizeConstrainer->setFixedAspectRatio ((double) designWidth / (double) designHeight);
        sizeConstrainer->setSizeLimits (designWidth * 60 / 100, designHeight * 60 / 100,
                                        designWidth * 150 / 100, designHeight * 150 / 100);
    }

    const auto saved = (int) processor.apvts.state.getProperty ("editorWidth", designWidth);
    const auto width = juce::jlimit (designWidth * 60 / 100, designWidth * 150 / 100, saved);

    setSize (width, juce::roundToInt ((double) width * designHeight / designWidth));

    startTimerHz (4);
}

CrestEditor::~CrestEditor()
{
    setLookAndFeel (nullptr);
}

void CrestEditor::paint (juce::Graphics& g)
{
    g.fillAll (panel::ink);
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
    auto lufs = [] (float value)
    {
        return value <= -99.0f ? juce::String ("--.-") : juce::String (value, 1);
    };

    juce::String status;
    status << "M " << lufs (processor.displayMomentary.load())
           << "    S " << lufs (processor.displayShortTerm.load())
           << "    I " << lufs (processor.displayIntegrated.load()) << " LUFS";

    const auto match = processor.displayMatchDb.load();

    if (std::abs (match) > 0.05f)
        status << "    MATCH " << juce::String (match, 1) << " dB";

    const auto rate    = processor.getSampleRate();
    const auto latency = processor.getLatencySamples();

    status << "    " << juce::String (rate / 1000.0, 1) << " kHz"
           << "    LATENCY " << latency << " smp";

    face.setStatusText (status);

    const auto blind = processor.apvts.getRawParameterValue (pid::blind)->load() > 0.5f;

    if (! blind)
    {
        revealSecondsLeft = 0;
        face.revealButton.setLit (false);
        face.setBlindText ({});
        return;
    }

    const auto onB = processor.apvts.getRawParameterValue (pid::bypass)->load() > 0.5f;

    if (revealSecondsLeft > 0)
    {
        face.revealButton.setLit (true);
        face.setBlindText (processor.blindInverted.load() ? "B IS PROCESSED" : "A IS PROCESSED");

        if (--revealSecondsLeft == 0)
        {
            processor.reshuffleBlind();
            face.revealButton.setLit (false);
        }
    }
    else
    {
        face.setBlindText (juce::String ("LISTENING TO ") + (onB ? "B" : "A"));
    }
}

} // namespace crest
