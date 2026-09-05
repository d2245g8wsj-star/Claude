#include "PluginEditor.h"
#include "BinaryData.h"

namespace crest
{

namespace
{
    /** Centres a square control on a point from the art, drawn larger than the
        control's nominal diameter because each filmstrip frame carries its own
        shadow and highlight outside the knob body. */
    /** The art places its labels assuming the filmstrip frame is drawn at exactly
        the diameter in controls.json, so that is what happens here. Anything
        larger runs the frame into the label above it. */
    juce::Rectangle<int> place (float centreX, float centreY, float diameter, float scale = 1.0f)
    {
        const auto side = diameter * scale;

        return juce::Rectangle<float> (side, side).withCentre ({ centreX, centreY }).toNearestInt();
    }
}

// ---------------------------------------------------------------------------

CrestEditor::Face::Face (CrestProcessor& p)
    : processor (p),
      largeStrip     (BinaryData::knob_large_png,           BinaryData::knob_large_pngSize),
      smallStrip     (BinaryData::knob_small_png,           BinaryData::knob_small_pngSize),
      characterStrip (BinaryData::character_selector_4_png, BinaryData::character_selector_4_pngSize),
      inputKnob     (p.apvts, pid::input,     largeStrip),
      kneeKnob      (p.apvts, pid::knee,      smallStrip),
      adaptKnob     (p.apvts, pid::adapt,     smallStrip),
      biasKnob      (p.apvts, pid::bias,      smallStrip),
      colourKnob    (p.apvts, pid::colour,    smallStrip),
      harmonicsKnob (p.apvts, pid::harmonics, smallStrip),
      seriesKnob    (p.apvts, pid::series,    smallStrip),
      sidechainKnob (p.apvts, pid::sidechain, smallStrip),
      mixKnob       (p.apvts, pid::mix,       smallStrip),
      ceilingKnob   (p.apvts, pid::ceiling,   largeStrip),
      outputKnob    (p.apvts, pid::output,    largeStrip),
      characterSwitch (p.apvts, pid::mode,    characterStrip),
      inputMeter  (p.meters, false, ledOff, ledGreen, ledAmber, ledRed),
      outputMeter (p.meters, true,  ledOff, ledGreen, ledAmber, ledRed),
      waveform    (p.waveform),
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

    // Four frames, four characters, in the same order the plate prints them:
    // GLASS at -135, EDGE at -45, SILK at +45, TUBE at +135.
    characterSwitch.setFrameMap ({ 0, 1, 2, 3 });

    // The wells and the headings of these are printed on the plate.
    osBank    .setWellsAreBaked (true);
    wordBank  .setWellsAreBaked (true);
    ditherBank.setWellsAreBaked (true);

    // As are the shapes and the resting legends of the five toggles. The legend
    // is still drawn when one is engaged, in cream over the amber, because the
    // printed one underneath is dark ink and would disappear.
    for (auto* toggle : { &autoSwitch, &tpSafeSwitch, &deltaSwitch, &linkSwitch, &bypassSwitch })
        toggle->setDrawFace (false);

    for (auto* child : std::initializer_list<juce::Component*> {
             &inputMeter, &outputMeter, &waveform, &scope,
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
    repaint (0, 436, getWidth(), 22);
}

void CrestEditor::Face::setBlindText (const juce::String& text)
{
    if (blindText == text)
        return;

    blindText = text;
    repaint (330, 330, 360, 24);
}

void CrestEditor::Face::resized()
{
    // Straight out of the art's controls.json, grouped by signal flow.
    inputKnob      .setBounds (place ( 120.0f, 150.0f, 78.0f));

    characterSwitch.setBounds (place ( 232.0f, 130.0f, 66.0f));
    kneeKnob       .setBounds (place ( 200.0f, 235.0f, 50.0f));
    adaptKnob      .setBounds (place ( 264.0f, 235.0f, 50.0f));
    biasKnob       .setBounds (place ( 232.0f, 320.0f, 50.0f));

    colourKnob     .setBounds (place (1035.0f, 130.0f, 50.0f));
    harmonicsKnob  .setBounds (place (1099.0f, 130.0f, 50.0f));
    seriesKnob     .setBounds (place (1067.0f, 235.0f, 66.0f));
    sidechainKnob  .setBounds (place (1035.0f, 320.0f, 50.0f));
    mixKnob        .setBounds (place (1099.0f, 320.0f, 50.0f));

    ceilingKnob    .setBounds (place (1240.0f, 120.0f, 78.0f));
    outputKnob     .setBounds (place (1240.0f, 250.0f, 78.0f));

    waveform.setBounds (330,  90, 650, 210);
    scope   .setBounds (850, 170, 120, 120);

    // The transfer inset is a window of its own inside the same recess, so the
    // scrolling trace stops at its edge rather than running behind it.
    waveform.setExcluded (scope.getBounds().translated (-330, -90).expanded (2));

    inputMeter .setBounds (  26, 90, 32, 270);
    outputMeter.setBounds (1342, 90, 32, 270);

    // The comparison controls have no place in the art yet, so they take the
    // clear band between the scope and the switch row.
    matchSwitch .setBounds (700, 330, 80, 24);
    blindSwitch .setBounds (786, 330, 80, 24);
    revealButton.setBounds (872, 330, 88, 24);

    osBank    .setBounds (110, 405, 220, 24);
    wordBank  .setBounds (360, 405, 160, 24);
    ditherBank.setBounds (548, 405, 130, 24);

    autoSwitch  .setBounds ( 760, 405,  92, 24);
    tpSafeSwitch.setBounds ( 868, 405, 104, 24);
    deltaSwitch .setBounds ( 988, 405,  92, 24);
    linkSwitch  .setBounds (1096, 405,  84, 24);
    bypassSwitch.setBounds (1196, 405, 104, 24);
}

void CrestEditor::Face::paint (juce::Graphics& g)
{
    if (plate.isValid())
        g.drawImage (plate, getLocalBounds().toFloat(),
                     juce::RectanglePlacement::stretchToFit, false);
    else
        g.fillAll (panel::cream);

    if (blindText.isNotEmpty())
    {
        g.setColour (panel::amber);
        g.setFont (panel::plateFont (9.5f));
        g.drawText (blindText, juce::Rectangle<int> (330, 330, 360, 24),
                    juce::Justification::centredRight, false);
    }

    // The plate prints a sample rate and a latency of its own; cover it with a
    // clean piece of the same surface and write the live figures instead.
    if (plate.isValid())
    {
        const auto scaleX = (float) plate.getWidth()  / (float) getWidth();
        const auto scaleY = (float) plate.getHeight() / (float) getHeight();

        g.drawImage (plate,
                     300, 436, 800, 22,
                     juce::roundToInt (300.0f * scaleX), juce::roundToInt (368.0f * scaleY),
                     juce::roundToInt (800.0f * scaleX), juce::roundToInt ( 22.0f * scaleY),
                     false);
    }

    g.setColour (panel::ink.withAlpha (0.68f));
    g.setFont (panel::plateFont (9.0f, false));
    g.drawText (statusText, juce::Rectangle<int> (0, 438, getWidth(), 18),
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
