#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace beacon
{

namespace
{
    juce::String thresholdString (float value, int)
    {
        return value > -0.05f ? juce::String ("Off") : juce::String (value, 1) + " dB";
    }

    juce::String attackString (float value, int)
    {
        return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
    }

    juce::String releaseString (float value, int)
    {
        return value < 1.0f ? juce::String (juce::roundToInt (value * 1000.0f)) + " ms"
                            : juce::String (value, 2) + " s";
    }

    juce::String frequencyString (float value, int)
    {
        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz";

        return juce::String (juce::roundToInt (value)) + " Hz";
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

    /** A logarithmic travel, so each doubling gets the same turn of the knob. */
    juce::NormalisableRange<float> logRange (float low, float high)
    {
        return { low, high,
                 [] (float start, float end, float proportion)
                 {
                     return start * std::pow (end / start, proportion);
                 },
                 [] (float start, float end, float value)
                 {
                     return std::log (value / start) / std::log (end / start);
                 },
                 [] (float start, float end, float value)
                 {
                     return juce::jlimit (start, end, value);
                 } };
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout BeaconProcessor::createParameterLayout()
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatP = juce::AudioParameterFloat;
    using BoolP  = juce::AudioParameterBool;
    using Attr   = juce::AudioParameterFloatAttributes;

    Layout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::mode, 1 }, "Mode",
                                                              juce::StringArray { "Dual Mono", "Stereo", "Mid/Side" },
                                                              static_cast<int> (ChannelMode::stereo)));

    {
        const auto id   = [] (const char* name) { return juce::ParameterID { name, 1 }; };
        const auto name = [] (const char* text) { return juce::String (text); };

        // Quarter dB steps, as the encoders on the unit move.
        const juce::NormalisableRange<float> trimRange (-range::trimDb, range::trimDb, 0.25f);

        layout.add (std::make_unique<FloatP> (id (pid::input), name ("Input"), trimRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimStepString)));

        layout.add (std::make_unique<FloatP> (id (pid::threshold), name ("Threshold"),
                                              juce::NormalisableRange<float> (range::thresholdMin, 0.0f, 0.1f),
                                              -20.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (thresholdString)));
        layout.add (std::make_unique<FloatP> (id (pid::attack), name ("Attack"),
                                              logRange (range::attackMin, range::attackMax), 10.0f,
                                              Attr().withLabel ("ms").withStringFromValueFunction (attackString)));
        layout.add (std::make_unique<FloatP> (id (pid::release), name ("Release"),
                                              logRange (range::releaseMin, range::releaseMax), 0.5f,
                                              Attr().withLabel ("s").withStringFromValueFunction (releaseString)));
        layout.add (std::make_unique<BoolP>  (id (pid::feedFwd), name ("Feed Forward"), false));
        layout.add (std::make_unique<BoolP>  (id (pid::limit),   name ("Limit"),        false));

        layout.add (std::make_unique<FloatP> (id (pid::hpf), name ("SC HPF"),
                                              logRange (range::hpfMin, range::hpfMax), 80.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<BoolP>  (id (pid::hpfIn), name ("SC HPF In"), false));
        layout.add (std::make_unique<FloatP> (id (pid::bell), name ("SC High Bell"),
                                              logRange (range::bellMin, range::bellMax), 8000.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<BoolP>  (id (pid::bellIn), name ("SC High Bell In"), false));
        layout.add (std::make_unique<BoolP>  (id (pid::listen), name ("SC Listen"), false));

        layout.add (std::make_unique<FloatP> (id (pid::thd), name ("THD"),
                                              juce::NormalisableRange<float> (0.0f, range::thdMax, 0.01f, 0.6f), 0.0f,
                                              Attr().withLabel ("%").withStringFromValueFunction (percentString)));
        layout.add (std::make_unique<FloatP> (id (pid::tube), name ("Tube"), trimRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimStepString)));
        layout.add (std::make_unique<BoolP>  (id (pid::tubeIn), name ("Tube In"), true));

        layout.add (std::make_unique<BoolP>  (id (pid::iron), name ("Iron"), false));
        layout.add (std::make_unique<FloatP> (id (pid::ironPad), name ("Iron Pad"),
                                              juce::NormalisableRange<float> (0.0f, range::ironPadMax, 1.0f), 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (padString)));
        layout.add (std::make_unique<BoolP>  (id (pid::ironLink), name ("Iron Link"), false));

        layout.add (std::make_unique<FloatP> (id (pid::mix), name ("Mix"),
                                              juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                              Attr().withLabel ("%").withStringFromValueFunction (percentString)));
        layout.add (std::make_unique<FloatP> (id (pid::output), name ("Output"), trimRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimStepString)));
        layout.add (std::make_unique<BoolP>  (id (pid::dynLink), name ("Dynamic Link"), false));
    }

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

