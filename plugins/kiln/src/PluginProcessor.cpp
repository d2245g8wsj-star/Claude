#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace kiln
{

namespace
{
    juce::String trimString (float value, int)
    {
        if (std::abs (value) < 0.05f)
            return "0.0 dB";

        return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
    }

    juce::String thresholdString (float value, int)
    {
        return juce::String (value, 1) + " dBFS";
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

juce::AudioProcessorValueTreeState::ParameterLayout KilnProcessor::createParameterLayout()
{
    using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatP = juce::AudioParameterFloat;
    using BoolP  = juce::AudioParameterBool;
    using Attr   = juce::AudioParameterFloatAttributes;

    Layout layout;

    layout.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::mode, 1 }, "Mode",
                                                              juce::StringArray { "Dual Mono", "Stereo", "Mid/Side" },
                                                              static_cast<int> (StripMode::stereo)));

    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        const auto id   = [strip] (const char* name) { return juce::ParameterID { pid::forStrip (strip, name), 1 }; };
        const auto name = [strip] (const char* text) { return juce::String (strip == 0 ? "A " : "B ") + text; };

        // Half dB steps, as the stepped encoders on the unit move.
        const juce::NormalisableRange<float> trimRange (-range::trimDb, range::trimDb, 0.5f);

        layout.add (std::make_unique<FloatP> (id (pid::input), name ("Input"), trimRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimString)));

        layout.add (std::make_unique<FloatP> (id (pid::threshold), name ("Threshold"),
                                              juce::NormalisableRange<float> (range::thresholdMin, range::thresholdMax, 0.1f),
                                              -18.0f,
                                              Attr().withLabel ("dBFS").withStringFromValueFunction (thresholdString)));
        layout.add (std::make_unique<FloatP> (id (pid::attack), name ("Attack"),
                                              logRange (range::attackMin, range::attackMax), 10.0f,
                                              Attr().withLabel ("ms").withStringFromValueFunction (attackString)));
        layout.add (std::make_unique<FloatP> (id (pid::release), name ("Release"),
                                              logRange (range::releaseMin, range::releaseMax), 0.5f,
                                              Attr().withLabel ("s").withStringFromValueFunction (releaseString)));
        layout.add (std::make_unique<BoolP>  (id (pid::feedFwd), name ("Feed Forward"), false));
        layout.add (std::make_unique<BoolP>  (id (pid::limit),   name ("Limit"),        false));

        layout.add (std::make_unique<FloatP> (id (pid::scHpf), name ("SC HPF"),
                                              logRange (range::hpfMin, range::hpfMax), 20.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<FloatP> (id (pid::scHf), name ("SC HF"),
                                              logRange (range::hfMin, range::hfMax), 8000.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<BoolP>  (id (pid::scHfIn), name ("SC HF In"), false));
        layout.add (std::make_unique<BoolP>  (id (pid::listen), name ("SC Listen"), false));

        layout.add (std::make_unique<FloatP> (id (pid::thd), name ("THD"),
                                              juce::NormalisableRange<float> (0.0f, range::thdMax, 0.01f, 0.6f), 0.0f,
                                              Attr().withLabel ("%").withStringFromValueFunction (percentString)));
        layout.add (std::make_unique<BoolP>  (id (pid::tube),    name ("Tube Output"), true));
        layout.add (std::make_unique<BoolP>  (id (pid::ironPad), name ("Iron Pad"),    false));

        layout.add (std::make_unique<FloatP> (id (pid::mix), name ("Mix"),
                                              juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                              Attr().withLabel ("%").withStringFromValueFunction (percentString)));
        layout.add (std::make_unique<FloatP> (id (pid::output), name ("Output"), trimRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimString)));
        layout.add (std::make_unique<BoolP>  (id (pid::active), name ("In"), true));
        layout.add (std::make_unique<BoolP>  (id (pid::link),   name ("I/O Link"), false));
    }

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

