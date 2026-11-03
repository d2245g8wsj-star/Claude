#include "PluginEditor.h"

namespace swell
{

namespace ui = indiewave::ui;
namespace colour = modern::colour;

namespace
{
    constexpr float earWidth = 64.0f;
    constexpr float rowCentres[] = { 102.0f, 272.0f };

    // Horizontal plan of a row, left to right.
    constexpr float letterX = 282.0f, inputX = 340.0f;
    constexpr float bandX[] = { 394.0f, 660.0f, 926.0f };
    constexpr float bandWidth = 252.0f, bandHeight = 158.0f;
    constexpr float outputX = 1236.0f, meterX = 1300.0f, meterWidth = 250.0f;

    juce::String signedDb (double v, int decimals)
    {
        const auto shown = std::abs (v) < 0.5 * std::pow (10.0, -decimals) ? 0.0 : v;
        return (shown > 0.0 ? "+" : "") + juce::String (shown, decimals);
    }

    juce::String hertz (double hz)
    {
        if (hz < 1000.0)
            return juce::String (juce::roundToInt (hz)) + " Hz";

        return juce::String (hz / 1000.0, hz < 10000.0 ? 2 : 1).trimCharactersAtEnd ("0").trimCharactersAtEnd (".") + " kHz";
    }

    modern::ArcKnob::Style trim (const char* name)
    {
        return { 26.0f, colour::ink, true, name, [] (double v) { return signedDb (v, 0) + " dB"; } };
    }

    modern::ArcKnob::Style level (juce::Colour c)
    {
        return { 28.0f, c, true, "LEVEL", [] (double v) { return signedDb (v, 1); } };
    }

    modern::ArcKnob::Style frequency (juce::Colour c)
    {
        return { 19.0f, c, false, "FREQ", [] (double v) { return hertz (v); } };
    }

    constexpr const char* bandTitle[] = { "LO SHELF", "MID PEAK", "HI SHELF" };
    const juce::Colour bandColour[] = { colour::lo, colour::mid, colour::hi };

    // The switch shows STEREO, DUAL, M/S; the parameter's order is dual, stereo, M/S.
    StripMode modeAt (int segment)
    {
        return segment == 0 ? StripMode::stereo : segment == 1 ? StripMode::dualMono : StripMode::midSide;
    }

    int segmentOf (StripMode mode)
    {
        return mode == StripMode::stereo ? 0 : mode == StripMode::dualMono ? 1 : 2;
    }
}

// ---------------------------------------------------------------------------

SwellEditor::Strip::Strip (SwellProcessor& p, int index)
    : input    (p.apvts, pid::forStrip (index, pid::input),   trim ("INPUT")),
      loLevel  (p.apvts, pid::forStrip (index, pid::loGain),  level (colour::lo)),
      loFreq   (p.apvts, pid::forStrip (index, pid::loFreq),  frequency (colour::lo)),
      midLevel (p.apvts, pid::forStrip (index, pid::midGain), level (colour::mid)),
      midFreq  (p.apvts, pid::forStrip (index, pid::midFreq), frequency (colour::mid)),
      hiLevel  (p.apvts, pid::forStrip (index, pid::hiGain),  level (colour::hi)),
      hiFreq   (p.apvts, pid::forStrip (index, pid::hiFreq),  frequency (colour::hi)),
      output   (p.apvts, pid::forStrip (index, pid::output),  trim ("OUTPUT")),
      loIn     (p.apvts, pid::forStrip (index, pid::loIn),    colour::lo,  "IN"),
      midIn    (p.apvts, pid::forStrip (index, pid::midIn),   colour::mid, "IN"),
      hiIn     (p.apvts, pid::forStrip (index, pid::hiIn),    colour::hi,  "IN"),
      active   (p.apvts, pid::forStrip (index, pid::active),  colour::ink, "IN"),
      loDrive  (colour::lo),
      midDrive (colour::mid),
      hiDrive  (colour::hi)
{
}

std::vector<juce::Component*> SwellEditor::Strip::components()
{
    return { &input, &loLevel, &loFreq, &midLevel, &midFreq, &hiLevel, &hiFreq, &output,
             &loIn, &midIn, &hiIn, &active, &loDrive, &midDrive, &hiDrive, &meter };
}

void SwellEditor::Strip::layout (float y)
{
    using K = modern::ArcKnob;
    input .setBounds (K::boundsAt ({ inputX,  y + 6.0f }, 26.0f));
    output.setBounds (K::boundsAt ({ outputX, y + 6.0f }, 26.0f));

    const std::array<std::tuple<K*, K*, modern::Pill*, modern::DriveLed*>, 3> bands {
        std::make_tuple (&loLevel,  &loFreq,  &loIn,  &loDrive),
        std::make_tuple (&midLevel, &midFreq, &midIn, &midDrive),
        std::make_tuple (&hiLevel,  &hiFreq,  &hiIn,  &hiDrive)
    };

    for (size_t b = 0; b < bands.size(); ++b)
    {
        const auto x = bandX[b];
        auto [gain, freq, in, drive] = bands[b];
        gain ->setBounds (K::boundsAt ({ x + 66.0f,  y + 6.0f },  28.0f));
        freq ->setBounds (K::boundsAt ({ x + 160.0f, y + 10.0f }, 19.0f));
        in   ->setBounds (juce::Rectangle<float> (36.0f, 20.0f).withCentre ({ x + 222.0f, y + 10.0f }).toNearestInt());
        drive->setBounds (juce::Rectangle<float> (18.0f, 18.0f).withCentre ({ x + 214.0f, y - 64.0f }).toNearestInt());
    }

    active.setBounds (juce::Rectangle<float> (36.0f, 18.0f).withCentre ({ meterX + meterWidth - 20.0f, y - 44.0f }).toNearestInt());
    meter .setBounds (juce::Rectangle<float> (meterX, y - 26.0f, meterWidth, 74.0f).toNearestInt());
}

// ---------------------------------------------------------------------------

SwellEditor::Face::Face (SwellProcessor& p)
    : processor (p),
      fold (ui::FoldTab::Edge::bottom, colour::ink, juce::Colour (0xff2b2f34))
{
    for (int i = 0; i < numChannelStrips; ++i)
    {
        strips[(size_t) i] = std::make_unique<Strip> (p, i);

        for (auto* child : strips[(size_t) i]->components())
            i == 0 ? addAndMakeVisible (child) : addChildComponent (child);
    }

    addAndMakeVisible (modeSwitch);
    modeSwitch.onChange = [this] (int segment) { setMode (modeAt (segment)); };

    addAndMakeVisible (fold);
    fold.onClick = [this]
    {
        // Folding is stereo, one strip for both channels; unfolding goes back
        // to the two strip mode last used, dual unless it was mid/side.
        setMode (open ? StripMode::stereo : lastOpenMode);
    };

    refresh();
}

void SwellEditor::Face::setMode (StripMode mode)
{
    auto* parameter = processor.apvts.getParameter (pid::mode);
    parameter->beginChangeGesture();
    parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) static_cast<int> (mode)));
    parameter->endChangeGesture();
}

