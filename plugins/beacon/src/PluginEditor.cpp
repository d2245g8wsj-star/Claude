#include "PluginEditor.h"
#include "BeaconPanelData.h"

namespace beacon
{

namespace rendered = indiewave::ui::rendered;

namespace
{
    const rendered::Art& art()
    {
        static const rendered::Art instance (
            [] (const char* name, int& size) { return BeaconPanelData::getNamedResource (name, size); });
        return instance;
    }

    const juce::Identifier msOpenProperty { "midSideOpen" };

    /** The settings each channel row has its own of. */
    const std::array<const char*, 17> perChannel {
        pid::input, pid::threshold, pid::attack, pid::release, pid::feedFwd, pid::limit,
        pid::hpf, pid::hpfIn, pid::bell, pid::bellIn, pid::listen,
        pid::thd, pid::tube, pid::tubeIn, pid::mix, pid::output, pid::dynLink
    };
}

BeaconEditor::Extras::Extras (Face& face, int rows)
    : split (*art().sheet ("sw"))
{
    face.place (split, "split");
    split.setTooltip ("Mid/side: fold the side row away or out. Dual mono: give the right channel its own settings.");

    const char* ids[] { "vu_main", "vu_side" };
    for (int r = 0; r < rows; ++r)
    {
        meters.push_back (std::make_unique<rendered::VuNeedles> (art(), face.faceName, ids[r], 0.0f));
        face.addAndMakeVisible (*meters.back());
    }
}

BeaconEditor::BeaconEditor (BeaconProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      folded (art(), p.apvts, "folded"),
      opened (art(), p.apvts, "open"),
      foldedExtras (folded, 1),
      openExtras (opened, 2)
{
    addChildComponent (folded);
    addChildComponent (opened);

    for (auto* extras : { &foldedExtras, &openExtras })
        extras->split.onClick = [this] { splitClicked(); };

    setResizable (true, true);
    timerCallback();

    const auto size = designSize();
    setSize (size.x * 3 / 5, size.y * 3 / 5);
    startTimerHz (30);
}

BeaconEditor::~BeaconEditor()
{
    stopTimer();
}

juce::Point<int> BeaconEditor::designSize() const
{
    return art().faceSize (open ? "open" : "folded");
}

bool BeaconEditor::wantsOpen() const
{
    switch (static_cast<ChannelMode> ((int) processor.apvts.getRawParameterValue (pid::mode)->load()))
    {
        case ChannelMode::midSide:  return (bool) processor.apvts.state.getProperty (msOpenProperty, true);
        case ChannelMode::dualMono: return processor.apvts.getRawParameterValue (pid::dualSplit)->load() >= 0.5f;
        case ChannelMode::stereo:   break;
    }
    return false;
}

void BeaconEditor::splitClicked()
{
    const auto mode = static_cast<ChannelMode> ((int) processor.apvts.getRawParameterValue (pid::mode)->load());

    if (mode == ChannelMode::midSide)
    {
        processor.apvts.state.setProperty (msOpenProperty, ! open, nullptr);
        return;
    }

    if (mode != ChannelMode::dualMono)
        return;

    auto* split = processor.apvts.getParameter (pid::dualSplit);
    const auto splitting = split->getValue() < 0.5f;

    // Splitting starts the right channel where the left is, so nothing moves
    // until a control is turned.
    if (splitting)
        for (auto* name : perChannel)
        {
            auto* from = processor.apvts.getParameter (name);
            auto* to   = processor.apvts.getParameter (pid::side (name));
            to->beginChangeGesture();
            to->setValueNotifyingHost (from->getValue());
            to->endChangeGesture();
        }

    split->beginChangeGesture();
    split->setValueNotifyingHost (splitting ? 1.0f : 0.0f);
    split->endChangeGesture();
}

void BeaconEditor::showFace (bool shouldOpen)
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

void BeaconEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void BeaconEditor::resized()
{
    const auto size = designSize();
    (open ? opened : folded).setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                                         (float) getHeight() / (float) size.y));
}

void BeaconEditor::timerCallback()
{
    // The meters read gain reduction: 0 at rest, falling to the left.
    float reduction[2];
    for (size_t c = 0; c < 2; ++c)
    {
        auto& m = processor.meters.channels[c];
        reduction[c] = juce::jmax (0.0f, MeterSource::take (m.reductionDb));
        MeterSource::take (m.thdDrive);
        MeterSource::take (m.inPeak);
        MeterSource::take (m.outPeak);
    }

    foldedExtras.meters[0]->push (-reduction[0], -reduction[1]);
    for (size_t r = 0; r < 2; ++r)
    {
        openExtras.meters[r]->setNeedles (1);
        openExtras.meters[r]->push (-reduction[r]);
    }

    const auto mode = (int) processor.apvts.getRawParameterValue (pid::mode)->load();
    const auto want = wantsOpen();

    for (auto* extras : { &foldedExtras, &openExtras })
    {
        extras->split.setEnabled (static_cast<ChannelMode> (mode) != ChannelMode::stereo);
        extras->split.setToggleState (want, juce::dontSendNotification);
    }

    if (mode == shownMode && want == shownWant)
        return;

    shownMode = mode;
    shownWant = want;
    showFace (want);
}

} // namespace beacon
