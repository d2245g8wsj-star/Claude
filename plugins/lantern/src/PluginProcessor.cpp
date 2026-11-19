#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace lantern
{

namespace
{
    juce::String frequencyText (float value)
    {
        if (value >= 1000.0f)
        {
            const auto k = value / 1000.0f;
            auto text = juce::String (k, std::abs (k - std::round (k)) < 0.01f ? 0 : (k >= 10.0f ? 1 : 2));
            if (text.containsChar ('.'))
                text = text.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return text + " kHz";
        }

        return juce::String (juce::roundToInt (value)) + " Hz";
    }

    juce::String frequencyString (float value, int) { return frequencyText (value); }

    juce::String gainString (float value, int)
    {
        if (std::abs (value) < 0.05f)
            return "0.0 dB";

        return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
    }

    juce::String trimStepString (float value, int)
    {
        if (std::abs (value) < 0.01f)
            return "0.00 dB";

        return (value > 0.0f ? "+" : "") + juce::String (value, 2) + " dB";
    }

    juce::String padString (float value, int)
    {
        const auto steps = juce::roundToInt (value);
        return steps == 0 ? juce::String ("0 dB") : "-" + juce::String (steps) + " dB";
    }

    juce::String thdString (float value, int)
    {
        const auto steps = juce::roundToInt (value);
        return steps == 0 ? juce::String ("0 (out)")
                          : juce::String (steps) + " (" + juce::String (range::thdPercent ((float) steps), 2) + " %)";
    }

    juce::NormalisableRange<float> logRange (float low, float high)
    {
        return { low, high,
                 [] (float start, float end, float proportion) { return start * std::pow (end / start, proportion); },
                 [] (float start, float end, float value)      { return std::log (value / start) / std::log (end / start); },
                 [] (float start, float end, float value)      { return juce::jlimit (start, end, value); } };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout LanternProcessor::createParameterLayout()
{
    using Layout  = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatP  = juce::AudioParameterFloat;
    using BoolP   = juce::AudioParameterBool;
    using ChoiceP = juce::AudioParameterChoice;
    using Attr    = juce::AudioParameterFloatAttributes;

    Layout layout;

    layout.add (std::make_unique<ChoiceP> (juce::ParameterID { pid::mode, 1 }, "Mode",
                                           juce::StringArray { "Dual Mono", "Stereo", "Mid/Side" },
                                           static_cast<int> (ChannelMode::stereo)));
    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::dualSplit, 1 }, "Dual Split", false));

    const std::array<const char*, eq::numBands> bandNames { "LF", "LMF", "HMF", "HF" };

    for (const auto isSide : { false, true })
    {
        const auto id   = [isSide] (const juce::String& name) { return juce::ParameterID { isSide ? pid::side (name) : name, 1 }; };
        const auto name = [isSide] (const juce::String& text) { return juce::String (isSide ? "Side " : "") + text; };

        layout.add (std::make_unique<FloatP> (id (pid::hpf), name ("HPF"), logRange (range::hpfMin, range::hpfMax), 30.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<BoolP>  (id (pid::hpfIn),    name ("HPF In"), false));
        layout.add (std::make_unique<BoolP>  (id (pid::hpfSteep), name ("HPF 24 dB"), false));
        layout.add (std::make_unique<FloatP> (id (pid::lpf), name ("LPF"), logRange (range::lpfMin, range::lpfMax), 20000.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<BoolP>  (id (pid::lpfIn),    name ("LPF In"), false));
        layout.add (std::make_unique<BoolP>  (id (pid::lpfSteep), name ("LPF 24 dB"), false));

        for (int b = 0; b < eq::numBands; ++b)
        {
            juce::StringArray choices;
            for (auto hz : eq::frequencies[(size_t) b])
                choices.add (frequencyText (hz));

            const juce::String band (bandNames[(size_t) b]);
            layout.add (std::make_unique<ChoiceP> (id (pid::band (b, "freq")), name (band + " Frequency"), choices,
                                                   eq::defaultPosition[(size_t) b]));
            layout.add (std::make_unique<FloatP>  (id (pid::band (b, "gain")), name (band + " Gain"),
                                                   juce::NormalisableRange<float> (-eq::wideRangeDb, eq::wideRangeDb, 0.1f), 0.0f,
                                                   Attr().withLabel ("dB").withStringFromValueFunction (gainString)));
            layout.add (std::make_unique<BoolP>   (id (pid::band (b, "in")), name (band + " In"), true));

            if (pid::isOuter (b))
                layout.add (std::make_unique<BoolP> (id (pid::band (b, "bell")), name (band + " Bell"), false));
            else
                layout.add (std::make_unique<BoolP> (id (pid::band (b, "hiq")), name (band + " Hi-Q"), false));
        }

        layout.add (std::make_unique<FloatP> (id (pid::thd), name ("THD"),
                                              juce::NormalisableRange<float> (0.0f, range::thdMax, 1.0f), 0.0f,
                                              Attr().withStringFromValueFunction (thdString)));
        layout.add (std::make_unique<FloatP> (id (pid::output), name ("Output"),
                                              juce::NormalisableRange<float> (-range::trimDb, range::trimDb, 0.25f), 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimStepString)));
    }

    layout.add (std::make_unique<ChoiceP> (juce::ParameterID { pid::gainRange, 1 }, "Range",
                                           juce::StringArray { "5 dB", "15 dB" }, static_cast<int> (GainRange::wide)));
    layout.add (std::make_unique<ChoiceP> (juce::ParameterID { pid::qMode, 1 }, "Q Mode",
                                           juce::StringArray { "Proportional", "Constant" }, static_cast<int> (QMode::proportional)));

    layout.add (std::make_unique<BoolP>  (juce::ParameterID { pid::iron, 1 }, "Iron", false));
    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::ironPad, 1 }, "Iron Pad",
                                          juce::NormalisableRange<float> (0.0f, range::ironPadMax, 1.0f), 0.0f,
                                          Attr().withLabel ("dB").withStringFromValueFunction (padString)));
    layout.add (std::make_unique<BoolP>  (juce::ParameterID { pid::ironLink, 1 }, "Iron Link", false));

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