void SwellEditor::Face::resized()
{
    for (int i = 0; i < numChannelStrips; ++i)
        strips[(size_t) i]->layout (rowCentres[i]);

    modeSwitch.setBounds (86, 148, 144, 24);

    const auto height = (float) (open ? designHeightOpen : designHeightFolded);
    fold.setBounds (juce::Rectangle<float> (760.0f, height - 22.0f, 120.0f, 17.0f).toNearestInt());
}

void SwellEditor::Face::refresh()
{
    auto& a = processor.meters.channels[0];
    auto& b = processor.meters.channels[1];
    const float peaks[2] = { MeterSource::take (a.outputPeak), MeterSource::take (b.outputPeak) };
    float drives[2][3];

    for (size_t band = 0; band < 3; ++band)
    {
        drives[0][band] = MeterSource::take (a.bandDrive[band]);
        drives[1][band] = MeterSource::take (b.bandDrive[band]);
    }

    // Folded, the one row carries both channels: both bars, and each band's
    // drive lamp shows the harder working side.
    if (open)
    {
        strips[0]->meter.push ({ peaks[0] });
        strips[1]->meter.push ({ peaks[1] });
    }
    else
    {
        strips[0]->meter.push ({ peaks[0], peaks[1] });
    }

    for (int row = 0; row < 2; ++row)
    {
        auto& s = *strips[(size_t) row];
        const auto drive = [&] (int band)
        {
            return open ? drives[row][band] : juce::jmax (drives[0][band], drives[1][band]);
        };
        s.loDrive .push (drive (0));
        s.midDrive.push (drive (1));
        s.hiDrive .push (drive (2));
    }

    const auto mode = static_cast<StripMode> (juce::jlimit (0, 2, (int) processor.apvts.getRawParameterValue (pid::mode)->load()));
    const auto modeIndex = static_cast<int> (mode);
    modeSwitch.setSelected (segmentOf (mode));

    if (mode != StripMode::stereo)
        lastOpenMode = mode;

    if (modeIndex == shownMode)
        return;

    const auto shouldOpen = mode != StripMode::stereo;
    const auto changed = shouldOpen != open;
    shownMode = modeIndex;
    open = shouldOpen;

    for (auto* child : strips[1]->components())
        child->setVisible (open);

    // Each strip's meter reads its own output, before mid/side goes back to
    // left and right; folded, the one meter shows both channels.
    const auto midSide = mode == StripMode::midSide;
    strips[0]->meter.setChannels (open ? juce::StringArray { midSide ? "M" : "L" } : juce::StringArray { "L", "R" });
    strips[1]->meter.setChannels ({ midSide ? "S" : "R" });

    fold.setCaption (lastOpenMode == StripMode::midSide ? juce::String (juce::CharPointer_UTF8 ("SPLIT M · S"))
                                                        : juce::String (juce::CharPointer_UTF8 ("DUAL L · R")),
                     "FOLD");
    fold.setOpen (open);

    resized();
    repaint();

    if (changed && onFoldChanged)
        onFoldChanged();
}

