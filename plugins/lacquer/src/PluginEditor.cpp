#include "PluginEditor.h"
#include "LacquerPanelData.h"

namespace lacquer
{

namespace rendered = indiewave::ui::rendered;

namespace
{
    const rendered::Art& art()
    {
        static const rendered::Art instance (
            [] (const char* name, int& size) { return LacquerPanelData::getNamedResource (name, size); });
        return instance;
    }

    juce::String faceName (bool open, int finish)
    {
        return juce::String (open ? "open_" : "folded_") + (finish == 1 ? "black" : "white");
    }

    size_t slot (bool open, int finish) { return (size_t) ((open ? 2 : 0) + finish); }
}

LacquerEditor::Panel::Panel (const rendered::Art& a, LacquerProcessor& p, const juce::String& name, int fin)
    : face (a, p.apvts, name),
      mode (*a.sheet ("mode")),
      pilot (*a.sheet ("pilot")),
      finishSwitch (*a.sheet ("finish"))
{
    face.place (mode, "mode");
    face.place (pilot, "pilot");
    face.place (finishSwitch, "finish");
    mode.setTooltip ("STEREO: one set of controls for both channels. DUAL: left and right. M/S: mid and side.");
    finishSwitch.setTooltip ("The panel finish: Classic White or Lacquer Black. Saved with the session; it does not change the sound.");
    finishSwitch.setToggleState (fin == 1, juce::dontSendNotification);
}

LacquerEditor::LacquerEditor (LacquerProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p)
{
    for (int o = 0; o < 2; ++o)
        for (int f = 0; f < 2; ++f)
        {
            auto& panel = panels[slot (o == 1, f)];
            panel = std::make_unique<Panel> (art(), p, faceName (o == 1, f), f);
            addChildComponent (panel->face);
            panel->mode.onChange = [this] (int index) { setMode (index); };
            panel->finishSwitch.onClick = [this, f] { processor.setFinish (1 - f); };
        }

    setResizable (true, true);
    timerCallback();

    const auto size = designSize();
    setSize (size.x * 4 / 5, size.y * 4 / 5);
    startTimerHz (15);
}

LacquerEditor::~LacquerEditor()
{
    stopTimer();
}

LacquerEditor::Panel& LacquerEditor::current()
{
    return *panels[slot (open, juce::jmax (0, finish))];
}

juce::Point<int> LacquerEditor::designSize() const
{
    return art().faceSize (faceName (open, juce::jmax (0, finish)));
}

void LacquerEditor::setMode (int index)
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

void LacquerEditor::show (bool shouldOpen, int newFinish)
{
    // Keep the scale the panel is drawn at; only the height changes.
    const auto scale = getWidth() > 0 ? (double) getWidth() / designSize().x : 0.8;
    open = shouldOpen;
    finish = newFinish;

    for (auto& panel : panels)
        panel->face.setVisible (&panel->face == &current().face);

    const auto size = designSize();
    setResizeLimits (size.x / 2, size.y / 2, size.x * 3 / 2, size.y * 3 / 2);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) size.x / size.y);

    if (getWidth() > 0)
        setSize (getWidth(), juce::roundToInt (size.y * scale));

    resized();
}

void LacquerEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void LacquerEditor::resized()
{
    const auto size = designSize();
    current().face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                               (float) getHeight() / (float) size.y));
}

void LacquerEditor::timerCallback()
{
    const auto linked  = processor.apvts.getRawParameterValue (pid::link)->load() >= 0.5f;
    const auto midSide = processor.apvts.getRawParameterValue (pid::midSide)->load() >= 0.5f;
    const auto bypassed = processor.apvts.getRawParameterValue (pid::bypass)->load() >= 0.5f;
    const auto modeIndex = midSide ? 2 : linked ? 0 : 1;

    for (auto& panel : panels)
    {
        panel->mode.setIndex (modeIndex);
        panel->pilot.setLit (! bypassed);
    }

    // The finish lives in the plugin state, so a session or preset recall
    // can change it under an open editor.
    const auto storedFinish = processor.getFinish();

    if (modeIndex == shownMode && storedFinish == finish)
        return;

    shownMode = modeIndex;

    // Mid/side may still be linked (one setting for mid and side): that folds.
    show (! linked, storedFinish);
}

} // namespace lacquer
