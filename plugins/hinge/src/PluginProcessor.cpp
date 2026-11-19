#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace hinge
{

namespace
{
    juce::String signedDb (double value, int decimals)
    {
        if (std::abs (value) < 0.5 * std::pow (10.0, -decimals))
            return juce::String (0.0, decimals) + " dB";

        return (value > 0.0 ? "+" : "") + juce::String (value, decimals) + " dB";
    }

    juce::StringArray names (const std::array<const char*, network::numPositions>& table)
    {
        juce::StringArray out;

        for (auto* name : table)
            out.add (name);

        return out;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout HingeProcessor::createParameterLayout()
{
    using Layout  = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatP  = juce::AudioParameterFloat;
    using ChoiceP = juce::AudioParameterChoice;
    using BoolP   = juce::AudioParameterBool;
    using Attr    = juce::AudioParameterFloatAttributes;

    Layout layout;

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::link, 1 },    "Link",     true));
    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::midSide, 1 }, "Mid/Side", false));

    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        const auto id   = [strip] (const char* name) { return juce::ParameterID { pid::forStrip (strip, name), 1 }; };
        const auto name = [strip] (const char* text) { return juce::String (strip == 0 ? "A " : "B ") + text; };

        // The level attenuators have twenty one detents, half a dB apart.
        const juce::NormalisableRange<float> levelRange ((float) -network::maxLevelDb, (float) network::maxLevelDb,
                                                         (float) network::levelDbPerDetent);
        const auto levelAttributes = Attr().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return signedDb (v, 1); });

        layout.add (std::make_unique<ChoiceP> (id (pid::lowCut),   name ("Low Cut"),    names (network::lowCutNames),   0));
        layout.add (std::make_unique<ChoiceP> (id (pid::lowShelf), name ("Low Shelf"),  names (network::lowShelfNames), 2));
        layout.add (std::make_unique<FloatP>  (id (pid::lowLevel), name ("Low Level"),  levelRange, 0.0f, levelAttributes));
        layout.add (std::make_unique<FloatP>  (id (pid::highLevel), name ("High Level"), levelRange, 0.0f, levelAttributes));
        layout.add (std::make_unique<ChoiceP> (id (pid::highShelf), name ("High Shelf"), names (network::highShelfNames), 2));
        layout.add (std::make_unique<ChoiceP> (id (pid::highCut),  name ("High Cut"),   names (network::highCutNames),  0));

        layout.add (std::make_unique<juce::AudioParameterInt> (
            id (pid::output), name ("Output"), -network::outputSteps, network::outputSteps, 0,
            juce::AudioParameterIntAttributes().withLabel ("dB").withStringFromValueFunction (
                [] (int step, int) { return signedDb (juce::Decibels::gainToDecibels (network::outputGain (step)), 1); })));

        layout.add (std::make_unique<BoolP> (id (pid::engage), name ("Engage"), true));

        layout.add (std::make_unique<FloatP> (id (pid::threshold), name ("Dyn Threshold"),
                                              juce::NormalisableRange<float> (range::thresholdMin, range::thresholdMax, 0.5f),
                                              -24.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (
                                                  [] (float v, int) { return juce::String (v, 1) + " dB"; })));

        const juce::NormalisableRange<float> dynRange (-range::dynDb, range::dynDb, 0.1f);
        const auto dynAttributes = Attr().withLabel ("dB").withStringFromValueFunction (
            [] (float v, int) { return signedDb (v, 1); });

        layout.add (std::make_unique<FloatP> (id (pid::lowDyn),  name ("Low Dyn"),  dynRange, 0.0f, dynAttributes));
        layout.add (std::make_unique<FloatP> (id (pid::highDyn), name ("High Dyn"), dynRange, 0.0f, dynAttributes));
    }

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

HingeProcessor::HingeProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "HINGE", createParameterLayout())
{
    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        auto& s = strips[(size_t) strip];
        const auto id = [strip] (const char* name) { return pid::forStrip (strip, name); };

        s.lowCut    = findParameter<juce::AudioParameterChoice> (id (pid::lowCut));
        s.lowShelf  = findParameter<juce::AudioParameterChoice> (id (pid::lowShelf));
        s.lowLevel  = findParameter<juce::AudioParameterFloat>  (id (pid::lowLevel));
        s.highLevel = findParameter<juce::AudioParameterFloat>  (id (pid::highLevel));
        s.highShelf = findParameter<juce::AudioParameterChoice> (id (pid::highShelf));
        s.highCut   = findParameter<juce::AudioParameterChoice> (id (pid::highCut));
        s.output    = findParameter<juce::AudioParameterInt>    (id (pid::output));
        s.engage    = findParameter<juce::AudioParameterBool>   (id (pid::engage));
        s.threshold = findParameter<juce::AudioParameterFloat>  (id (pid::threshold));
        s.lowDyn    = findParameter<juce::AudioParameterFloat>  (id (pid::lowDyn));
        s.highDyn   = findParameter<juce::AudioParameterFloat>  (id (pid::highDyn));
    }

    linkParam       = findParameter<juce::AudioParameterBool> (pid::link);
    midSideParam    = findParameter<juce::AudioParameterBool> (pid::midSide);
    bypassParameter = findParameter<juce::AudioParameterBool> (pid::bypass);
}

