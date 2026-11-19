#include "PluginEditor.h"
#include "BreakwaterPanelData.h"

namespace breakwater
{

namespace rendered = indiewave::ui::rendered;

namespace
{
    const rendered::Art& art()
    {
        static const rendered::Art instance (
            [] (const char* name, int& size) { return BreakwaterPanelData::getNamedResource (name, size); });
        return instance;
    }

    const juce::Identifier advancedProperty { "advancedView" };
    const juce::Identifier scaleProperty    { "editorScale" };
    constexpr double minimumScale = 0.4, maximumScale = 1.25, defaultScale = 0.75;

    juce::Rectangle<int> window (const juce::String& face, const juce::String& id)
    {
        const auto p = art().part (face, id);
        return { (int) p["x"], (int) p["y"], (int) p["w"], (int) p["h"] };
    }

    std::unique_ptr<juce::PropertiesFile> openUserSettings()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "Breakwater";
        options.folderName          = "IndieWave Audio";
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        return std::make_unique<juce::PropertiesFile> (options);
    }
}

BreakwaterEditor::Panel::Panel (BreakwaterProcessor& p, const juce::String& name, bool isAdvanced)
    : face (art(), p.apvts, name),
      advanced (*art().sheet ("sw")),
      scope (p.meters),
      meters (p.meters),
      loudness (p.meters)
{
    face.place (advanced, "advanced");
    advanced.setToggleState (isAdvanced, juce::dontSendNotification);
    advanced.setTooltip ("Advanced: the limiter's timing and linking, tone, the converter and the compressor");

    scope   .setBounds (window (name, "scope"));
    meters  .setBounds (window (name, "meters"));
    loudness.setBounds (window (name, "loudness"));
    face.addAndMakeVisible (scope);
    face.addAndMakeVisible (meters);
    face.addAndMakeVisible (loudness);

    if (isAdvanced)
    {
        compReduction = std::make_unique<juce::Label>();
        compReduction->setBounds (window (name, "comp_gr"));
        compReduction->setJustificationType (juce::Justification::centred);
        compReduction->setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold)));
        compReduction->setColour (juce::Label::textColourId, juce::Colour (0xffff7a45));
        compReduction->setInterceptsMouseClicks (false, false);
        face.addAndMakeVisible (*compReduction);
    }
}

BreakwaterEditor::BreakwaterEditor (BreakwaterProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      simple (p, "simple", false),
      full (p, "advanced", true)
{
    setOpaque (true);
    addChildComponent (simple.face);
    addChildComponent (full.face);

    simple.advanced.onClick = [this] { setAdvanced (true); };
    full.advanced.onClick   = [this] { setAdvanced (false); };

    // The project's own size first; failing that, the last size used anywhere.
    userSettings = openUserSettings();
    auto scale = userSettings->getDoubleValue (scaleProperty.toString(), defaultScale);
    if (processor.apvts.state.hasProperty (scaleProperty))
        scale = (double) processor.apvts.state.getProperty (scaleProperty);
    scale = juce::jlimit (minimumScale, maximumScale, scale);

    setResizable (true, true);
    const auto size = art().faceSize ("simple");
    setSize (juce::roundToInt (size.x * scale), juce::roundToInt (size.y * scale));
    setAdvanced (processor.apvts.state.getProperty (advancedProperty, false));

    sizeRestored = true;
    startTimerHz (30);
}

BreakwaterEditor::~BreakwaterEditor()
{
    stopTimer();

    if (userSettings != nullptr)
        userSettings->saveIfNeeded();
}

juce::Point<int> BreakwaterEditor::designSize() const
{
    return art().faceSize (advancedShown ? "advanced" : "simple");
}

void BreakwaterEditor::setAdvanced (bool shouldShow)
{
    processor.apvts.state.setProperty (advancedProperty, shouldShow, nullptr);

    const auto scale = (double) getWidth() / designSize().x;
    advancedShown = shouldShow;
    simple.face.setVisible (! shouldShow);
    full.face.setVisible (shouldShow);

    const auto size = designSize();
    setResizeLimits (juce::roundToInt (size.x * minimumScale), juce::roundToInt (size.y * minimumScale),
                     juce::roundToInt (size.x * maximumScale), juce::roundToInt (size.y * maximumScale));
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) size.x / size.y);

    setSize (getWidth(), juce::roundToInt (scale * size.y));
    resized();
}

