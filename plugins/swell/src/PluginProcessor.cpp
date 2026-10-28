#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace swell
{

namespace
{
    juce::String trimString (float value, int)
    {
        const auto steps = juce::roundToInt (value);
        return (steps > 0 ? "+" : "") + juce::String (steps) + " dB";
    }

    juce::String gainString (float value, int)
    {
        if (std::abs (value) < 0.05f)
            return "0.0 dB";

        return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
    }

    juce::String frequencyString (float value, int)
    {
        if (value >= 1000.0f)
            return juce::String (value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz";

        return juce::String (juce::roundToInt (value)) + " Hz";
    }

    /** A logarithmic travel, so each octave gets the same turn of the knob. */
    juce::NormalisableRange<float> frequencyRange (float low, float high)
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

juce::AudioProcessorValueTreeState::ParameterLayout SwellProcessor::createParameterLayout()
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

        // Input and output step in whole dB, as the trims on a hardware strip do.
        const juce::NormalisableRange<float> trimRange (-range::trimDb, range::trimDb, 1.0f);
        const juce::NormalisableRange<float> gainRange (-HarmonicBand::maxGainDb, HarmonicBand::maxGainDb, 0.1f);

        layout.add (std::make_unique<FloatP> (id (pid::input), name ("Input"), trimRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimString)));

        layout.add (std::make_unique<FloatP> (id (pid::loGain), name ("Lo Level"), gainRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (gainString)));
        layout.add (std::make_unique<FloatP> (id (pid::loFreq), name ("Lo Freq"),
                                              frequencyRange (range::loMin, range::loMax), 90.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<BoolP>  (id (pid::loIn), name ("Lo In"), true));

        layout.add (std::make_unique<FloatP> (id (pid::midGain), name ("Mid Level"), gainRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (gainString)));
        layout.add (std::make_unique<FloatP> (id (pid::midFreq), name ("Mid Freq"),
                                              frequencyRange (range::midMin, range::midMax), 1200.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<BoolP>  (id (pid::midIn), name ("Mid In"), true));

        layout.add (std::make_unique<FloatP> (id (pid::hiGain), name ("Hi Level"), gainRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (gainString)));
        layout.add (std::make_unique<FloatP> (id (pid::hiFreq), name ("Hi Freq"),
                                              frequencyRange (range::hiMin, range::hiMax), 12000.0f,
                                              Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
        layout.add (std::make_unique<BoolP>  (id (pid::hiIn), name ("Hi In"), true));

        layout.add (std::make_unique<FloatP> (id (pid::output), name ("Output"), trimRange, 0.0f,
                                              Attr().withLabel ("dB").withStringFromValueFunction (trimString)));
        layout.add (std::make_unique<BoolP>  (id (pid::active), name ("In"), true));
    }

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass, 1 }, "Bypass", false));

    return layout;
}

SwellProcessor::SwellProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SWELL", createParameterLayout())
{
    for (int strip = 0; strip < numChannelStrips; ++strip)
    {
        auto& s = strips[(size_t) strip];
        const auto id = [strip] (const char* name) { return pid::forStrip (strip, name); };

        s.input   = findParameter<juce::AudioParameterFloat> (id (pid::input));
        s.loGain  = findParameter<juce::AudioParameterFloat> (id (pid::loGain));
        s.loFreq  = findParameter<juce::AudioParameterFloat> (id (pid::loFreq));
        s.loIn    = findParameter<juce::AudioParameterBool>  (id (pid::loIn));
        s.midGain = findParameter<juce::AudioParameterFloat> (id (pid::midGain));
        s.midFreq = findParameter<juce::AudioParameterFloat> (id (pid::midFreq));
        s.midIn   = findParameter<juce::AudioParameterBool>  (id (pid::midIn));
        s.hiGain  = findParameter<juce::AudioParameterFloat> (id (pid::hiGain));
        s.hiFreq  = findParameter<juce::AudioParameterFloat> (id (pid::hiFreq));
        s.hiIn    = findParameter<juce::AudioParameterBool>  (id (pid::hiIn));
        s.output  = findParameter<juce::AudioParameterFloat> (id (pid::output));
        s.active  = findParameter<juce::AudioParameterBool>  (id (pid::active));
    }

    modeParam       = findParameter<juce::AudioParameterChoice> (pid::mode);
    bypassParameter = findParameter<juce::AudioParameterBool>   (pid::bypass);
}

bool SwellProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

// ---------------------------------------------------------------------------

void SwellProcessor::ChannelPath::prepare (double innerRate, double hostRate)
{
    for (auto* band : { &lo, &mid, &hi })
        band->prepare (innerRate);

    inputGain .reset (innerRate, 0.02);
    outputGain.reset (innerRate, 0.02);
    engaged   .reset (hostRate,  0.02);
}

void SwellProcessor::ChannelPath::setTargets (const StripParameters& p, bool engagedNow)
{
    inputGain .setTargetValue (juce::Decibels::decibelsToGain (p.input ->get()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (p.output->get()));
    engaged   .setTargetValue (engagedNow ? 1.0f : 0.0f);

    lo .setTargets (p.loFreq ->get(), p.loGain ->get(), p.loIn ->get());
    mid.setTargets (p.midFreq->get(), p.midGain->get(), p.midIn->get());
    hi .setTargets (p.hiFreq ->get(), p.hiGain ->get(), p.hiIn ->get());
}

void SwellProcessor::ChannelPath::snapToTargets()
{
    inputGain .setCurrentAndTargetValue (inputGain .getTargetValue());
    outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    engaged   .setCurrentAndTargetValue (engaged   .getTargetValue());

    for (auto* band : { &lo, &mid, &hi })
        band->snapToTargets();
}

void SwellProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
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
        path.prepare (innerRate, sampleRate);
        path.lo .reset();
        path.mid.reset();
        path.hi .reset();
    }

    // Start at the settings rather than gliding to them from the defaults, so
    // an offline render is what the panel says from the first sample.
    pathsSnapped = false;
}

void SwellProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
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

    // In stereo mode strip A drives both channels, so the two sides cannot
    // drift apart; strip B is ignored until the mode changes.
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

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& path = paths[(size_t) ch];
        auto* data = inner.getChannelPointer ((size_t) ch);

        for (size_t i = 0; i < inner.getNumSamples(); ++i)
        {
            const auto x = data[i] * path.inputGain.getNextValue();

            // The three bands work in parallel on the same signal and are summed
            // onto it, so a boost in one shifts the region its neighbour sees.
            const auto y = x + path.lo.process (x) + path.mid.process (x) + path.hi.process (x);
            data[i] = y * path.outputGain.getNextValue();
        }

        auto& meter = meters.channels[(size_t) ch];
        MeterSource::raise (meter.bandDrive[0], path.lo .getDrive());
        MeterSource::raise (meter.bandDrive[1], path.mid.getDrive());
        MeterSource::raise (meter.bandDrive[2], path.hi .getDrive());
    }

    oversampler->processSamplesDown (block);

    // IN switches the whole strip, trims included.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& path = paths[(size_t) ch];
        auto* data = buffer.getWritePointer (ch);
        const auto* dry = dryBuffer.getReadPointer (ch);

        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto wet = path.engaged.getNextValue();
            data[i] = dry[i] + wet * (data[i] - dry[i]);
            peak = juce::jmax (peak, std::abs (data[i]));
        }

        MeterSource::raise (meters.channels[(size_t) ch].outputPeak, peak);
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

void SwellProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SwellProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* SwellProcessor::createEditor()
{
    return new SwellEditor (*this);
}

} // namespace swell

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new swell::SwellProcessor();
}