ChannelSettings HingeProcessor::StripParameters::read (bool bypassed) const
{
    ChannelSettings s;
    s.lowCut      = lowCut->getIndex();
    s.lowShelf    = lowShelf->getIndex();
    s.lowLevelDb  = lowLevel->get();
    s.highLevelDb = highLevel->get();
    s.highShelf   = highShelf->getIndex();
    s.highCut     = highCut->getIndex();
    s.outputStep  = output->get();
    s.engaged     = engage->get() && ! bypassed;
    s.thresholdDb = threshold->get();
    // On the parameter's own 0.1 dB grid, in double: a float range snapped
    // with fused multiply-adds lands a hair off zero, and DYN at zero must
    // mean the side chain is ignored.
    s.lowDynDb    = std::round (lowDyn->get()  * 10.0) / 10.0;
    s.highDynDb   = std::round (highDyn->get() * 10.0) / 10.0;
    return s;
}

bool HingeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void HingeProcessor::prepareToPlay (double sampleRate, int)
{
    controlInterval = controlIntervalFor (sampleRate);

    for (auto& channel : channels)
        channel.prepare (sampleRate, controlInterval);

    setLatencySamples (0);

    // Start at the settings rather than gliding to them from the defaults, so
    // an offline render is what the panel says from the first sample.
    snapped = false;
    untilControl = 0;
}

void HingeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    const auto numSamples  = buffer.getNumSamples();

    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels == 0 || numSamples == 0)
        return;

    const auto linked   = linkParam->get();
    const auto midSide  = midSideParam->get() && numChannels == 2;
    const auto bypassed = bypassParameter->get();

    // Linked, strip A drives both channels and B is ignored until unlinked.
    for (int ch = 0; ch < numChannels; ++ch)
        channels[(size_t) ch].setTargets (strips[(size_t) (linked ? 0 : ch)].read (bypassed));

    if (! snapped)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            channels[(size_t) ch].snapToTargets();

        snapped = true;
        untilControl = controlInterval;
    }

    // A linked stereo pair listens with one ear, so the dynamics cannot pull
    // the image to one side. Mid and side are different signals and do not.
    const auto shareSideChain = linked && ! midSide && numChannels == 2;

    if (midSide)
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto m = 0.5f * (left[i] + right[i]);
            const auto s = 0.5f * (left[i] - right[i]);
            left[i]  = m;
            right[i] = s;
        }
    }

    std::array<float, 2> lowMoved {}, highMoved {};

    for (int start = 0; start < numSamples;)
    {
        if (untilControl == 0)
        {
            std::array<BandLevels, 2> side { channels[0].levels(), channels[1].levels() };

            if (shareSideChain)
                side[0] = side[1] = BandLevels::louder (side[0], side[1]);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& channel = channels[(size_t) ch];
                channel.control (side[(size_t) ch]);
                lowMoved [(size_t) ch] = std::max (lowMoved [(size_t) ch], std::abs (channel.getLowMovement()));
                highMoved[(size_t) ch] = std::max (highMoved[(size_t) ch], std::abs (channel.getHighMovement()));
            }

            untilControl = controlInterval;
        }

        const auto count = juce::jmin (numSamples - start, untilControl);

        for (int ch = 0; ch < numChannels; ++ch)
            channels[(size_t) ch].process (buffer.getWritePointer (ch) + start, count);

        start += count;
        untilControl -= count;
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& meter = meters.channels[(size_t) ch];
        MeterSource::raise (meter.outputPeak, buffer.getMagnitude (ch, 0, numSamples));
        MeterSource::raise (meter.shelfMovement[0], lowMoved [(size_t) ch]);
        MeterSource::raise (meter.shelfMovement[1], highMoved[(size_t) ch]);
    }

    if (midSide)
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto m = left[i];
            const auto s = right[i];
            left[i]  = m + s;
            right[i] = m - s;
        }
    }
}

// ---------------------------------------------------------------------------

void HingeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HingeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* HingeProcessor::createEditor()
{
    return new HingeEditor (*this);
}

} // namespace hinge

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hinge::HingeProcessor();
}