KilnProcessor::KilnProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "KILN", createParameterLayout())
{
    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        auto& s = strips[(size_t) strip];
        const auto id = [strip] (const char* name) { return pid::forStrip (strip, name); };

        s.input     = findParameter<juce::AudioParameterFloat> (id (pid::input));
        s.threshold = findParameter<juce::AudioParameterFloat> (id (pid::threshold));
        s.attack    = findParameter<juce::AudioParameterFloat> (id (pid::attack));
        s.release   = findParameter<juce::AudioParameterFloat> (id (pid::release));
        s.feedFwd   = findParameter<juce::AudioParameterBool>  (id (pid::feedFwd));
        s.limit     = findParameter<juce::AudioParameterBool>  (id (pid::limit));
        s.scHpf     = findParameter<juce::AudioParameterFloat> (id (pid::scHpf));
        s.scHf      = findParameter<juce::AudioParameterFloat> (id (pid::scHf));
        s.scHfIn    = findParameter<juce::AudioParameterBool>  (id (pid::scHfIn));
        s.listen    = findParameter<juce::AudioParameterBool>  (id (pid::listen));
        s.thd       = findParameter<juce::AudioParameterFloat> (id (pid::thd));
        s.tube      = findParameter<juce::AudioParameterBool>  (id (pid::tube));
        s.ironPad   = findParameter<juce::AudioParameterBool>  (id (pid::ironPad));
        s.mix       = findParameter<juce::AudioParameterFloat> (id (pid::mix));
        s.output    = findParameter<juce::AudioParameterFloat> (id (pid::output));
        s.active    = findParameter<juce::AudioParameterBool>  (id (pid::active));
    }

    modeParam       = findParameter<juce::AudioParameterChoice> (pid::mode);
    bypassParameter = findParameter<juce::AudioParameterBool>   (pid::bypass);

    // Build the lookup tables here rather than on the audio thread.
    (void) tube::ThdCalibration::get();
    (void) varimu::ForwardTable::forCompress();
    (void) varimu::ForwardTable::forLimit();
}

bool KilnProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

// ---------------------------------------------------------------------------

void KilnProcessor::ChannelPath::prepare (double innerRate, double hostRate)
{
    sidechain.prepare (innerRate);
    envelope.prepare (innerRate);
    cell.prepare (innerRate);
    thdStage.prepare (innerRate);
    outputStage.prepare (innerRate);

    inputGain .reset (innerRate, 0.02);
    outputGain.reset (innerRate, 0.02);
    threshold .reset (innerRate, 0.02);
    thdDrive  .reset (innerRate, 0.02);
    mix       .reset (innerRate, 0.02);
    engaged   .reset (hostRate,  0.02);

    reset();
}

void KilnProcessor::ChannelPath::reset()
{
    sidechain.reset();
    envelope.reset();
    cell.reset();
    thdStage.reset();
    outputStage.reset();
    lastCellOutput = 0.0f;
}