juce::String BreakwaterEditor::caption() const
{
    if (processor.apvts.getRawParameterValue (pid::delta)->load() > 0.5f)
        return "DELTA - listening to what the limiter removes";

    if ((int) processor.apvts.getRawParameterValue (pid::engine)->load() == static_cast<int> (Engine::analog))
        return "ANALOG - no lookahead: transients pass with their shape, the true peak stage catches the rest";

    const auto index = juce::jlimit (0, (int) styles.size() - 1, (int) processor.apvts.getRawParameterValue (pid::style)->load());
    const auto& s = styles[(size_t) index];
    return juce::String (s.name).toUpperCase() + " - " + s.description;
}

juce::String BreakwaterEditor::footer() const
{
    auto text = "CORRELATION " + juce::String (processor.meters.correlation.load(), 2)
                + "     LINK " + juce::String (juce::roundToInt (processor.meters.link.load() * 100.0f)) + " %";

    if (processor.apvts.getRawParameterValue (pid::compOn)->load() > 0.5f)
        text += "     COMP " + juce::String (-compressorReductionDb, 1) + " dB";

    if (processor.apvts.getRawParameterValue (pid::sidechain)->load() > 0.5f)
        text += "     SIDECHAIN";

    return text;
}

void BreakwaterEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
}

void BreakwaterEditor::resized()
{
    const auto size = designSize();
    auto& face = advancedShown ? full.face : simple.face;
    face.setTransform (juce::AffineTransform::scale ((float) getWidth()  / (float) size.x,
                                                     (float) getHeight() / (float) size.y));

    // Remember the size, but not the default the constructor passes through
    // on its way to the stored one.
    if (sizeRestored)
    {
        const auto scale = (double) getWidth() / size.x;
        processor.apvts.state.setProperty (scaleProperty, scale, nullptr);
        if (userSettings != nullptr)
            userSettings->setValue (scaleProperty.toString(), scale);
    }
}

void BreakwaterEditor::timerCallback()
{
    auto& panel = advancedShown ? full : simple;

    const auto ceilingDb = processor.apvts.getRawParameterValue (pid::ceiling)->load();
    const auto targetIndex = juce::jlimit (0, (int) loudnessTargets.size() - 1,
                                           juce::roundToInt (processor.apvts.getRawParameterValue (pid::target)->load()));
    const auto& t = loudnessTargets[(size_t) targetIndex];

    const auto compGr = processor.meters.compressorReduction.take();
    compressorReductionDb = compGr > compressorReductionDb ? compGr : juce::jmax (compGr, compressorReductionDb - 0.5f);

    panel.meters.refresh();
    panel.scope.refresh (ceilingDb, panel.meters.getReductionDb(), caption(), footer());
    panel.loudness.refresh (ceilingDb, t.lufs, t.name);

    if (full.compReduction != nullptr)
        full.compReduction->setText (compressorReductionDb < 0.05f ? juce::String ("0.0")
                                                                   : juce::String (-compressorReductionDb, 1),
                                     juce::dontSendNotification);

    // Controls that do nothing in the current mode stay in place, dimmed, so
    // the panel does not rearrange under the hand.
    const auto engineIndex = (int) processor.apvts.getRawParameterValue (pid::engine)->load();
    const auto compressorIn = processor.apvts.getRawParameterValue (pid::compOn)->load() > 0.5f ? 1 : 0;

    if (engineIndex != shownEngine || compressorIn != shownCompressor)
    {
        shownEngine = engineIndex;
        shownCompressor = compressorIn;
        const auto modern = engineIndex == static_cast<int> (Engine::modern);

        for (auto* p : { &simple, &full })
        {
            for (auto* id : { pid::style, pid::lookahead })
                if (auto* c = p->face.find (id))
                    c->setEnabled (modern);

            for (auto* id : { pid::compMode, pid::compThreshold, pid::compRatio, pid::compAttack, pid::compRelease, pid::compHighPass })
                if (auto* c = p->face.find (id))
                    c->setEnabled (compressorIn == 1);
        }
    }
}

} // namespace breakwater
