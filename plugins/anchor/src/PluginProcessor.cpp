#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace anchor
{

namespace
{
    juce::String thresholdString (float value, int)
    {
        return (value > 0.05f ? "+" : "") + juce::String (value, 1) + " dBu";
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

    juce::String percentString (float value, int)
    {
        return juce::String (value, 1) + " %";
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AnchorProcessor::createParameterLayout()
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

    const juce::NormalisableRange<float> trimRange (-range::trimDb, range::trimDb, 0.25f);

    for (const auto isSide : { false, true })
    {
        const auto id   = [isSide] (const char* name) { return juce::ParameterID { isSide ? pid::side (name) : juce::String (name), 1 }; };
        const auto name = [isSide] (const char* text) { return juce::String (isSide ? "Side " : "") + text; };

        layout.add (std::make_unique<FloatP> (id (pid::input), name ("Input"), trimRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimStepString)));
        layout.add (std::make_unique<FloatP> (id (pid::threshold), name ("Threshold"),
                                              juce::NormalisableRange<float> (range::thresholdMin, range::thresholdMax, 0.1f),
                                              4.0f,
                                              Attr().withLabel ("dBu").withStringFromValueFunction (thresholdString)));
        layout.add (std::make_unique<ChoiceP> (id (pid::ratio), name ("Ratio"),
                                               juce::StringArray { "1.5:1", "2:1", "4:1", "10:1", "INF" }, 2));
        layout.add (std::make_unique<ChoiceP> (id (pid::attack), name ("Attack"),
                                               juce::StringArray { "0.1 ms", "0.3 ms", "1 ms", "3 ms", "10 ms", "30 ms" }, 4));
        layout.add (std::make_unique<ChoiceP> (id (pid::release), name ("Release"),
                                               juce::StringArray { "0.1 s", "0.3 s", "0.6 s", "0.9 s", "1.2 s", "Auto" }, vca::autoRelease));
        layout.add (std::make_unique<BoolP>   (id (pid::feedback), name ("Feedback"), false));
        layout.add (std::make_unique<ChoiceP> (id (pid::scFilter), name ("SC Filter"),
                                               juce::StringArray { "Off", "60 Hz", "90 Hz", "150 Hz", "T1", "T2" }, 0));
        layout.add (std::make_unique<BoolP>   (id (pid::listen), name ("SC Listen"), false));
        layout.add (std::make_unique<FloatP>  (id (pid::thd), name ("THD"),
                                               juce::NormalisableRange<float> (0.0f, range::thdMax, 0.01f, 0.6f), 0.0f,
                                               Attr().withLabel ("%").withStringFromValueFunction (percentString)));
        layout.add (std::make_unique<FloatP>  (id (pid::mix), name ("Mix"),
                                               juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                               Attr().withLabel ("%").withStringFromValueFunction (percentString)));
        layout.add (std::make_unique<FloatP>  (id (pid::output), name ("Output"), trimRange, 0.0f,
                                               Attr().withLabel ("dB").withStringFromValueFunction (trimStepString)));
        layout.add (std::make_unique<BoolP>   (id (pid::dynLink), name ("Dynamic Link"), false));
    }

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::external, 1 }, "External Sidechain", false));

    layout.add (std::make_unique<BoolP>  (juce::ParameterID { pid::iron, 1 }, "Iron", false));
    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::ironPad, 1 }, "Iron Pad",
                                          juce::NormalisableRange<float> (0.0f, range::ironPadMax, 1.0f), 0.0f,
                                          Attr().withLabel ("dB").withStringFromValueFunction (padString)));
    layout.add (std::make_unique<BoolP>  (juce::ParameterID { pid::ironLink, 1 }, "Iron Link", false));

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