void KilnProcessor::ChannelPath::setTargets (const StripParameters& p, bool engagedNow)
{
    inputGain .setTargetValue (juce::Decibels::decibelsToGain (p.input ->get()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (p.output->get()));
    threshold .setTargetValue (juce::Decibels::decibelsToGain (p.threshold->get()));
    thdDrive  .setTargetValue (tube::ThdCalibration::get().driveFor (p.thd->get()));
    mix       .setTargetValue (p.mix->get() / 100.0f);
    engaged   .setTargetValue (engagedNow ? 1.0f : 0.0f);

    envelope.setTimes (p.attack->get() / 1000.0f, p.release->get());
    sidechain.setTargets (p.scHpf->get(), p.scHf->get(), p.scHfIn->get());
    outputStage.setTargets (p.tube->get(), p.ironPad->get());

    feedForward = p.feedFwd->get();
    limiting    = p.limit->get();
    listening   = p.listen->get();
}

void KilnProcessor::ChannelPath::snapToTargets()
{
    for (auto* v : { &inputGain, &outputGain, &threshold })
        v->setCurrentAndTargetValue (v->getTargetValue());

    for (auto* v : { &thdDrive, &mix, &engaged })
        v->setCurrentAndTargetValue (v->getTargetValue());

    outputStage.snapToTargets();
}

void KilnProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
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

void KilnProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());

    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (numChannels == 0 || buffer.getNumSamples() == 0 || oversampler == nullptr)
        return;

    const auto mode     = static_cast<StripMode> (modeParam->getIndex());
    const auto bypassed = bypassParameter->get();
    const auto midSide  = mode == StripMode::midSide && numChannels == 2;

    // In stereo mode strip A drives both channels and the two share one
    // detector, so the image cannot shift; strip B waits for the mode to change.
    const auto linked = mode == StripMode::stereo && numChannels == 2;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto& p = strips[(size_t) (mode == StripMode::stereo ? 0 : ch)];
        paths[(size_t) ch].setTargets (p, ! bypassed && p.active->get());
    }

    if (! pathsSnapped)
    {
        for (auto& path : paths)
            path.snapToTargets();

        pathsSnapped = true;
    }

    if (midSide)
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto m = 0.5f * (left[i] + right[i]);
            const auto s = 0.5f * (left[i] - right[i]);
            left[i]  = m;
            right[i] = s;
        }
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
        std::array<float, 2> x {}, sc {};

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& path = paths[(size_t) ch];
            x[(size_t) ch]  = data[(size_t) ch][i] * path.inputGain.getNextValue();

            // Feedback listens to what the gain cell has already done, as a
            // classic vari-mu does; feed-forward listens to what is coming.
            sc[(size_t) ch] = path.sidechain.process (path.feedForward ? x[(size_t) ch] : path.lastCellOutput);
        }

        std::array<float, 2> reduction {};

        if (linked)
        {
            auto& lead = paths[0];
            const auto envelope = lead.envelope.process (juce::jmax (std::abs (sc[0]), std::abs (sc[1])));
            const auto threshold = lead.threshold.getNextValue();
            paths[1].threshold.getNextValue();

            reduction[0] = reduction[1] = varimu::reduction (envelope, threshold, lead.feedForward, lead.limiting);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto& path = paths[(size_t) ch];
                const auto envelope = path.envelope.process (std::abs (sc[(size_t) ch]));
                reduction[(size_t) ch] = varimu::reduction (envelope, path.threshold.getNextValue(),
                                                            path.feedForward, path.limiting);
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto& path = paths[(size_t) ch];
            const auto in = x[(size_t) ch];

            auto y = path.cell.process (in, reduction[(size_t) ch]);
            path.lastCellOutput = y;

            const auto drive = path.thdDrive.getNextValue();
            drivePeak[(size_t) ch] = juce::jmax (drivePeak[(size_t) ch], std::abs (y) * drive);

            y = path.thdStage.process (y, drive);
            y = path.outputStage.process (y);

            // Parallel compression against the trimmed input, in the same
            // domain, so the two halves are sample aligned.
            const auto wet = path.mix.getNextValue();
            auto out = in + wet * (y - in);
            out *= path.outputGain.getNextValue();

            data[(size_t) ch][i] = path.listening ? sc[(size_t) ch] : out;
            reductionPeak[(size_t) ch] = juce::jmax (reductionPeak[(size_t) ch], reduction[(size_t) ch]);
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

    if (midSide)
    {
        auto* left  = buffer.getWritePointer (0);
        auto* right = buffer.getWritePointer (1);

        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto m = left[i];
            const auto s = right[i];
            left[i]  = m + s;
            right[i] = m - s;
        }
    }
}

// ---------------------------------------------------------------------------

void KilnProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void KilnProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* KilnProcessor::createEditor()
{
    return new KilnEditor (*this);
}

} // namespace kiln

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kiln::KilnProcessor();
}