BeaconProcessor::BeaconProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "BEACON", createParameterLayout())
{
    {
        auto& s = params;
        const auto id = [] (const char* name) { return juce::String (name); };

        s.input     = findParameter<juce::AudioParameterFloat> (id (pid::input));
        s.threshold = findParameter<juce::AudioParameterFloat> (id (pid::threshold));
        s.attack    = findParameter<juce::AudioParameterFloat> (id (pid::attack));
        s.release   = findParameter<juce::AudioParameterFloat> (id (pid::release));
        s.feedFwd   = findParameter<juce::AudioParameterBool>  (id (pid::feedFwd));
        s.limit     = findParameter<juce::AudioParameterBool>  (id (pid::limit));
        s.hpf       = findParameter<juce::AudioParameterFloat> (id (pid::hpf));
        s.hpfIn     = findParameter<juce::AudioParameterBool>  (id (pid::hpfIn));
        s.bell      = findParameter<juce::AudioParameterFloat> (id (pid::bell));
        s.bellIn    = findParameter<juce::AudioParameterBool>  (id (pid::bellIn));
        s.listen    = findParameter<juce::AudioParameterBool>  (id (pid::listen));
        s.thd       = findParameter<juce::AudioParameterFloat> (id (pid::thd));
        s.tube      = findParameter<juce::AudioParameterFloat> (id (pid::tube));
        s.tubeIn    = findParameter<juce::AudioParameterBool>  (id (pid::tubeIn));
        s.iron      = findParameter<juce::AudioParameterBool>  (id (pid::iron));
        s.ironPad   = findParameter<juce::AudioParameterFloat> (id (pid::ironPad));
        s.mix       = findParameter<juce::AudioParameterFloat> (id (pid::mix));
        s.output    = findParameter<juce::AudioParameterFloat> (id (pid::output));
    }

    modeParam       = findParameter<juce::AudioParameterChoice> (pid::mode);
    bypassParameter = findParameter<juce::AudioParameterBool>   (pid::bypass);

    // Build the lookup tables here rather than on the audio thread.
    (void) tube::ThdCalibration::get();
    (void) varimu::ForwardTable::forCompress();
    (void) varimu::ForwardTable::forLimit();
}

bool BeaconProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

// ---------------------------------------------------------------------------

void BeaconProcessor::ChannelPath::prepare (double innerRate, double hostRate)
{
    inputIron.prepare (innerRate);
    thdStage.prepare (innerRate);
    sidechain.prepare (innerRate);
    envelope.prepare (innerRate);
    cell.prepare (innerRate);
    outputStage.prepare (innerRate);

    for (auto* v : { &inputGain, &outputGain, &tubeGain, &threshold })
        v->reset (innerRate, 0.02);

    thdDrive.reset (innerRate, 0.02);
    mix     .reset (innerRate, 0.02);
    engaged .reset (hostRate,  0.02);

    reset();
}

void BeaconProcessor::ChannelPath::reset()
{
    inputIron.reset();
    thdStage.reset();
    sidechain.reset();
    envelope.reset();
    cell.reset();
    outputStage.reset();
    lastCellOutput = 0.0f;
}

void BeaconProcessor::ChannelPath::setTargets (const Parameters& p, bool engagedNow)
{
    const auto thresholdControl = p.threshold->get();
    compressing = thresholdControl < -0.05f;

    inputGain .setTargetValue (juce::Decibels::decibelsToGain (p.input ->get()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (p.output->get()));
    tubeGain  .setTargetValue (juce::Decibels::decibelsToGain (p.tube  ->get()));
    threshold .setTargetValue (juce::Decibels::decibelsToGain (range::thresholdDbfs (juce::jmin (thresholdControl, -0.05f))));
    thdDrive  .setTargetValue (tube::ThdCalibration::get().driveFor (p.thd->get()));
    mix       .setTargetValue (p.mix->get() / 100.0f);
    engaged   .setTargetValue (engagedNow ? 1.0f : 0.0f);

    envelope.setTimes (p.attack->get() / 1000.0f, p.release->get());
    sidechain.setTargets (p.hpfIn->get(), p.hpf->get(), p.bellIn->get(), p.bell->get());
    outputStage.setTargets (p.iron->get(), p.ironPad->get());

    feedForward = p.feedFwd->get();
    limiting    = p.limit->get();
    listening   = p.listen->get();
    tubeOn      = p.tubeIn->get();
}

void BeaconProcessor::ChannelPath::snapToTargets()
{
    for (auto* v : { &inputGain, &outputGain, &tubeGain, &threshold })
        v->setCurrentAndTargetValue (v->getTargetValue());

    for (auto* v : { &thdDrive, &mix, &engaged })
        v->setCurrentAndTargetValue (v->getTargetValue());

    outputStage.snapToTargets();
}

void BeaconProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
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
        path.prepare (innerRate, sampleRate);

    // Start at the settings rather than gliding to them from the defaults, so
    // an offline render is what the panel says from the first sample.
    pathsSnapped = false;
}

void BeaconProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());

    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (numChannels == 0 || buffer.getNumSamples() == 0 || oversampler == nullptr)
        return;

    const auto mode     = static_cast<ChannelMode> (modeParam->getIndex());
    const auto bypassed = bypassParameter->get();
    const auto midSide  = mode == ChannelMode::midSide && numChannels == 2;

    // In stereo mode the two channels share one detector, so the image
    // cannot shift. In dual mono and mid/side the detectors are unlinked.
    const auto linked = mode == ChannelMode::stereo && numChannels == 2;

    for (auto& path : paths)
        path.setTargets (params, ! bypassed);

    if (! pathsSnapped)
    {
        for (auto& path : paths)
            path.snapToTargets();

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

    std::array<float*, 2> data { inner.getChannelPointer (0),
                                 numChannels > 1 ? inner.getChannelPointer (1) : nullptr };

    std::array<float, 2> reductionPeak {}, drivePeak {};

    for (size_t i = 0; i < inner.getNumSamples(); ++i)
    {
        std::array<float, 2> raw { data[0][i], numChannels > 1 ? data[1][i] : 0.0f };

        if (midSide)
            raw = { 0.5f * (raw[0] + raw[1]), 0.5f * (raw[0] - raw[1]) };

        // Input trim and input transformer, then THD, the first saturation
        // stage, ahead of the compressor. The dry half of the mix is taken
        // after the transformer and before the THD.
        std::array<float, 2> dryIn {}, x {}, sc {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& path = paths[(size_t) ch];
            const auto c = (size_t) ch;

            dryIn[c] = path.inputIron.process (raw[c] * path.inputGain.getNextValue());

            const auto drive = path.thdDrive.getNextValue();
            drivePeak[c] = juce::jmax (drivePeak[c], std::abs (dryIn[c]) * drive);
            x[c] = path.thdStage.process (dryIn[c], drive);

            // Feedback listens to what the gain cell has already done, as a
            // classic vari-mu does; feed-forward listens to what is coming.
            sc[c] = path.sidechain.process (path.feedForward ? x[c] : path.lastCellOutput);
        }

        std::array<float, 2> reduction {};

        if (linked)
        {
            auto& lead = paths[0];
            const auto envelope  = lead.envelope.process (juce::jmax (std::abs (sc[0]), std::abs (sc[1])));
            const auto threshold = lead.threshold.getNextValue();
            paths[1].threshold.getNextValue();

            if (lead.compressing)
                reduction[0] = reduction[1] = varimu::reduction (envelope, threshold, lead.feedForward, lead.limiting);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& path = paths[(size_t) ch];
                const auto envelope  = path.envelope.process (std::abs (sc[(size_t) ch]));
                const auto threshold = path.threshold.getNextValue();

                if (path.compressing)
                    reduction[(size_t) ch] = varimu::reduction (envelope, threshold, path.feedForward, path.limiting);
            }
        }

        std::array<float, 2> out {};
        auto listening = false;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& path = paths[(size_t) ch];
            const auto c = (size_t) ch;

            const auto y = path.cell.process (x[c], reduction[c], path.tubeGain.getNextValue(), path.tubeOn);
            path.lastCellOutput = y;

            // Parallel compression, sample aligned, then the output gain,
            // which in RED mode is also what drives the tube and the iron.
            const auto wet = path.mix.getNextValue();
            out[c] = (dryIn[c] + wet * (y - dryIn[c])) * path.outputGain.getNextValue();

            if (path.listening)
            {
                out[c] = sc[c];
                listening = true;
            }

            reductionPeak[c] = juce::jmax (reductionPeak[c], reduction[c]);
        }

        // The output stages are wired left and right, outside the mid/side
        // domain.
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

    // IN switches the whole strip, trims included.
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
    }
}

// ---------------------------------------------------------------------------

void BeaconProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BeaconProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BeaconProcessor::createEditor()
{
    return new BeaconEditor (*this);
}

} // namespace beacon

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new beacon::BeaconProcessor();
}
