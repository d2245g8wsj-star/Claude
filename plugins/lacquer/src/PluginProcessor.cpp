#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace lacquer
{

namespace
{
    juce::String hzString (float hz)
    {
        if (hz >= 1000.0f)
        {
            const auto k = hz / 1000.0f;
            return juce::String (k, std::abs (k - std::round (k)) < 0.01f ? 0 : 1) + " kHz";
        }

        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    template <size_t N>
    juce::StringArray hzChoices (const std::array<float, N>& table)
    {
        juce::StringArray names;

        for (auto hz : table)
            names.add (hzString (hz));

        return names;
    }

    juce::StringArray midCutChoices()
    {
        auto names = hzChoices (voicing::midCutHz);
        names.set (0, "Out");
        return names;
    }

    juce::StringArray trebleBoostChoices()
    {
        juce::StringArray names;

        for (int i = 0; i < (int) voicing::trebleBoostHz.size(); ++i)
        {
            auto name = hzString (voicing::trebleBoostHz[(size_t) i])
                      + (voicing::trebleIsBell (i) ? " bell" : " shelf");

            if (i == 3) name += " (low L)";
            if (i == 4) name += " (high L)";

            names.add (name);
        }

        return names;
    }

    /** "6.5 / +4.6 dB": the switch position, as on the panel, then what it does. */
    auto amountString (float maxDb)
    {
        return [maxDb] (int step, int)
        {
            const auto db = voicing::amountDb (step, maxDb);
            const auto position = juce::String ((float) step * 10.0f / (float) voicing::amountSteps, 1);

            if (step == 0)
                return position + " / 0 dB";

            return position + " / " + (db > 0.0f ? "+" : "") + juce::String (db, 1) + " dB";
        };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout LacquerProcessor::createParameterLayout()
{
    using Layout  = juce::AudioProcessorValueTreeState::ParameterLayout;
    using ChoiceP = juce::AudioParameterChoice;
    using IntP    = juce::AudioParameterInt;
    using BoolP   = juce::AudioParameterBool;
    using IntAttr = juce::AudioParameterIntAttributes;

    Layout layout;

    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        const auto id   = [strip] (const char* name) { return juce::ParameterID { pid::forStrip (strip, name), 1 }; };
        const auto name = [strip] (const char* text) { return juce::String (strip == 0 ? "1 " : "2 ") + text; };

        const auto amount = [&] (const char* paramId, const char* text, float maxDb)
        {
            layout.add (std::make_unique<IntP> (id (paramId), name (text), 0, voicing::amountSteps, 0,
                                                IntAttr().withStringFromValueFunction (amountString (maxDb))));
        };

        const voicing::Settings defaults;

        layout.add (std::make_unique<ChoiceP> (id (pid::lowBoostFreq), name ("Bass Boost Freq"),
                                               hzChoices (voicing::lowBoostHz), defaults.lowBoostFreq));
        amount (pid::lowBoostAmount, "Bass Boost", voicing::lowBoostMaxDb);

        layout.add (std::make_unique<ChoiceP> (id (pid::lowCutFreq), name ("Bass Cut Freq"),
                                               hzChoices (voicing::lowCutHz), defaults.lowCutFreq));
        amount (pid::lowCutAmount, "Bass Cut", voicing::lowCutMaxDb);

        layout.add (std::make_unique<ChoiceP> (id (pid::midBoostFreq), name ("Mid Boost Freq"),
                                               hzChoices (voicing::midBoostHz), defaults.midBoostFreq));
        amount (pid::midBoostAmount, "Mid Boost", voicing::midBoostMaxDb);

        layout.add (std::make_unique<ChoiceP> (id (pid::midCutFreq), name ("Mid Cut Freq"),
                                               midCutChoices(), defaults.midCutFreq));
        amount (pid::midCutAmount, "Mid Cut", voicing::midCutMaxDb);

        layout.add (std::make_unique<ChoiceP> (id (pid::trebleBoostFreq), name ("Treble Boost Freq"),
                                               trebleBoostChoices(), defaults.trebleBoostFreq));
        layout.add (std::make_unique<IntP> (id (pid::trebleQ), name ("Treble Q"), 0, voicing::qSteps, defaults.trebleQ,
                                            IntAttr().withStringFromValueFunction ([] (int step, int)
                                            {
                                                return juce::String (step) + (step <= 3 ? " (wide / soft)"
                                                                            : step >= 7 ? " (narrow / hard)" : "");
                                            })));
        amount (pid::trebleBoostAmount, "Treble Boost", voicing::trebleBoostMaxDb);

        layout.add (std::make_unique<ChoiceP> (id (pid::trebleCutFreq), name ("Treble Cut Freq"),
                                               hzChoices (voicing::trebleCutHz), defaults.trebleCutFreq));
        amount (pid::trebleCutAmount, "Treble Cut", voicing::trebleCutMaxDb);

        layout.add (std::make_unique<BoolP> (id (pid::shift),       name ("Shift"),              false));
        layout.add (std::make_unique<BoolP> (id (pid::buffer),      name ("Input Buffer"),       true));
        layout.add (std::make_unique<BoolP> (id (pid::transformer), name ("Output Transformer"), true));
        layout.add (std::make_unique<BoolP> (id (pid::eqIn),        name ("EQ In"),              true));
    }

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::link, 1 }, "Link", true));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pid::output, 1 }, "Output",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB").withStringFromValueFunction ([] (float v, int)
        {
            return std::abs (v) < 0.05f ? juce::String ("0.0 dB")
                                        : (v > 0.0f ? "+" : "") + juce::String (v, 1) + " dB";
        })));
    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

