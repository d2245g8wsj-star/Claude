#include "PluginEditor.h"
#include "AnchorPanelData.h"

namespace anchor
{

namespace rendered = indiewave::ui::rendered;

namespace
{
    const rendered::Art& art()
    {
        static const rendered::Art instance (
            [] (const char* name, int& size) { return AnchorPanelData::getNamedResource (name, size); });
        return instance;
    }

    const juce::Identifier msOpenProperty { "midSideOpen" };

    /** The settings each channel half has its own of. */
    const std::array<const char*, 12> perChannel {
        pid::threshold, pid::ratio, pid::attack, pid::release, pid::feedback, pid::scFilter,
        pid::listen, pid::thd, pid::mix, pid::makeUp, pid::makeUpIron, pid::mute
    };
}

AnchorEditor::Extras::Extras (Face& face, int halves)
    : split (*art().sheet ("sw"))
{
    face.place (split, "split");
    split.setTooltip ("Mid/side: fold the side half away or out. Dual mono: give the right channel its own settings.");

    const char* ids[] { "vu_main", "vu_side" };
    for (int h = 0; h < halves; ++h)
    {
        meters.push_back (std::make_unique<rendered::VuNeedles> (art(), face.faceName, ids[h], 0.0f));
        face.addAndMakeVisible (*meters.back());
    }
}

AnchorEditor::AnchorEditor (AnchorProcessor& p)
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

    // Iron link: the RED make up and the pad move together while the iron is in.
    auto* ironParameter = p.apvts.getParameter (pid::iron);
    for (auto* face : { &folded, &opened })
        if (auto* makeUpIron = face->get<juce::Slider> (pid::makeUpIron))
            links.push_back (std::make_unique<rendered::Follow> (*makeUpIron, p.apvts.getParameter (pid::ironLink),
                std::vector<std::pair<juce::RangedAudioParameter*, float>> { { p.apvts.getParameter (pid::ironPad), 1.0f } },
                [ironParameter] { return ironParameter->getValue() >= 0.5f; }));

    setResizable (true, true);
    timerCallback();

    const auto size = designSize();
    setSize (size.x * 7 / 10, size.y * 7 / 10);
    startTimerHz (30);
}

AnchorEditor::~AnchorEditor()
{
    stopTimer();
}

juce::Point<int> AnchorEditor::designSize() const
{
    return art().faceSize (open ? "open" : "folded");
}

bool AnchorEditor::wantsOpen() const
{
    switch (static_cast<ChannelMode> ((int) processor.apvts.getRawParameterValue (pid::mode)->load()))
    {
        case ChannelMode::midSide:  return (bool) processor.apvts.state.getProperty (msOpenProperty, true);
        case ChannelMode::dualMono: return processor.apvts.getRawParameterValue (pid::dualSplit)->load() >= 0.5f;
        case ChannelMode::stereo:   break;
    }
    return false;
}

void AnchorEditor::splitClicked()
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

void AnchorEditor::showFace (bool shouldOpen)
{
    // The second half folds out to the right: keep the height, widen.
    const auto scale = getHeight() > 0 ? (double) getHeight() / designSize().y : 0.7;
    open = shouldOpen;
    folded.setVisible (! open);
    opened.setVisible (open);

    const auto size = designSize();
    setResizeLimits (size.x * 2 / 5, size.y * 2 / 5, size.x * 6 / 5, size.y * 6 / 5);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) size.x / size.y);

    if (getHeight() > 0)
        setSize (juce::roundToInt (size.x * scale), getHeight());

    resized();
}

void AnchorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void AnchorEditor::resized()
{
    const auto size = designSize();
    (open ? opened : folded).setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                                         (float) getHeight() / (float) size.y));
}

void AnchorEditor::timerCallback()
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
    for (size_t h = 0; h < 2; ++h)
    {
        openExtras.meters[h]->setNeedles (1);
        openExtras.meters[h]->push (-reduction[h]);
    }

    // The make up knob is the GREEN stage's or, with the iron in, the RED one's.
    const auto iron = processor.apvts.getRawParameterValue (pid::iron)->load() >= 0.5f ? 1 : 0;
    if (iron != shownIron)
    {
        shownIron = iron;
        for (auto* face : { &folded, &opened })
            for (const juce::String prefix : { "", "side_" })
            {
                if (auto* c = face->find (prefix + pid::makeUp))     c->setVisible (iron == 0);
                if (auto* c = face->find (prefix + pid::makeUpIron)) c->setVisible (iron == 1);
            }
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

} // namespace anchor