AnchorProcessor::AnchorProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                                .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "ANCHOR", createParameterLayout())
{
    for (const auto isSide : { false, true })
    {
        auto& s = isSide ? sideParams : params;
        const auto id = [isSide] (const char* name) { return isSide ? pid::side (name) : juce::String (name); };

        s.input     = findParameter<juce::AudioParameterFloat>  (id (pid::input));
        s.threshold = findParameter<juce::AudioParameterFloat>  (id (pid::threshold));
        s.ratio     = findParameter<juce::AudioParameterChoice> (id (pid::ratio));
        s.attack    = findParameter<juce::AudioParameterChoice> (id (pid::attack));
        s.release   = findParameter<juce::AudioParameterChoice> (id (pid::release));
        s.feedback  = findParameter<juce::AudioParameterBool>   (id (pid::feedback));
        s.scFilter  = findParameter<juce::AudioParameterChoice> (id (pid::scFilter));
        s.listen    = findParameter<juce::AudioParameterBool>   (id (pid::listen));
        s.thd       = findParameter<juce::AudioParameterFloat>  (id (pid::thd));
        s.mix       = findParameter<juce::AudioParameterFloat>  (id (pid::mix));
        s.output    = findParameter<juce::AudioParameterFloat>  (id (pid::output));
    }

    externalParam   = findParameter<juce::AudioParameterBool>   (pid::external);
    ironParam       = findParameter<juce::AudioParameterBool>   (pid::iron);
    ironPadParam    = findParameter<juce::AudioParameterFloat>  (pid::ironPad);
    modeParam       = findParameter<juce::AudioParameterChoice> (pid::mode);
    dualSplitParam  = findParameter<juce::AudioParameterBool>   (pid::dualSplit);
    bypassParameter = findParameter<juce::AudioParameterBool>   (pid::bypass);

    (void) tube::ThdCalibration::get();
}

bool AnchorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto& key = layouts.getChannelSet (true, 1);
        return key.isDisabled() || key == juce::AudioChannelSet::mono() || key == juce::AudioChannelSet::stereo();
    }

    return true;
}

// ---------------------------------------------------------------------------

void AnchorProcessor::ChannelPath::prepare (double innerRate, double hostRate)
{
    thdStage.prepare (innerRate);
    sidechain.prepare (innerRate);
    ballistics.prepare (innerRate);
    cell.prepare (innerRate);
    outputStage.prepare (innerRate);

    inputGain  .reset (innerRate, 0.02);
    outputGain .reset (innerRate, 0.02);
    thresholdDb.reset (innerRate, 0.02);
    thdDrive   .reset (innerRate, 0.02);
    mix        .reset (innerRate, 0.02);
    engaged    .reset (hostRate,  0.02);

    // The feedback loop rings if the reduction moves most of the way to its
    // target within one sample; at every rate this keeps it well damped.
    maxFeedbackCoeff = 0.08f;

    reset();
}

void AnchorProcessor::ChannelPath::reset()
{
    thdStage.reset();
    sidechain.reset();
    ballistics.reset();
    cell.reset();
    outputStage.reset();
    lastWetOutput = 0.0f;
}

void AnchorProcessor::ChannelPath::setTargets (const Parameters& p, bool ironOn, float ironPadDb, bool engagedNow)
{
    inputGain  .setTargetValue (juce::Decibels::decibelsToGain (p.input ->get()));
    outputGain .setTargetValue (juce::Decibels::decibelsToGain (p.output->get()));
    thresholdDb.setTargetValue (range::thresholdDbfs (p.threshold->get()));
    thdDrive   .setTargetValue (tube::ThdCalibration::get().driveFor (p.thd->get()));
    mix        .setTargetValue (p.mix->get() / 100.0f);
    engaged    .setTargetValue (engagedNow ? 1.0f : 0.0f);

    const auto releaseIndex = p.release->getIndex();
    const auto isAuto = releaseIndex == vca::autoRelease;
    ballistics.setTimes (vca::attacks[(size_t) p.attack->getIndex()] / 1000.0f,
                         isAuto ? vca::releases[0] : vca::releases[(size_t) releaseIndex], isAuto);

    feedback  = p.feedback->get();
    if (feedback)
        ballistics.limitAttack (maxFeedbackCoeff);

    slope     = vca::slopeFor (p.ratio->getIndex());
    listening = p.listen->get();
    sidechain.setPosition (p.scFilter->getIndex());
    outputStage.setTargets (ironOn, ironPadDb);
}

