#include "PluginEditor.h"

namespace crest
{

namespace
{
    /** The mark: one cycle of a sine whose crest has been flattened off.
        It is the product description, drawn. */
    void drawMark (juce::Graphics& g, juce::Rectangle<float> area)
    {
        g.setColour (theme::outline);
        g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.0f);

        auto plot = area.reduced (4.0f, 5.0f);

        juce::Path wave;

        constexpr int steps   = 48;
        constexpr float limit = 0.62f;   // where the crest gets taken off

        for (int i = 0; i <= steps; ++i)
        {
            const auto phase = juce::MathConstants<float>::twoPi * (float) i / (float) steps;
            const auto value = juce::jlimit (-limit, limit, std::sin (phase) * 1.35f);

            const juce::Point<float> point { juce::jmap ((float) i, 0.0f, (float) steps,
                                                         plot.getX(), plot.getRight()),
                                             plot.getCentreY() - value * plot.getHeight() * 0.5f };

            if (i == 0) wave.startNewSubPath (point);
            else        wave.lineTo (point);
        }

        g.setColour (theme::amber);
        g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
}

// ---------------------------------------------------------------------------

CrestEditor::Content::Content (CrestProcessor& p)
    : processor (p),
      inputMeter     (p.meters, false, "IN"),
      outputMeter    (p.meters, true,  "OUT"),
      reductionMeter (p.meters),
      curve          (p.apvts, p.meters),
      inputKnob   (p.apvts, pid::input,   "Input",   theme::amber),
      ceilingKnob (p.apvts, pid::ceiling, "Ceiling", theme::teal),
      kneeKnob    (p.apvts, pid::knee,    "Knee",    theme::amber),
      biasKnob    (p.apvts, pid::bias,    "Bias",    theme::amber, true),
      colourKnob  (p.apvts, pid::colour,  "Colour",  theme::amber),
      mixKnob     (p.apvts, pid::mix,     "Mix",     theme::teal),
      outputKnob  (p.apvts, pid::output,  "Output",  theme::teal),
      modeStrip   (p.apvts, pid::mode,   "",       theme::amber),
      osStrip     (p.apvts, pid::os,     "OS",     theme::teal),
      wordStrip   (p.apvts, pid::word,   "WORD",   theme::teal),
      ditherStrip (p.apvts, pid::dither, "DITHER", theme::teal),
      autoChip    (p.apvts, pid::autoGain, "AUTO",    theme::teal),
      tpSafeChip  (p.apvts, pid::tpSafe,   "TP SAFE", theme::teal),
      deltaChip   (p.apvts, pid::delta,    "DELTA",   theme::amber),
      linkChip    (p.apvts, pid::link,     "LINK",    theme::teal),
      bypassChip  (p.apvts, pid::bypass,   "BYPASS",  theme::red)
{
    inputKnob.setLarge (true);
    ceilingKnob.setLarge (true);

    for (auto* child : std::initializer_list<juce::Component*> {
             &inputMeter, &outputMeter, &reductionMeter, &curve,
             &inputKnob, &ceilingKnob, &kneeKnob, &biasKnob, &colourKnob, &mixKnob, &outputKnob,
             &modeStrip, &osStrip, &wordStrip, &ditherStrip,
             &autoChip, &tpSafeChip, &deltaChip, &linkChip, &bypassChip })
    {
        addAndMakeVisible (child);
    }
}

void CrestEditor::Content::setStatusText (const juce::String& text)
{
    if (statusText == text)
        return;

    statusText = text;
    repaint (getLocalBounds().removeFromBottom (28));
}

void CrestEditor::Content::resized()
{
    inputMeter    .setBounds ( 16,  64,  74, 350);
    outputMeter   .setBounds (790,  64,  74, 350);

    modeStrip     .setBounds (280,  64, 320,  24);
    curve         .setBounds (280,  96, 320, 280);
    reductionMeter.setBounds (280, 386, 320,  30);

    inputKnob     .setBounds (104, 150, 120, 140);
    ceilingKnob   .setBounds (656, 150, 120, 140);

    // Five secondary controls spread across the same span as the meters.
    {
        constexpr int y = 430, height = 96, width = 84;
        constexpr int first = 104, last = 776 - width;
        constexpr int step = (last - first) / 4;

        gui::Knob* row[] { &kneeKnob, &biasKnob, &colourKnob, &mixKnob, &outputKnob };

        for (int i = 0; i < 5; ++i)
            row[i]->setBounds (first + step * i, y, width, height);
    }

    osStrip    .setBounds ( 96, 548, 196, 24);
    wordStrip  .setBounds (308, 548, 166, 24);
    ditherStrip.setBounds (490, 548, 186, 24);

    {
        constexpr int y = 584, width = 128, gap = 12;

        gui::ToggleChip* chips[] { &autoChip, &tpSafeChip, &deltaChip, &linkChip, &bypassChip };

        for (int i = 0; i < 5; ++i)
            chips[i]->setBounds (96 + i * (width + gap), y, width, 24);
    }
}

void CrestEditor::Content::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // A single soft gradient keeps the panel from reading as a flat rectangle
    // without adding anything the eye has to look past.
    juce::ColourGradient backdrop (theme::background.brighter (0.06f), bounds.getCentreX(), -80.0f,
                                   theme::background.darker (0.4f), bounds.getCentreX(), bounds.getBottom(),
                                   false);
    g.setGradientFill (backdrop);
    g.fillRect (bounds);