void SwellEditor::Face::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    ui::drawPlate (g, bounds, colour::plateTop, colour::plateBottom, 0x53574c);
    ui::drawRackEar (g, bounds.withWidth (earWidth), true);
    ui::drawRackEar (g, bounds.withLeft (bounds.getRight() - earWidth), false);

    // Name block.
    ui::drawLogo (g, { 158.0f, 44.0f }, 0.85f, colour::ink, colour::dim);
    modern::drawText (g, "Swell", { 78.0f, 84.0f, 160.0f, 32.0f }, modern::sans (28.0f, false), colour::ink);
    modern::drawText (g, "HARMONIC EQUALISER", { 68.0f, 116.0f, 180.0f, 11.0f }, modern::sans (7.5f), colour::dim,
                      juce::Justification::centred, 2.0f);

    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.fillRect (252.0f, 22.0f, 1.0f, bounds.getHeight() - 48.0f);

    paintRow (g, 0, rowCentres[0]);

    if (open)
        paintRow (g, 1, rowCentres[1]);
}

void SwellEditor::Face::paintRow (juce::Graphics& g, int index, float y) const
{
    const auto mode = static_cast<StripMode> (juce::jmax (0, shownMode));

    if (open)
    {
        const auto letter = mode == StripMode::midSide ? (index == 0 ? "M" : "S") : (index == 0 ? "L" : "R");
        modern::drawText (g, letter, { letterX - 20.0f, y - 18.0f, 40.0f, 36.0f }, modern::sans (26.0f, false), colour::ink);
    }

    for (int b = 0; b < 3; ++b)
    {
        const auto x = bandX[b];
        modern::drawSection (g, { x, y - 82.0f, bandWidth, bandHeight });

        g.setColour (bandColour[b]);
        g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x + 18.0f, y - 64.0f }));
        modern::drawText (g, bandTitle[b], { x + 30.0f, y - 70.0f, 120.0f, 12.0f }, modern::sans (9.0f), colour::ink,
                          juce::Justification::centredLeft, 1.4f);
        modern::drawText (g, "DRIVE", { x + 146.0f, y - 70.0f, 56.0f, 12.0f }, modern::sans (7.0f), colour::dim,
                          juce::Justification::centredRight, 1.0f);
    }

    modern::drawText (g, "METER", { meterX, y - 50.0f, 80.0f, 12.0f }, modern::sans (8.0f), colour::dim,
                      juce::Justification::centredLeft, 1.4f);
    modern::drawText (g, "STRIP", { meterX + meterWidth - 100.0f, y - 50.0f, 58.0f, 12.0f }, modern::sans (7.0f),
                      colour::dim, juce::Justification::centredRight, 1.0f);
}

// ---------------------------------------------------------------------------

SwellEditor::SwellEditor (SwellProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      face (p)
{
    addAndMakeVisible (face);
    face.onFoldChanged = [this] { applyFold(); };

    setResizable (true, true);
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;
    setResizeLimits (designWidth / 2, height / 2, designWidth * 8 / 5, height * 8 / 5);

    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) designWidth / height);

    setSize (designWidth * 4 / 5, height * 4 / 5);
    startTimerHz (30);
}

SwellEditor::~SwellEditor()
{
    stopTimer();
}

void SwellEditor::applyFold()
{
    // Keep the scale the panel is drawn at; only the height changes.
    const auto scale  = (double) getWidth() / designWidth;
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;

    setResizeLimits (designWidth / 2, height / 2, designWidth * 8 / 5, height * 8 / 5);

    if (auto* sizeLimits = getConstrainer())
        sizeLimits->setFixedAspectRatio ((double) designWidth / height);

    setSize (getWidth(), juce::roundToInt (height * scale));
}

void SwellEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::plateBottom);
}

void SwellEditor::resized()
{
    const auto height = face.isOpen() ? designHeightOpen : designHeightFolded;
    face.setBounds (0, 0, designWidth, height);
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) designWidth,
                                                     (float) getHeight() / (float) height));
}

void SwellEditor::timerCallback()
{
    face.refresh();
}

} // namespace swell