LacquerProcessor::LacquerProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LACQUER", createParameterLayout())
{
    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        auto& s = strips[(size_t) strip];
        const auto id = [strip] (const char* name) { return pid::forStrip (strip, name); };

        s.lowBoostFreq      = findParameter<juce::AudioParameterChoice> (id (pid::lowBoostFreq));
        s.lowBoostAmount    = findParameter<juce::AudioParameterInt>    (id (pid::lowBoostAmount));
        s.lowCutFreq        = findParameter<juce::AudioParameterChoice> (id (pid::lowCutFreq));
        s.lowCutAmount      = findParameter<juce::AudioParameterInt>    (id (pid::lowCutAmount));
        s.midBoostFreq      = findParameter<juce::AudioParameterChoice> (id (pid::midBoostFreq));
        s.midBoostAmount    = findParameter<juce::AudioParameterInt>    (id (pid::midBoostAmount));
        s.midCutFreq        = findParameter<juce::AudioParameterChoice> (id (pid::midCutFreq));
        s.midCutAmount      = findParameter<juce::AudioParameterInt>    (id (pid::midCutAmount));
        s.trebleBoostFreq   = findParameter<juce::AudioParameterChoice> (id (pid::trebleBoostFreq));
        s.trebleQ           = findParameter<juce::AudioParameterInt>    (id (pid::trebleQ));
        s.trebleBoostAmount = findParameter<juce::AudioParameterInt>    (id (pid::trebleBoostAmount));
        s.trebleCutFreq     = findParameter<juce::AudioParameterChoice> (id (pid::trebleCutFreq));
        s.trebleCutAmount   = findParameter<juce::AudioParameterInt>    (id (pid::trebleCutAmount));
        s.shift             = findParameter<juce::AudioParameterBool>   (id (pid::shift));
        s.buffer            = findParameter<juce::AudioParameterBool>   (id (pid::buffer));
        s.transformer       = findParameter<juce::AudioParameterBool>   (id (pid::transformer));
        s.eqIn              = findParameter<juce::AudioParameterBool>   (id (pid::eqIn));
    }

    linkParam       = findParameter<juce::AudioParameterBool>  (pid::link);
    outputParam     = findParameter<juce::AudioParameterFloat> (pid::output);
    bypassParameter = findParameter<juce::AudioParameterBool>  (pid::bypass);
}

bool LacquerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

voicing::Settings LacquerProcessor::StripParameters::settings() const
{
    voicing::Settings s;
    s.lowBoostFreq      = lowBoostFreq->getIndex();
    s.lowBoostAmount    = lowBoostAmount->get();
    s.lowCutFreq        = lowCutFreq->getIndex();
    s.lowCutAmount      = lowCutAmount->get();
    s.midBoostFreq      = midBoostFreq->getIndex();
    s.midBoostAmount    = midBoostAmount->get();
    s.midCutFreq        = midCutFreq->getIndex();
    s.midCutAmount      = midCutAmount->get();
    s.trebleBoostFreq   = trebleBoostFreq->getIndex();
    s.trebleQ           = trebleQ->get();
    s.trebleBoostAmount = trebleBoostAmount->get();
    s.trebleCutFreq     = trebleCutFreq->getIndex();
    s.trebleCutAmount   = trebleCutAmount->get();
    s.shift             = shift->get();
    return s;
}

// ---------------------------------------------------------------------------

LacquerProcessor::ChannelPath::ChannelPath()
{
    sections.reserve (voicing::numSections);

    for (auto type : voicing::sectionTypes)
        sections.emplace_back (type);
}

void LacquerProcessor::ChannelPath::prepare (double innerRate)
{
    for (auto& band : sections)
        band.prepare (innerRate);

    input      .prepare (innerRate);
    amp        .prepare (innerRate);
    transformer.prepare (innerRate);

    for (auto* fade : { &bufferIn, &transformerIn, &eqIn })
        fade->reset (innerRate, 0.02);
}

void LacquerProcessor::ChannelPath::reset()
{
    for (auto& band : sections)
        band.reset();

    input.reset();
    amp.reset();
    transformer.reset();
}

