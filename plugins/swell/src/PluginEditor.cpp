#include "PluginEditor.h"
#include "SwellPanelData.h"

namespace swell
{

namespace rendered = indiewave::ui::rendered;

namespace
{
    const rendered::Art& art()
    {
        static const rendered::Art instance (
            [] (const char* name, int& size) { return SwellPanelData::getNamedResource (name, size); });
        return instance;
    }

    float toDb (float linear) { return juce::Decibels::gainToDecibels (linear, -100.0f); }

    const char* const strips[] { "a", "b" };
    const char* const bands[] { "lo", "mid", "hi" };
}

SwellEditor::Extras::Extras (Face& face, int rows)
{
    for (int r = 0; r < rows; ++r)
    {
        for (auto* band : bands)
        {
            drive.push_back (std::make_unique<rendered::Lamp> (*art().sheet ("drive")));
            face.place (*drive.back(), juce::String ("drive_") + strips[r] + "_" + band);
        }
        meters.push_back (std::make_unique<rendered::VuNeedles> (art(), face.faceName, juce::String ("vu_") + strips[r]));
        face.addAndMakeVisible (*meters.back());
    }
}

SwellEditor::SwellEditor (SwellProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      folded (art(), p.apvts, "folded"),
      opened (art(), p.apvts, "open"),
      foldedExtras (folded, 1),
      openExtras (opened, 2)
{
    addChildComponent (folded);
    addChildComponent (opened);

    setResizable (true, true);
    timerCallback();

    const auto size = designSize();
    setSize (size.x * 3 / 5, size.y * 3 / 5);
    startTimerHz (30);
}

SwellEditor::~SwellEditor()
{
    stopTimer();
}

juce::Point<int> SwellEditor::designSize() const
{
    return art().faceSize (open ? "open" : "folded");
}

void SwellEditor::showFace (bool shouldOpen)
{
    // Keep the scale the panel is drawn at; only the height changes.
    const auto scale = getWidth() > 0 ? (double) getWidth() / designSize().x : 0.6;
    open = shouldOpen;
    folded.setVisible (! open);
    opened.setVisible (open);

    const auto size = designSize();
    setResizeLimits (size.x * 2 / 5, size.y * 2 / 5, size.x, size.y);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) size.x / size.y);

    if (getWidth() > 0)
        setSize (getWidth(), juce::roundToInt (size.y * scale));

    resized();
}

void SwellEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void SwellEditor::resized()
{
    const auto size = designSize();
    (open ? opened : folded).setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                                         (float) getHeight() / (float) size.y));
}

void SwellEditor::timerCallback()
{
    float peaks[2], drives[2][3];
    for (size_t c = 0; c < 2; ++c)
    {
        auto& source = processor.meters.channels[c];
        peaks[c] = MeterSource::take (source.outputPeak);
        for (size_t b = 0; b < 3; ++b)
            drives[c][b] = MeterSource::take (source.bandDrive[b]);
    }

    // Folded, one meter carries both channels and each lamp the harder working side.
    foldedExtras.meters[0]->push (toDb (peaks[0]), toDb (peaks[1]));
    for (size_t b = 0; b < 3; ++b)
        foldedExtras.drive[b]->setLevel (juce::jmax (drives[0][b], drives[1][b]));

    for (size_t r = 0; r < 2; ++r)
    {
        openExtras.meters[r]->setNeedles (1);
        openExtras.meters[r]->push (toDb (peaks[r]));
        for (size_t b = 0; b < 3; ++b)
            openExtras.drive[r * 3 + b]->setLevel (drives[r][b]);
    }

    const auto mode = juce::jlimit (0, 2, (int) processor.apvts.getRawParameterValue (pid::mode)->load());

    if (mode == shownMode)
        return;

    shownMode = mode;
    showFace (static_cast<StripMode> (mode) != StripMode::stereo);
}

} // namespace swell
