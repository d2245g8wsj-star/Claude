#include "PluginEditor.h"
#include "LanternPanelData.h"

namespace lantern
{

namespace rendered = indiewave::ui::rendered;

namespace
{
    const rendered::Art& art()
    {
        static const rendered::Art instance (
            [] (const char* name, int& size) { return LanternPanelData::getNamedResource (name, size); });
        return instance;
    }

    const juce::Identifier msOpenProperty { "midSideOpen" };

    juce::Rectangle<int> window (const juce::String& face, const juce::String& id)
    {
        const auto p = art().part (face, id);
        return { (int) p["x"], (int) p["y"], (int) p["w"], (int) p["h"] };
    }
}

// ---------------------------------------------------------------------------

float LanternEditor::Curve::frequencyAt (int point) noexcept
{
    return 20.0f * std::pow (1000.0f, (float) point / (float) (points - 1));   // 20 Hz to 20 kHz
}

void LanternEditor::Curve::setCurves (std::vector<float> a, std::vector<float> b, float spanDb)
{
    if (a == first && b == second && std::abs (spanDb - span) < 1.0e-3f)
        return;

    first = std::move (a);
    second = std::move (b);
    span = spanDb;
    repaint();
}

void LanternEditor::Curve::paint (juce::Graphics& g)
{
    const auto plot = getLocalBounds().toFloat().reduced (8.0f, 8.0f);
    const auto xFor = [plot] (float hz) { return plot.getX() + plot.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); };
    const auto yFor = [plot, this] (float db) { return plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / span); };

    const auto glow = juce::Colour (0xff3dff9a);
    g.setColour (glow.withAlpha (0.10f));
    for (auto hz : { 100.0f, 1000.0f, 10000.0f })
        g.drawVerticalLine ((int) xFor (hz), plot.getY(), plot.getBottom());
    for (auto fraction : { -0.5f, 0.5f })
        g.drawHorizontalLine ((int) yFor (fraction * span), plot.getX(), plot.getRight());
    g.setColour (glow.withAlpha (0.22f));
    g.drawHorizontalLine ((int) yFor (0.0f), plot.getX(), plot.getRight());

    const auto trace = [&] (const std::vector<float>& curve, juce::Colour colour)
    {
        if (curve.size() != (size_t) points)
            return;

        juce::Path p;
        for (int i = 0; i < points; ++i)
        {
            const juce::Point<float> at { xFor (frequencyAt (i)), yFor (curve[(size_t) i]) };
            if (i == 0) p.startNewSubPath (at); else p.lineTo (at);
        }

        g.setColour (colour.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour);
        g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    trace (second, juce::Colour (0xffffb03a));
    trace (first, glow);

    g.setColour (glow.withAlpha (0.55f));
    g.setFont (juce::Font (juce::FontOptions (8.0f, juce::Font::bold)));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (juce::roundToInt (span)) + " dB",
                getLocalBounds().reduced (8, 4), juce::Justification::topRight, false);
}

// ---------------------------------------------------------------------------

LanternEditor::Extras::Extras (Face& face)
    : split (*art().sheet ("sw"))
{
    face.place (split, "split");
    split.setTooltip ("Mid/side: fold the side half away or out. Dual mono: give the right channel its own settings.");
    curve.setBounds (window (face.faceName, "curve"));
    face.addAndMakeVisible (curve);
}

LanternEditor::LanternEditor (LanternProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      folded (art(), p.apvts, "folded"),
      opened (art(), p.apvts, "open"),
      foldedExtras (folded),
      openExtras (opened)
{
    addChildComponent (folded);
    addChildComponent (opened);

    for (auto* extras : { &foldedExtras, &openExtras })
        extras->split.onClick = [this] { splitClicked(); };

    // Iron link: the RED output and the pad move together while the iron is in.
    auto* ironParameter = p.apvts.getParameter (pid::iron);
    const auto follow = [&] (Face& face) -> std::unique_ptr<rendered::Follow>
    {
        if (auto* outputIron = face.get<juce::Slider> (pid::outputIron))
            return std::make_unique<rendered::Follow> (*outputIron, p.apvts.getParameter (pid::ironLink),
                std::vector<std::pair<juce::RangedAudioParameter*, float>> { { p.apvts.getParameter (pid::ironPad), 1.0f } },
                [ironParameter] { return ironParameter->getValue() >= 0.5f; });
        return {};
    };
    folllowFolded = follow (folded);
    followOpen = follow (opened);

    setResizable (true, true);
    timerCallback();

    const auto size = designSize();
    setSize (size.x * 7 / 10, size.y * 7 / 10);
    startTimerHz (30);
}

LanternEditor::~LanternEditor()
{
    stopTimer();
}

juce::Point<int> LanternEditor::designSize() const
{
    return art().faceSize (open ? "open" : "folded");
}

bool LanternEditor::wantsOpen() const
{
    switch (static_cast<ChannelMode> ((int) processor.apvts.getRawParameterValue (pid::mode)->load()))
    {
        case ChannelMode::midSide:  return (bool) processor.apvts.state.getProperty (msOpenProperty, true);
        case ChannelMode::dualMono: return processor.apvts.getRawParameterValue (pid::dualSplit)->load() >= 0.5f;
        case ChannelMode::stereo:   break;
    }
    return false;
}

void LanternEditor::splitClicked()
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
        for (const auto& name : pid::perChannel())
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

void LanternEditor::showFace (bool shouldOpen)
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

void LanternEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void LanternEditor::resized()
{
    const auto size = designSize();
    (open ? opened : folded).setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                                         (float) getHeight() / (float) size.y));
}

std::vector<float> LanternEditor::curveOf (bool secondSet) const
{
    const auto settings = processor.settingsFor (secondSet);
    std::vector<float> response ((size_t) Curve::points);
    for (int i = 0; i < Curve::points; ++i)
        response[(size_t) i] = (float) eq::responseDb (settings, Curve::frequencyAt (i));
    return response;
}

void LanternEditor::timerCallback()
{
    for (size_t ch = 0; ch < 2; ++ch)
    {
        auto& m = processor.meters.channels[ch];
        MeterSource::take (m.reductionDb);
        MeterSource::take (m.inPeak);
        MeterSource::take (m.thdDrive);
        MeterSource::take (m.outPeak);
    }

    // The output knob is the GREEN stage's or, with the iron in, the RED one's.
    const auto iron = processor.apvts.getRawParameterValue (pid::iron)->load() >= 0.5f ? 1 : 0;
    if (iron != shownIron)
    {
        shownIron = iron;
        for (auto* face : { &folded, &opened })
            for (const juce::String prefix : { "", "side_" })
            {
                if (auto* c = face->find (prefix + pid::output))     c->setVisible (iron == 0);
                if (auto* c = face->find (prefix + pid::outputIron)) c->setVisible (iron == 1);
            }
    }

    const auto wide = [this] (const juce::String& prefix)
    {
        return processor.apvts.getRawParameterValue (prefix + pid::gainRange)->load() >= 0.5f;
    };
    const auto wideRange = wide ("") || (open && wide ("side_"));
    (open ? openExtras : foldedExtras).curve.setCurves (curveOf (false), open ? curveOf (true) : std::vector<float> {},
                                                        wideRange ? 16.0f : 6.0f);

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

} // namespace lantern
