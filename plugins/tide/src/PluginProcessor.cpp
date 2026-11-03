#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace tide
{

namespace
{
    template <size_t N>
    juce::StringArray names (const std::array<const char*, N>& table)
    {
        juce::StringArray out;

        for (auto* name : table)
            out.add (name);

        return out;
    }

    constexpr int chunkSize = 256;
}

juce::AudioProcessorValueTreeState::ParameterLayout TideProcessor::createParameterLayout()
{
    using Layout  = juce::AudioProcessorValueTreeState::ParameterLayout;
    using ChoiceP = juce::AudioParameterChoice;
    using BoolP   = juce::AudioParameterBool;

    Layout layout;

    layout.add (std::make_unique<BoolP>   (juce::ParameterID { pid::eqIn, 1 },    "EQ In",    true));
    layout.add (std::make_unique<BoolP>   (juce::ParameterID { pid::midSide, 1 }, "Mid/Side", false));
    layout.add (std::make_unique<ChoiceP> (juce::ParameterID { pid::cue, 1 },     "Cue",
                                           juce::StringArray { "L/M", "Off", "R/S" }, (int) Cue::off));
    layout.add (std::make_unique<BoolP>   (juce::ParameterID { pid::link, 1 },    "Link",     false));

    const auto gains = names (bands::gainNames);

    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        const auto id   = [strip] (const char* name) { return juce::ParameterID { pid::forStrip (strip, name), 1 }; };
        const auto name = [strip] (const char* text) { return juce::String (strip == 0 ? "L/M " : "R/S ") + text; };

        layout.add (std::make_unique<ChoiceP> (id (pid::lowFreq),   name ("Low Freq"),   names (bands::lowNames),  2));
        layout.add (std::make_unique<BoolP>   (id (pid::lowShelf),  name ("Low Shelf"),  true));
        layout.add (std::make_unique<ChoiceP> (id (pid::lowGain),   name ("Low Gain"),   gains, bands::centreStep));
        layout.add (std::make_unique<ChoiceP> (id (pid::midFreq),   name ("Mid Freq"),   names (bands::midNames),  2));
        layout.add (std::make_unique<ChoiceP> (id (pid::midGain),   name ("Mid Gain"),   gains, bands::centreStep));
        layout.add (std::make_unique<ChoiceP> (id (pid::highFreq),  name ("High Freq"),  names (bands::highNames), 2));
        layout.add (std::make_unique<BoolP>   (id (pid::highShelf), name ("High Shelf"), true));
        layout.add (std::make_unique<ChoiceP> (id (pid::highGain),  name ("High Gain"),  gains, bands::centreStep));
        layout.add (std::make_unique<BoolP>   (id (pid::hpfIn),     name ("HPF In"),     false));
        layout.add (std::make_unique<BoolP>   (id (pid::hpfTimes5), name ("HPF x5"),     false));
        layout.add (std::make_unique<ChoiceP> (id (pid::hpfFreq),   name ("HPF Freq"),   names (bands::hpfNames),  0));
        layout.add (std::make_unique<ChoiceP> (id (pid::level),     name ("Level"),      gains, bands::centreStep));
    }

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

TideProcessor::TideProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "TIDE", createParameterLayout())
{
    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        auto& s = strips[(size_t) strip];
        const auto id = [strip] (const char* name) { return pid::forStrip (strip, name); };

        s.lowFreq   = findParameter<juce::AudioParameterChoice> (id (pid::lowFreq));
        s.lowShelf  = findParameter<juce::AudioParameterBool>   (id (pid::lowShelf));
        s.lowGain   = findParameter<juce::AudioParameterChoice> (id (pid::lowGain));
        s.midFreq   = findParameter<juce::AudioParameterChoice> (id (pid::midFreq));
        s.midGain   = findParameter<juce::AudioParameterChoice> (id (pid::midGain));
        s.highFreq  = findParameter<juce::AudioParameterChoice> (id (pid::highFreq));
        s.highShelf = findParameter<juce::AudioParameterBool>   (id (pid::highShelf));
        s.highGain  = findParameter<juce::AudioParameterChoice> (id (pid::highGain));
        s.hpfIn     = findParameter<juce::AudioParameterBool>   (id (pid::hpfIn));
        s.hpfTimes5 = findParameter<juce::AudioParameterBool>   (id (pid::hpfTimes5));
        s.hpfFreq   = findParameter<juce::AudioParameterChoice> (id (pid::hpfFreq));
        s.level     = findParameter<juce::AudioParameterChoice> (id (pid::level));
    }

    eqInParam       = findParameter<juce::AudioParameterBool>   (pid::eqIn);
    midSideParam    = findParameter<juce::AudioParameterBool>   (pid::midSide);
    cueParam        = findParameter<juce::AudioParameterChoice> (pid::cue);
    linkParam       = findParameter<juce::AudioParameterBool>   (pid::link);
    bypassParameter = findParameter<juce::AudioParameterBool>   (pid::bypass);
}

