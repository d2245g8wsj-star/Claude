#include "PluginEditor.h"
#include "CrestPanelData.h"

namespace crest
{

namespace rendered = indiewave::ui::rendered;

namespace
{
    const rendered::Art& art()
    {
        static const rendered::Art instance (
            [] (const char* name, int& size) { return CrestPanelData::getNamedResource (name, size); });
        return instance;
    }

    juce::Rectangle<int> window (const juce::String& id)
    {
        const auto p = art().part ("front", id);
        return { (int) p["x"], (int) p["y"], (int) p["w"], (int) p["h"] };
    }

    juce::String figure (float value, float floorDb)
    {
        return value <= floorDb ? juce::String ("-inf") : juce::String (value, 1);
    }

    const juce::Colour amber { 0xffe08a44 };
}

// ---------------------------------------------------------------------------

CrestEditor::Display::Display (CrestProcessor& p)
    : waveform (p.waveform),
      scope (p.apvts, p.meters, p.displayKnee)
{
    setInterceptsMouseClicks (false, false);
    waveform.setOnDark (true);
    scope.setOnDark (true);
    addAndMakeVisible (waveform);
    addAndMakeVisible (scope);
}

void CrestEditor::Display::resized()
{
    const auto b = getLocalBounds().toFloat();
    inset = juce::Rectangle<float> (136.0f, 134.0f).withPosition (b.getRight() - 146.0f, b.getBottom() - 144.0f);
    waveform.setBounds (getLocalBounds().reduced (4));
    waveform.setExcluded (inset.translated (-4.0f, -4.0f).expanded (2.0f).toNearestInt());
    scope.setBounds (inset.withTrimmedTop (18.0f).reduced (4.0f, 2.0f).toNearestInt());
}

void CrestEditor::Display::paint (juce::Graphics& g)
{
    // A faint grid on the glass, the transfer inset in its own frame.
    const auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colours::white.withAlpha (0.045f));
    for (int i = 1; i < 8; ++i)
        g.fillRect (b.getX() + b.getWidth() * (float) i / 8.0f, b.getY(), 1.0f, b.getHeight());
    for (int i = 1; i < 4; ++i)
        g.fillRect (b.getX(), b.getY() + b.getHeight() * (float) i / 4.0f, b.getWidth(), 1.0f);

    g.setColour (juce::Colour (0xff120d09));
    g.fillRoundedRectangle (inset, 4.0f);
    g.setColour (amber.withAlpha (0.4f));
    g.drawRoundedRectangle (inset, 4.0f, 1.0f);
    g.setColour (amber);
    g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
    g.drawText ("TRANSFER", inset.withHeight (18.0f).translated (0.0f, 2.0f), juce::Justification::centred);
}

void CrestEditor::Figure::setText (const juce::String& t)
{
    if (t != text)
    {
        text = t;
        repaint();
    }
}

void CrestEditor::Figure::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xffffb46a));
    g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold)));
    g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
}

// ---------------------------------------------------------------------------

juce::Point<int> CrestEditor::designSize()
{
    return art().faceSize ("front");
}

CrestEditor::CrestEditor (CrestProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      face (art(), p.apvts, "front"),
      display (p),
      meter (art(), "front", "vu"),
      pilot (*art().sheet ("pilot"))
{
    addAndMakeVisible (face);

    display.setBounds (window ("display"));
    face.addAndMakeVisible (display);

    for (int i = 0; i < 4; ++i)
    {
        figures[(size_t) i].setBounds (window ("figure" + juce::String (i)));
        face.addAndMakeVisible (figures[(size_t) i]);
    }

    face.addAndMakeVisible (meter);
    face.place (pilot, "pilot");

    const auto size = designSize();
    setResizable (true, true);
    setResizeLimits (size.x * 2 / 5, size.y * 2 / 5, size.x * 3 / 2, size.y * 3 / 2);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) size.x / size.y);

    // The width the editor was last left at is kept with the session.
    const auto saved = (int) processor.apvts.state.getProperty ("editorWidth", size.x * 7 / 10);
    const auto width = juce::jlimit (size.x * 2 / 5, size.x * 3 / 2, saved);
    setSize (width, juce::roundToInt ((double) width * size.y / size.x));

    startTimerHz (30);
}

CrestEditor::~CrestEditor()
{
    stopTimer();
}

void CrestEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void CrestEditor::resized()
{
    const auto size = designSize();
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                     (float) getHeight() / (float) size.y));
    processor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}

void CrestEditor::timerCallback()
{
    auto& m = processor.meters;
    const auto inDb  = juce::Decibels::gainToDecibels (juce::jmax (m.inputPeak[0].take(),  m.inputPeak[1].take()),  -100.0f);
    const auto outDb = juce::Decibels::gainToDecibels (juce::jmax (m.outputPeak[0].take(), m.outputPeak[1].take()), -100.0f);
    meter.push (inDb, outDb);

    const auto removed = juce::jlimit (0.0f, 0.999f, m.reduction.take());
    const auto clip = -juce::Decibels::gainToDecibels (1.0f - removed, -60.0f);
    clipDb = clip > clipDb ? clip : juce::jmax (0.0f, clipDb - 0.1f);

    figures[0].setText (clipDb < 0.02f ? juce::String ("0.0") : juce::String (clipDb, 1));
    figures[1].setText (figure (processor.displayMomentary .load(), -99.0f));
    figures[2].setText (figure (processor.displayShortTerm .load(), -99.0f));
    figures[3].setText (figure (processor.displayIntegrated.load(), -99.0f));

    pilot.setLit (processor.apvts.getRawParameterValue (pid::bypass)->load() < 0.5f);
}

} // namespace crest