LanternProcessor::LanternProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "LANTERN", createParameterLayout())
{
    for (const auto isSide : { false, true })
    {
        auto& s = isSide ? sideParams : params;
        const auto id = [isSide] (const juce::String& name) { return isSide ? pid::side (name) : name; };

        s.hpf      = findParameter<juce::AudioParameterFloat> (id (pid::hpf));
        s.hpfIn    = findParameter<juce::AudioParameterBool>  (id (pid::hpfIn));
        s.hpfSteep = findParameter<juce::AudioParameterBool>  (id (pid::hpfSteep));
        s.lpf      = findParameter<juce::AudioParameterFloat> (id (pid::lpf));
        s.lpfIn    = findParameter<juce::AudioParameterBool>  (id (pid::lpfIn));
        s.lpfSteep = findParameter<juce::AudioParameterBool>  (id (pid::lpfSteep));

        for (int b = 0; b < eq::numBands; ++b)
        {
            const auto i = (size_t) b;
            s.freq[i]    = findParameter<juce::AudioParameterChoice> (id (pid::band (b, "freq")));
            s.gain[i]    = findParameter<juce::AudioParameterFloat>  (id (pid::band (b, "gain")));
            s.in[i]      = findParameter<juce::AudioParameterBool>   (id (pid::band (b, "in")));
            s.variant[i] = findParameter<juce::AudioParameterBool>   (id (pid::band (b, pid::isOuter (b) ? "bell" : "hiq")));
        }

        s.thd    = findParameter<juce::AudioParameterFloat> (id (pid::thd));
        s.output = findParameter<juce::AudioParameterFloat> (id (pid::output));
    }

    rangeParam      = findParameter<juce::AudioParameterChoice> (pid::gainRange);
    qModeParam      = findParameter<juce::AudioParameterChoice> (pid::qMode);
    ironParam       = findParameter<juce::AudioParameterBool>   (pid::iron);
    ironPadParam    = findParameter<juce::AudioParameterFloat>  (pid::ironPad);
    modeParam       = findParameter<juce::AudioParameterChoice> (pid::mode);
    dualSplitParam  = findParameter<juce::AudioParameterBool>   (pid::dualSplit);
    bypassParameter = findParameter<juce::AudioParameterBool>   (pid::bypass);

    (void) tube::ThdCalibration::get();
}

bool LanternProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

eq::Settings LanternProcessor::settingsFor (bool second) const
{
    const auto& p = second ? sideParams : params;
    const auto scale = rangeParam->getIndex() == (int) GainRange::moderate ? eq::moderateScale : 1.0f;
    const auto proportional = qModeParam->getIndex() == (int) QMode::proportional;

    eq::Settings s;
    s.highPassHz    = p.hpf->get();
    s.highPassIn    = p.hpfIn->get();
    s.highPassSteep = p.hpfSteep->get();
    s.lowPassHz     = p.lpf->get();
    s.lowPassIn     = p.lpfIn->get();
    s.lowPassSteep  = p.lpfSteep->get();

    for (int b = 0; b < eq::numBands; ++b)
    {
        const auto i = (size_t) b;
        auto& band = s.bands[i];

        band.hz = eq::frequencies[i][(size_t) juce::jlimit (0, eq::positions - 1, p.freq[i]->getIndex())];
        band.db = p.gain[i]->get() * scale;
        band.in = p.in[i]->get();

        const auto shelf = pid::isOuter (b) && ! p.variant[i]->get();

        if (shelf)
        {
            band.shape = b == 0 ? eq::Shape::lowShelf : eq::Shape::highShelf;
            band.q = eq::shelfQ;
        }
        else
        {
            band.shape = eq::Shape::bell;
            const auto nominal = (! pid::isOuter (b) && p.variant[i]->get()) ? eq::highQ : eq::bellQ;
            band.q = proportional ? eq::proportionalQ (nominal, band.db) : nominal;
        }
    }

    return s;
}

// ---------------------------------------------------------------------------

