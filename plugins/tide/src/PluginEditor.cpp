#include "PluginEditor.h"
#include "TidePanelData.h"

namespace tide
{

namespace
{
    const indiewave::ui::rendered::Art& art()
    {
        static const indiewave::ui::rendered::Art instance (
            [] (const char* name, int& size) { return TidePanelData::getNamedResource (name, size); });
        return instance;
    }

    const char* const secondHalf[] { "b_lo_freq", "b_lo_gain", "b_lo_shelf", "b_mid_freq", "b_mid_gain",
                                     "b_hi_freq", "b_hi_gain", "b_hi_shelf", "b_hpf_freq", "b_hpf_in",
                                     "b_hpf_x5", "b_level" };
}

juce::Point<int> TideEditor::designSize()
{
    return art().faceSize ("front");
}

TideEditor::TideEditor (TideProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      face (art(), p.apvts, "front")
{
    addAndMakeVisible (face);

    const auto size = designSize();
    setResizable (true, true);
    setResizeLimits (size.x / 2, size.y / 2, size.x * 3 / 2, size.y * 3 / 2);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) size.x / size.y);

    setSize (size.x * 4 / 5, size.y * 4 / 5);
    startTimerHz (15);
}

TideEditor::~TideEditor()
{
    stopTimer();
}

void TideEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void TideEditor::resized()
{
    const auto size = designSize();
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                     (float) getHeight() / (float) size.y));
}

void TideEditor::timerCallback()
{
    // Linked, the left half sets both; the right half's controls stay where
    // they were but dim, so they cannot look as if they did something.
    const auto linked = processor.apvts.getRawParameterValue ("link")->load() > 0.5f ? 1 : 0;

    if (linked == shownLink)
        return;

    shownLink = linked;

    for (auto* id : secondHalf)
        if (auto* c = face.find (id))
            c->setEnabled (linked == 0);
}

} // namespace tide