void LacquerProcessor::ChannelPath::setTargets (const StripParameters& p)
{
    const auto targets = voicing::targets (p.settings());

    for (size_t i = 0; i < sections.size(); ++i)
        sections[i].setTargets (targets[i].hz, targets[i].db, targets[i].q);

    bufferIn     .setTargetValue (p.buffer     ->get() ? 1.0f : 0.0f);
    transformerIn.setTargetValue (p.transformer->get() ? 1.0f : 0.0f);
    eqIn         .setTargetValue (p.eqIn       ->get() ? 1.0f : 0.0f);
}

void LacquerProcessor::ChannelPath::snapToTargets()
{
    for (auto& band : sections)
        band.snapToTargets();

    for (auto* fade : { &bufferIn, &transformerIn, &eqIn })
        fade->setCurrentAndTargetValue (fade->getTargetValue());
}

float LacquerProcessor::ChannelPath::process (float x) noexcept
{
    // Both sides of each switch run all the time, so throwing one is a 20 ms
    // crossfade between two settled paths rather than a stage starting cold.
    const auto buffered = input.processBuffered (x);
    const auto direct   = input.processDirect (x);
    x = direct + bufferIn.getNextValue() * (buffered - direct);

    auto equalised = x;

    for (auto& band : sections)
        equalised = band.process (equalised);

    // EQ IN relays the filter network out; the amplifiers stay in circuit.
    x += eqIn.getNextValue() * (equalised - x);

    x = amp.process (x);

    const auto coupled = transformer.process (x);
    return x + transformerIn.getNextValue() * (coupled - x);
}

// ---------------------------------------------------------------------------

void LacquerProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, oversamplingStages,
        juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
        true,    // maximum quality
        true);   // integer latency, so the host can compensate it exactly

    oversampler->initProcessing (static_cast<size_t> (juce::jmax (1, maximumExpectedSamplesPerBlock)));
    oversampler->reset();

    const auto latency = juce::roundToInt (oversampler->getLatencyInSamples());
    setLatencySamples (latency);

    dryDelay.prepare ({ sampleRate, (juce::uint32) juce::jmax (1, maximumExpectedSamplesPerBlock), 2 });
    dryDelay.setMaximumDelayInSamples (latency + 1);
    dryDelay.setDelay ((float) latency);
    dryDelay.reset();

    dryBuffer.setSize (2, juce::jmax (1, maximumExpectedSamplesPerBlock));

    const auto innerRate = sampleRate * (double) (1 << oversamplingStages);

    for (auto& path : paths)
    {
        path.prepare (innerRate);
        path.reset();
    }

    outputGain.reset (sampleRate, 0.02);
    engaged   .reset (sampleRate, 0.02);

    // Start at the settings rather than gliding to them from the defaults, so
    // an offline render is what the panel says from the first sample.
    pathsSnapped = false;
}

void LacquerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());

    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (numChannels == 0 || buffer.getNumSamples() == 0 || oversampler == nullptr)
        return;

    // Linked, channel 1's controls set both channels, so the two sides of a
    // stereo master cannot drift apart.
    const auto linked = linkParam->get();

    for (int ch = 0; ch < numChannels; ++ch)
        paths[(size_t) ch].setTargets (strips[(size_t) (linked ? 0 : ch)]);

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->get()));
    engaged   .setTargetValue (bypassParameter->get() ? 0.0f : 1.0f);

    if (! pathsSnapped)
    {
        for (auto& path : paths)
            path.snapToTargets();

        outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
        engaged   .setCurrentAndTargetValue (engaged   .getTargetValue());
        pathsSnapped = true;
    }

    const auto numSamples = buffer.getNumSamples();
    dryBuffer.setSize (2, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* dry = dryBuffer.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            dryDelay.pushSample (ch, in[i]);
            dry[i] = dryDelay.popSample (ch);
        }
    }

    auto block = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, (size_t) numChannels);
    auto inner = oversampler->processSamplesUp (block);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& path = paths[(size_t) ch];
        auto* data = inner.getChannelPointer ((size_t) ch);

        for (size_t i = 0; i < inner.getNumSamples(); ++i)
            data[i] = path.process (data[i]);
    }

    oversampler->processSamplesDown (block);

    // Output trim and bypass at the host rate, against the latency matched dry
    // signal, so bypass is the input exactly.
    for (int i = 0; i < numSamples; ++i)
    {
        const auto gain = outputGain.getNextValue();
        const auto wet  = engaged.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            const auto dry = dryBuffer.getSample (ch, i);
            data[i] = dry + wet * (data[i] * gain - dry);
        }
    }
}

// ---------------------------------------------------------------------------

void LacquerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LacquerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* LacquerProcessor::createEditor()
{
    return new LacquerEditor (*this);
}

} // namespace lacquer

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lacquer::LacquerProcessor();
}