ChannelSettings TideProcessor::StripParameters::read() const
{
    ChannelSettings s;
    s.lowFreq   = lowFreq->getIndex();
    s.lowShelf  = lowShelf->get();
    s.lowGain   = lowGain->getIndex();
    s.midFreq   = midFreq->getIndex();
    s.midGain   = midGain->getIndex();
    s.highFreq  = highFreq->getIndex();
    s.highShelf = highShelf->get();
    s.highGain  = highGain->getIndex();
    s.hpfIn     = hpfIn->get();
    s.hpfTimes5 = hpfTimes5->get();
    s.hpfFreq   = hpfFreq->getIndex();
    s.level     = level->getIndex();
    return s;
}

bool TideProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void TideProcessor::prepareToPlay (double sampleRate, int)
{
    controlInterval = controlIntervalFor (sampleRate);

    for (auto& channel : channels)
        channel.prepare (sampleRate, controlInterval);

    for (auto* v : { &engaged, &cueFirst, &cueSecond })
        v->reset (sampleRate, 0.02);

    setLatencySamples (0);

    // Start at the settings rather than gliding to them from the defaults, so
    // an offline render is what the panel says from the first sample.
    snapped = false;
    untilControl = 0;
}

void TideProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    const auto numSamples  = buffer.getNumSamples();

    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels == 0 || numSamples == 0)
        return;

    const auto linked   = linkParam->get();
    const auto bypassed = bypassParameter->get();
    const auto wantsMs  = midSideParam->get() && numChannels == 2;

    // Linked, the left half of the panel drives both channels and the right
    // half is ignored until unlinked.
    for (int ch = 0; ch < numChannels; ++ch)
        channels[(size_t) ch].setTargets (strips[(size_t) (linked ? 0 : ch)].read());

    const auto cue = bypassed ? Cue::off : (Cue) cueParam->getIndex();
    cueFirst .setTargetValue (cue == Cue::first  ? 1.0 : 0.0);
    cueSecond.setTargetValue (cue == Cue::second ? 1.0 : 0.0);

    if (! snapped)
    {
        runningMidSide = wantsMs;
        engaged.setCurrentAndTargetValue (eqInParam->get() && ! bypassed ? 1.0 : 0.0);
        cueFirst .setCurrentAndTargetValue (cueFirst .getTargetValue());
        cueSecond.setCurrentAndTargetValue (cueSecond.getTargetValue());

        for (int ch = 0; ch < numChannels; ++ch)
            channels[(size_t) ch].snapToTargets();

        snapped = true;
        untilControl = controlInterval;
    }

    std::array<double, chunkSize> left {}, right {};

    for (int start = 0; start < numSamples; start += chunkSize)
    {
        const auto count = juce::jmin (chunkSize, numSamples - start);

        // A change of mode waits for the EQ to have faded out.
        if (runningMidSide != wantsMs && isZero (engaged.getCurrentValue()))
        {
            runningMidSide = wantsMs;

            for (auto& channel : channels)
                channel.reset();
        }

        engaged.setTargetValue (eqInParam->get() && ! bypassed && runningMidSide == wantsMs ? 1.0 : 0.0);

        auto* outL = buffer.getWritePointer (0) + start;
        auto* outR = numChannels > 1 ? buffer.getWritePointer (1) + start : nullptr;

        for (int i = 0; i < count; ++i)
        {
            left[(size_t) i]  = outL[i];
            right[(size_t) i] = outR != nullptr ? outR[i] : 0.0;
        }

        process (left.data(), right.data(), numChannels, count);

        // EQ in/out and the host's bypass: out is the input, sample for
        // sample; the fade only covers the switch.
        for (int i = 0; i < count; ++i)
        {
            const auto w  = engaged.getNextValue();
            auto l = (double) outL[i] + w * (left[(size_t) i] - (double) outL[i]);
            auto r = outR != nullptr ? (double) outR[i] + w * (right[(size_t) i] - (double) outR[i]) : l;

            // Cue: one side of the EQ on both outputs. Left or right, or mid
            // or side when the switch is in M/S.
            const auto first  = cueFirst .getNextValue();
            const auto second = cueSecond.getNextValue();

            if (first > 0.0 || second > 0.0)
            {
                const auto a = wantsMs ? 0.5 * (l + r) : l;
                const auto b = wantsMs ? 0.5 * (l - r) : r;
                const auto keep = 1.0 - first - second;

                l = keep * l + first * a + second * b;
                r = keep * r + first * a + second * b;
            }

            outL[i] = (float) l;

            if (outR != nullptr)
                outR[i] = (float) r;
        }
    }
}

void TideProcessor::process (double* left, double* right, int numChannels, int numSamples)
{
    const auto midSide = runningMidSide && numChannels == 2;

    if (midSide)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto m = 0.5 * (left[i] + right[i]);
            const auto s = 0.5 * (left[i] - right[i]);
            left[i]  = m;
            right[i] = s;
        }
    }

    for (int start = 0; start < numSamples;)
    {
        if (untilControl == 0)
        {
            for (int ch = 0; ch < numChannels; ++ch)
                channels[(size_t) ch].control();

            untilControl = controlInterval;
        }

        const auto count = juce::jmin (numSamples - start, untilControl);

        channels[0].process (left + start, count);

        if (numChannels > 1)
            channels[1].process (right + start, count);

        start += count;
        untilControl -= count;
    }

    if (midSide)
    {
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

void TideProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void TideProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* TideProcessor::createEditor()
{
    return new TideEditor (*this);
}

} // namespace tide

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new tide::TideProcessor();
}