    // ------------------------------------------------------------- header
    auto header = bounds.withHeight (54.0f);

    g.setColour (theme::panel.withAlpha (0.55f));
    g.fillRect (header);

    drawMark (g, juce::Rectangle<float> (20.0f, 16.0f, 24.0f, 22.0f));

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    theme::drawTracked (g, "INDIEWAVE AUDIO",
                        juce::Rectangle<float> (54.0f, 16.0f, 220.0f, 22.0f),
                        juce::Justification::left, 2.2f);

    g.setColour (theme::text);
    g.setFont (theme::displayFont (24.0f));
    theme::drawTracked (g, "CREST", header.withTrimmedBottom (4.0f),
                        juce::Justification::horizontallyCentred, 7.0f);

    g.setColour (theme::textDim);
    g.setFont (theme::labelFont (10.0f));
    theme::drawTracked (g, "CONVERTER . CLIPPER",
                        juce::Rectangle<float> (bounds.getRight() - 240.0f, 16.0f, 220.0f, 22.0f),
                        juce::Justification::right, 2.2f);

    g.setColour (theme::outline);
    g.drawHorizontalLine (54, 0.0f, bounds.getRight());

    // ------------------------------------------------------------ dividers
    g.setColour (theme::outlineSoft);
    g.drawHorizontalLine (536, 96.0f, 784.0f);

    // -------------------------------------------------------------- footer
    g.setColour (theme::textFaint);
    g.setFont (theme::valueFont (10.0f));
    g.drawText (statusText,
                juce::Rectangle<float> (96.0f, 612.0f, 688.0f, 18.0f),
                juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------

CrestEditor::CrestEditor (CrestProcessor& p)
    : juce::AudioProcessorEditor (&p), processor (p), content (p)
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (content);

    setResizable (true, true);

    if (auto* sizeConstrainer = getConstrainer())
    {
        sizeConstrainer->setFixedAspectRatio ((double) designWidth / (double) designHeight);
        sizeConstrainer->setSizeLimits (designWidth * 3 / 4, designHeight * 3 / 4,
                                        designWidth * 3 / 2, designHeight * 3 / 2);
    }

    // Remember the size the user chose, alongside the parameters.
    const auto savedWidth = (int) processor.apvts.state.getProperty ("editorWidth", designWidth);

    setSize (juce::jlimit (designWidth * 3 / 4, designWidth * 3 / 2, savedWidth),
             juce::roundToInt (juce::jlimit (designWidth * 3 / 4, designWidth * 3 / 2, savedWidth)
                               * (double) designHeight / (double) designWidth));

    startTimerHz (4);
}

CrestEditor::~CrestEditor()
{
    setLookAndFeel (nullptr);
}

void CrestEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void CrestEditor::resized()
{
    const auto scale = (float) getWidth() / (float) designWidth;

    content.setBounds (0, 0, designWidth, designHeight);
    content.setTransform (juce::AffineTransform::scale (scale));

    processor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}

void CrestEditor::timerCallback()
{
    const auto rate    = processor.getSampleRate();
    const auto latency = processor.getLatencySamples();

    juce::String status;
    status << juce::String (rate / 1000.0, 1) << " kHz"
           << "   .   LATENCY " << latency << " smp";

    if (rate > 0.0)
        status << " / " << juce::String (1000.0 * latency / rate, 2) << " ms";

    status << "   .   INDIEWAVE AUDIO CREST v" << JucePlugin_VersionString;

    content.setStatusText (status);
}

} // namespace crest
