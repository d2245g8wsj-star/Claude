#include "PluginEditor.h"
#include "HingePanelData.h"

namespace hinge
{

namespace rendered = indiewave::ui::rendered;

namespace
{
    const rendered::Art& art()
    {
        static const rendered::Art instance (
            [] (const char* name, int& size) { return HingePanelData::getNamedResource (name, size); });
        return instance;
    }

    float toDb (float linear) { return juce::Decibels::gainToDecibels (linear, -100.0f); }
}

HingeEditor::Extras::Extras (Face& face, bool isOpen)
    : mode (*art().sheet ("mode"))
{
    face.place (mode, "mode");
    mode.setTooltip ("STEREO: one set of controls for both channels. DUAL: left and right. M/S: mid and side.");

    const char* rows[] { "a", "b" };
    for (int i = 0; i < (isOpen ? 2 : 1); ++i)
    {
        lamps[(size_t) i] = std::make_unique<rendered::Lamp> (*art().sheet ("lamp"));
        face.place (*lamps[(size_t) i], juce::String ("lamp_") + rows[i]);
        meters[(size_t) i] = std::make_unique<rendered::VuNeedles> (art(), face.faceName, juce::String ("vu_") + rows[i]);
        face.addAndMakeVisible (*meters[(size_t) i]);
    }
}

HingeEditor::HingeEditor (HingeProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      folded (art(), p.apvts, "folded"),
      opened (art(), p.apvts, "open"),
      foldedExtras (folded, false),
      openExtras (opened, true)
{
    addChildComponent (folded);
    addChildComponent (opened);

    for (auto* extras : { &foldedExtras, &openExtras })
        extras->mode.onChange = [this] (int index) { setMode (index); };

    setResizable (true, true);
    timerCallback();
    showFace (open);

    const auto size = designSize();
    setSize (size.x * 3 / 5, size.y * 3 / 5);
    startTimerHz (30);
}

HingeEditor::~HingeEditor()
{
    stopTimer();
}

juce::Point<int> HingeEditor::designSize() const
{
    return art().faceSize (open ? "open" : "folded");
}

void HingeEditor::setMode (int index)
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

void HingeEditor::showFace (bool shouldOpen)
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
}

void HingeEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void HingeEditor::resized()
{
    const auto size = designSize();
    auto& face = open ? opened : folded;
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                     (float) getHeight() / (float) size.y));
}

void HingeEditor::timerCallback()
{
    auto& a = processor.meters.channels[0];
    auto& b = processor.meters.channels[1];
    const float peaks[2] = { MeterSource::take (a.outputPeak), MeterSource::take (b.outputPeak) };
    float moves[2] {};
    for (size_t s = 0; s < 2; ++s)
    {
        moves[0] = juce::jmax (moves[0], MeterSource::take (a.shelfMovement[s]));
        moves[1] = juce::jmax (moves[1], MeterSource::take (b.shelfMovement[s]));
    }

    const auto engaged = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load() > 0.5f; };
    const bool on[2] { engaged ("a_engage"), engaged ("b_engage") };

    // The lamp is lit while the row is engaged, and glows brighter as its
    // dynamic shelves move.
    const auto glow = [] (bool lit, float move) { return lit ? 0.35f + 0.65f * juce::jlimit (0.0f, 1.0f, move / 3.0f) : 0.0f; };

    // Folded, one meter carries both channels and one lamp both rows.
    foldedExtras.meters[0]->push (toDb (peaks[0]), toDb (peaks[1]));
    foldedExtras.lamps[0]->setLevel (glow (on[0], juce::jmax (moves[0], moves[1])));
    for (int i = 0; i < 2; ++i)
    {
        openExtras.meters[(size_t) i]->setNeedles (1);
        openExtras.meters[(size_t) i]->push (toDb (peaks[i]));
        openExtras.lamps[(size_t) i]->setLevel (glow (on[i], moves[i]));
    }

    const auto linked  = processor.apvts.getRawParameterValue (pid::link)->load() >= 0.5f;
    const auto midSide = processor.apvts.getRawParameterValue (pid::midSide)->load() >= 0.5f;
    const auto modeIndex = midSide ? 2 : linked ? 0 : 1;

    foldedExtras.mode.setIndex (modeIndex);
    openExtras.mode.setIndex (modeIndex);

    if (modeIndex == shownMode)
        return;

    shownMode = modeIndex;

    // Mid/side may still be linked (one setting for mid and side): that folds.
    if ((! linked) != open || ! (folded.isVisible() || opened.isVisible()))
        showFace (! linked);
}

} // namespace hinge