void AnchorProcessor::ChannelPath::snapToTargets()
{
    for (auto* v : { &inputGain, &outputGain })
        v->setCurrentAndTargetValue (v->getTargetValue());

    for (auto* v : { &thresholdDb, &thdDrive, &mix, &engaged })
        v->setCurrentAndTargetValue (v->getTargetValue());

    outputStage.snapToTargets();
}

void AnchorProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    const auto block = (size_t) juce::jmax (1, maximumExpectedSamplesPerBlock);

    for (auto* os : { &oversampler, &sidechainOversampler })
    {
        *os = std::make_unique<juce::dsp::Oversampling<float>> (
            2, oversamplingStages, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        (*os)->initProcessing (block);
        (*os)->reset();
    }

    const auto latency = juce::roundToInt (oversampler->getLatencyInSamples());
    setLatencySamples (latency);

    dryDelay.prepare ({ sampleRate, (juce::uint32) block, 2 });
    dryDelay.setMaximumDelayInSamples (latency + 1);
    dryDelay.setDelay ((float) latency);
    dryDelay.reset();

    dryBuffer.setSize (2, (int) block);
    keyBuffer.setSize (2, (int) block);

    const auto innerRate = sampleRate * (double) (1 << oversamplingStages);

    for (auto& path : paths)
        path.prepare (innerRate, sampleRate);

    pathsSnapped = false;
}

void AnchorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainIn = getBusBuffer (buffer, true, 0);
    const auto numChannels = juce::jmin (2, mainIn.getNumChannels(), getMainBusNumOutputChannels());
    const auto numSamples  = buffer.getNumSamples();

    if (numChannels == 0 || numSamples == 0 || oversampler == nullptr)
        return;

    // The key: the sidechain input, when the host feeds one and EXT is in.
    auto keyChannels = 0;
    if (getBusCount (true) > 1 && getBus (true, 1) != nullptr && getBus (true, 1)->isEnabled())
    {
        auto key = getBusBuffer (buffer, true, 1);
        keyChannels = juce::jmin (2, key.getNumChannels());
        keyBuffer.setSize (2, numSamples, false, false, true);

        for (int ch = 0; ch < 2; ++ch)
            keyBuffer.copyFrom (ch, 0, key, keyChannels == 0 ? 0 : juce::jmin (ch, keyChannels - 1), 0, numSamples);
    }

    externalConnected.store (keyChannels > 0, std::memory_order_relaxed);
    const auto useKey = keyChannels > 0 && externalParam->get();

    // Anything past the main pair is not ours to output.
    for (auto ch = numChannels; ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto mode     = static_cast<ChannelMode> (modeParam->getIndex());
    const auto bypassed = bypassParameter->get();
    const auto midSide  = mode == ChannelMode::midSide && numChannels == 2;
    const auto linked   = mode == ChannelMode::stereo && numChannels == 2;
    const auto ownSecondSet = midSide || (mode == ChannelMode::dualMono && numChannels == 2 && dualSplitParam->get());

    for (size_t ch = 0; ch < paths.size(); ++ch)
        paths[ch].setTargets (ownSecondSet && ch == 1 ? sideParams : params,
                              ironParam->get(), ironPadParam->get(), ! bypassed);

    if (! pathsSnapped)
    {
        for (auto& path : paths)
            path.snapToTargets();

        pathsSnapped = true;
    }

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

    const float* keyData[2] { nullptr, nullptr };
    if (useKey)
    {
        auto keyBlock = juce::dsp::AudioBlock<float> (keyBuffer).getSubsetChannelBlock (0, 2).getSubBlock (0, (size_t) numSamples);
        auto keyInner = sidechainOversampler->processSamplesUp (keyBlock);
        keyData[0] = keyInner.getChannelPointer (0);
        keyData[1] = keyInner.getChannelPointer (1);
    }

    std::array<float*, 2> data { inner.getChannelPointer (0),
                                 numChannels > 1 ? inner.getChannelPointer (1) : nullptr };

    std::array<float, 2> reductionPeak {}, drivePeak {};

    for (size_t i = 0; i < inner.getNumSamples(); ++i)
    {
        std::array<float, 2> raw { data[0][i], numChannels > 1 ? data[1][i] : 0.0f };
        std::array<float, 2> key { raw };

        if (useKey)
            key = { keyData[0][i], numChannels > 1 ? keyData[1][i] : keyData[0][i] };

        if (midSide)
        {
            raw = { 0.5f * (raw[0] + raw[1]), 0.5f * (raw[0] - raw[1]) };
            key = { 0.5f * (key[0] + key[1]), 0.5f * (key[0] - key[1]) };
        }

        // Input trim; the dry half of the mix is taken here, before THD.
        std::array<float, 2> dryIn {}, x {}, sc {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& path = paths[(size_t) ch];
            const auto c = (size_t) ch;

            const auto gain = path.inputGain.getNextValue();
            dryIn[c] = raw[c] * gain;

            const auto drive = path.thdDrive.getNextValue();
            drivePeak[c] = juce::jmax (drivePeak[c], std::abs (dryIn[c]) * drive);
            x[c] = path.thdStage.process (dryIn[c], drive);

            // Feed-forward listens to what is coming into the cell (or the
            // key, trimmed with the input so the threshold means the same);
            // feedback to what has come out of it.
            const auto source = path.feedback && ! useKey ? path.lastWetOutput
                                                         : (useKey ? key[c] * gain : x[c]);
            sc[c] = path.sidechain.process (source);
        }

        std::array<float, 2> target {};

        const auto targetFor = [] (ChannelPath& path, float level, float thresholdDb, bool fromOutput)
        {
            const auto overDb = juce::Decibels::gainToDecibels (level, -120.0f) - thresholdDb;
            return fromOutput ? vca::feedbackReductionFor (overDb, path.slope)
                              : vca::reductionFor (overDb, path.slope);
        };

        if (linked)
        {
            auto& lead = paths[0];
            const auto t = lead.thresholdDb.getNextValue();
            paths[1].thresholdDb.getNextValue();
            const auto level = juce::jmax (std::abs (sc[0]), std::abs (sc[1]));
            target[0] = target[1] = targetFor (lead, level, t, lead.feedback && ! useKey);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& path = paths[(size_t) ch];
                const auto t = path.thresholdDb.getNextValue();
                target[(size_t) ch] = targetFor (path, std::abs (sc[(size_t) ch]), t, path.feedback && ! useKey);
            }
        }

        std::array<float, 2> reduction {};

        if (linked)
        {
            reduction[0] = reduction[1] = paths[0].ballistics.process (target[0]);
            paths[1].ballistics.process (target[1]);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
                reduction[(size_t) ch] = paths[(size_t) ch].ballistics.process (target[(size_t) ch]);
        }

        std::array<float, 2> out {};
        auto listening = false;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& path = paths[(size_t) ch];
            const auto c = (size_t) ch;

            const auto y = path.cell.process (x[c], reduction[c]);
            path.lastWetOutput = y;

            const auto wet = path.mix.getNextValue();
            out[c] = (dryIn[c] + wet * (y - dryIn[c])) * path.outputGain.getNextValue();

            if (path.listening)
            {
                out[c] = sc[c];
                listening = true;
            }

            reductionPeak[c] = juce::jmax (reductionPeak[c], reduction[c]);
        }

        // The output stage works on left and right.
        if (midSide)
            out = { out[0] + out[1], out[0] - out[1] };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto shaped = paths[(size_t) ch].outputStage.process (out[(size_t) ch]);
            data[(size_t) ch][i] = listening ? out[(size_t) ch] : shaped;
        }
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& meter = meters.channels[(size_t) ch];
        MeterSource::raise (meter.reductionDb, reductionPeak[(size_t) ch]);
        MeterSource::raise (meter.thdDrive, drivePeak[(size_t) ch]);
    }

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

void AnchorProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AnchorProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* AnchorProcessor::createEditor()
{
    return new AnchorEditor (*this);
}

} // namespace anchor

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new anchor::AnchorProcessor();
}