void LanternProcessor::ChannelPath::prepare (double innerRate, double hostRate)
{
    equaliser.prepare (innerRate);
    thdStage.prepare (innerRate);
    outputStage.prepare (innerRate);

    outputGain.reset (innerRate, 0.02);
    thdDrive  .reset (innerRate, 0.02);
    engaged   .reset (hostRate,  0.02);

    reset();
}

void LanternProcessor::ChannelPath::reset()
{
    equaliser.reset();
    thdStage.reset();
    outputStage.reset();
}

void LanternProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const auto block = (size_t) juce::jmax (1, maximumExpectedSamplesPerBlock);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, oversamplingStages, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    oversampler->initProcessing (block);
    oversampler->reset();

    const auto latency = juce::roundToInt (oversampler->getLatencyInSamples());
    setLatencySamples (latency);

    dryDelay.prepare ({ sampleRate, (juce::uint32) block, 2 });
    dryDelay.setMaximumDelayInSamples (latency + 1);
    dryDelay.setDelay ((float) latency);
    dryDelay.reset();

    dryBuffer.setSize (2, (int) block);

    const auto innerRate = sampleRate * (double) (1 << oversamplingStages);

    for (auto& path : paths)
        path.prepare (innerRate, sampleRate);

    pathsSnapped = false;
}

void LanternProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());
    const auto numSamples  = buffer.getNumSamples();

    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numChannels == 0 || numSamples == 0 || oversampler == nullptr)
        return;

    const auto mode     = static_cast<ChannelMode> (modeParam->getIndex());
    const auto bypassed = bypassParameter->get();
    const auto midSide  = mode == ChannelMode::midSide && numChannels == 2;
    const auto ownSecondSet = midSide || (mode == ChannelMode::dualMono && numChannels == 2 && dualSplitParam->get());

    for (size_t ch = 0; ch < paths.size(); ++ch)
    {
        auto& path = paths[ch];
        const auto second = ownSecondSet && ch == 1;
        const auto& p = second ? sideParams : params;

        path.equaliser.setTargets (settingsFor (second));
        path.outputGain.setTargetValue (juce::Decibels::decibelsToGain (p.output->get()));
        path.thdDrive.setTargetValue (tube::ThdCalibration::get().driveFor (range::thdPercent (p.thd->get())));
        path.engaged.setTargetValue (bypassed ? 0.0f : 1.0f);
        path.outputStage.setTargets (ironParam->get(), ironPadParam->get());

        if (! pathsSnapped)
        {
            path.equaliser.snap();
            path.outputGain.setCurrentAndTargetValue (path.outputGain.getTargetValue());
            path.thdDrive.setCurrentAndTargetValue (path.thdDrive.getTargetValue());
            path.engaged.setCurrentAndTargetValue (path.engaged.getTargetValue());
            path.outputStage.snapToTargets();
        }
    }

    pathsSnapped = true;

    dryBuffer.setSize (2, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
        MeterSource::raise (meters.channels[(size_t) ch].inPeak, buffer.getMagnitude (ch, 0, numSamples));

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

    std::array<float*, 2> data { inner.getChannelPointer (0),
                                 numChannels > 1 ? inner.getChannelPointer (1) : nullptr };
    std::array<float, 2> drivePeak {};

    for (size_t i = 0; i < inner.getNumSamples(); ++i)
    {
        std::array<float, 2> x { data[0][i], numChannels > 1 ? data[1][i] : 0.0f };

        if (midSide)
            x = { 0.5f * (x[0] + x[1]), 0.5f * (x[0] - x[1]) };

        // Filters and the passive bands, then the THD stage, which the
        // equaliser's boosts drive, then the output gain.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& path = paths[(size_t) ch];
            const auto c = (size_t) ch;

            const auto y = path.equaliser.process (x[c]);
            const auto drive = path.thdDrive.getNextValue();
            drivePeak[c] = juce::jmax (drivePeak[c], std::abs (y) * drive);
            x[c] = path.thdStage.process (y, drive) * path.outputGain.getNextValue();
        }

        // The output stage works on left and right.
        if (midSide)
            x = { x[0] + x[1], x[0] - x[1] };

        for (int ch = 0; ch < numChannels; ++ch)
            data[(size_t) ch][i] = paths[(size_t) ch].outputStage.process (x[(size_t) ch]);
    }

    for (int ch = 0; ch < numChannels; ++ch)
        MeterSource::raise (meters.channels[(size_t) ch].thdDrive, drivePeak[(size_t) ch]);

    oversampler->processSamplesDown (block);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& path = paths[(size_t) ch];
        auto* out = buffer.getWritePointer (ch);
        const auto* dry = dryBuffer.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto wet = path.engaged.getNextValue();
            out[i] = dry[i] + wet * (out[i] - dry[i]);
        }

        MeterSource::raise (meters.channels[(size_t) ch].outPeak, buffer.getMagnitude (ch, 0, numSamples));
    }
}

// ---------------------------------------------------------------------------

void LanternProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void LanternProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* LanternProcessor::createEditor()
{
    return new LanternEditor (*this);
}

} // namespace lantern

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new lantern::LanternProcessor();
}
