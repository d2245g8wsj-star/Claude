#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace crest
{

namespace
{
    juce::String dbString (float value, int)
    {
        if (value <= -23.95f)
            return "-inf";

        return juce::String (value, value > -10.0f && value < 10.0f ? 2 : 1) + " dB";
    }

    juce::String percentString (float value, int)
    {
        return juce::String (juce::roundToInt (value)) + " %";
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout CrestProcessor::createParameterLayout()
{
    using Layout  = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatP  = juce::AudioParameterFloat;
    using ChoiceP = juce::AudioParameterChoice;
    using BoolP   = juce::AudioParameterBool;
    using Attr    = juce::AudioParameterFloatAttributes;

    Layout layout;

    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::input, 1 }, "Input",
                                          juce::NormalisableRange<float> (-12.0f, 30.0f, 0.01f), 0.0f,
                                          Attr().withLabel ("dB").withStringFromValueFunction (dbString)));

    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::ceiling, 1 }, "Ceiling",
                                          juce::NormalisableRange<float> (-24.0f, 0.0f, 0.01f), -0.30f,
                                          Attr().withLabel ("dBFS").withStringFromValueFunction (dbString)));

    juce::StringArray modeNames;
    for (int i = 0; i < numModes; ++i)
        modeNames.add (modeName (static_cast<Mode> (i)));

    layout.add (std::make_unique<ChoiceP> (juce::ParameterID { pid::mode, 1 }, "Character", modeNames, 0));

    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::knee, 1 }, "Knee",
                                          juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 40.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));

    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::adapt, 1 }, "Adapt",
                                          juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));

    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::bias, 1 }, "Bias",
                                          juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));

    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::colour, 1 }, "Colour",
                                          juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 35.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));

    {
        // Skewed so the useful range - roughly 40 to 250 Hz, where the body of a
        // kick or a bass note lives - occupies the middle of the travel instead
        // of the first eighth of it.
        juce::NormalisableRange<float> sidechainRange (15.0f, 800.0f, 1.0f);
        sidechainRange.setSkewForCentre (140.0f);

        layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::sidechain, 1 }, "SC LPF",
                                              sidechainRange, 15.0f,
                                              Attr().withLabel ("Hz")
                                                    .withStringFromValueFunction (
                                                        [] (float value, int) -> juce::String
                                                        {
                                                            if (value < sidechainOffBelowHz)
                                                                return "OFF";

                                                            return juce::String (juce::roundToInt (value)) + " Hz";
                                                        })));
    }

    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::mix, 1 }, "Mix",
                                          juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));

    layout.add (std::make_unique<FloatP> (juce::ParameterID { pid::output, 1 }, "Output",
                                          juce::NormalisableRange<float> (-24.0f, 12.0f, 0.01f), 0.0f,
                                          Attr().withLabel ("dB").withStringFromValueFunction (dbString)));

    layout.add (std::make_unique<ChoiceP> (juce::ParameterID { pid::os, 1 }, "Oversampling",
                                           juce::StringArray { "OFF", "2x", "4x", "8x", "16x" }, 2));

    layout.add (std::make_unique<ChoiceP> (juce::ParameterID { pid::word, 1 }, "Word",
                                           juce::StringArray { "OFF", "24", "20", "16" }, 0));

    layout.add (std::make_unique<ChoiceP> (juce::ParameterID { pid::dither, 1 }, "Dither",
                                           juce::StringArray { "OFF", "TPDF", "SHAPED" }, 1));

    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::autoGain, 1 }, "Auto Gain", false));
    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::tpSafe,   1 }, "TP Safe",   true));
    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::delta,    1 }, "Delta",     false));
    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::link,     1 }, "Link",      true));
    layout.add (std::make_unique<BoolP> (juce::ParameterID { pid::bypass,   1 }, "Bypass",    false));

    return layout;
}

CrestProcessor::CrestProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "CREST", createParameterLayout())
{
    inputParam      = findParameter<juce::AudioParameterFloat>  (pid::input);
    ceilingParam    = findParameter<juce::AudioParameterFloat>  (pid::ceiling);
    modeParam       = findParameter<juce::AudioParameterChoice> (pid::mode);
    kneeParam       = findParameter<juce::AudioParameterFloat>  (pid::knee);
    adaptParam      = findParameter<juce::AudioParameterFloat>  (pid::adapt);
    biasParam       = findParameter<juce::AudioParameterFloat>  (pid::bias);
    colourParam     = findParameter<juce::AudioParameterFloat>  (pid::colour);
    sidechainParam  = findParameter<juce::AudioParameterFloat>  (pid::sidechain);
    mixParam        = findParameter<juce::AudioParameterFloat>  (pid::mix);
    outputParam     = findParameter<juce::AudioParameterFloat>  (pid::output);
    osParam         = findParameter<juce::AudioParameterChoice> (pid::os);
    wordParam       = findParameter<juce::AudioParameterChoice> (pid::word);
    ditherParam     = findParameter<juce::AudioParameterChoice> (pid::dither);
    autoGainParam   = findParameter<juce::AudioParameterBool>   (pid::autoGain);
    tpSafeParam     = findParameter<juce::AudioParameterBool>   (pid::tpSafe);
    deltaParam      = findParameter<juce::AudioParameterBool>   (pid::delta);
    linkParam       = findParameter<juce::AudioParameterBool>   (pid::link);
    bypassParameter = findParameter<juce::AudioParameterBool>   (pid::bypass);
}

bool CrestProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void CrestProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    preparedSampleRate = sampleRate;
    preparedBlockSize  = juce::jmax (1, maximumExpectedSamplesPerBlock);

    const auto numChannels = static_cast<size_t> (juce::jmax (1, getTotalNumOutputChannels()));

    for (size_t stage = 0; stage < oversamplers.size(); ++stage)
    {
        oversamplers[stage] = std::make_unique<juce::dsp::Oversampling<float>> (
            numChannels, stage,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true,    // maximum quality
            true);   // integer latency, so the dry path can be delayed exactly

        oversamplers[stage]->initProcessing (static_cast<size_t> (preparedBlockSize));
        oversamplers[stage]->reset();

        oversamplingGain[stage] = measureRoundTripGain (*oversamplers[stage],
                                                        static_cast<int> (numChannels),
                                                        preparedBlockSize);
    }

    currentOversamplingIndex = -1;   // force the latency and colour update on the first block

    juce::dsp::ProcessSpec spec { sampleRate,
                                  static_cast<juce::uint32> (preparedBlockSize),
                                  static_cast<juce::uint32> (numChannels) };

    dryDelay.prepare (spec);
    dryDelay.setMaximumDelayInSamples (2048);
    dryDelay.reset();

    for (auto& c : colour)     c.prepare (sampleRate);
    for (auto& l : lowSplit)   l.prepare (sampleRate);
    for (auto& e : clipEnvelope) e.prepare (sampleRate);
    for (auto& d : dcBlockers) d.prepare (sampleRate);

    for (size_t i = 0; i < quantizers.size(); ++i)
        quantizers[i].setSeed (static_cast<uint32_t> (i * 7919u + 13u));

    dryBuffer.setSize (static_cast<int> (numChannels), preparedBlockSize, false, true, true);

    for (auto* s : { &preGain, &postGain, &makeupGain, &trimGain, &osCorrection })
        s->reset (sampleRate, 0.02);

    mixAmount.reset (sampleRate, 0.02);

    meters.clear();
}

void CrestProcessor::releaseResources()
{
    for (auto& o : oversamplers)
        if (o != nullptr)
            o->reset();

    dryDelay.reset();

    for (auto& c : colour)     c.reset();
    for (auto& l : lowSplit)   l.reset();
    for (auto& e : clipEnvelope) e.reset();
    for (auto& d : dcBlockers) d.reset();
    for (auto& q : quantizers) q.reset();

    meters.clear();
}

float CrestProcessor::measureRoundTripGain (juce::dsp::Oversampling<float>& oversampler,
                                            int numChannels, int blockSize)
{
    juce::AudioBuffer<float> buffer (numChannels, blockSize);

    // DC is the cleanest probe: the round trip gain at DC is the sum of the
    // filter coefficients, with no windowing or leakage to argue about.
    const auto passes = juce::jmax (6, 2048 / juce::jmax (1, blockSize) + 4);

    double measured = 1.0;

    for (int pass = 0; pass < passes; ++pass)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::fill (buffer.getWritePointer (ch), 1.0f, blockSize);

        juce::dsp::AudioBlock<float> block (buffer);

        oversampler.processSamplesUp (block);
        oversampler.processSamplesDown (block);

        if (pass == passes - 1)
        {
            double sum = 0.0;

            // Average the tail of the last block, past any remaining ramp.
            const auto from = blockSize / 2;

            for (int n = from; n < blockSize; ++n)
                sum += (double) buffer.getSample (0, n);

            measured = sum / (double) (blockSize - from);
        }
    }

    oversampler.reset();

    // Anything wildly off means the probe did not settle; leave the level alone
    // rather than "correcting" it by a factor of two.
    return (measured > 0.5 && measured < 2.0) ? (float) (1.0 / measured) : 1.0f;
}

void CrestProcessor::updateOversamplingLatency()
{
    const auto latency = oversamplers[static_cast<size_t> (currentOversamplingIndex)]->getLatencyInSamples();

    dryDelay.setDelay (latency);
    setLatencySamples (juce::roundToInt (latency));
}

void CrestProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto totalIn    = getTotalNumInputChannels();
    const auto totalOut   = getTotalNumOutputChannels();

    for (int ch = totalIn; ch < totalOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    const auto numChannels = juce::jlimit (0, 2, juce::jmin (totalIn, totalOut));

    if (numChannels == 0 || numSamples == 0)
        return;

    // ---------------------------------------------------------------- routing
    const auto osIndex = juce::jlimit (0, maxOversamplingStages, osParam->getIndex());

    if (osIndex != currentOversamplingIndex)
    {
        currentOversamplingIndex = osIndex;
        updateOversamplingLatency();

        // The colour stage lives in the oversampled domain, so its corner
        // frequency has to follow the rate it actually runs at.
        const auto osRate = preparedSampleRate * static_cast<double> (1 << osIndex);

        for (auto& c : colour)
            c.prepare (osRate);

        for (auto& l : lowSplit)
            l.prepare (osRate);

        for (auto& e : clipEnvelope)
            e.prepare (osRate);

        dryDelay.reset();
    }

    auto& oversampler = *oversamplers[static_cast<size_t> (osIndex)];

    // ------------------------------------------------------------- input meter
    for (int ch = 0; ch < numChannels; ++ch)
        meters.inputPeak[ch].push (buffer.getMagnitude (ch, 0, numSamples));

    // ------------------------------------------------------------------ bypass
    // Still runs through the delay line, so switching bypass does not shift the
    // signal in time relative to the rest of the session.
    if (bypassParameter->get())
    {
        for (int n = 0; n < numSamples; ++n)
        {
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                dryDelay.pushSample (ch, data[n]);
                data[n] = dryDelay.popSample (ch);
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
            meters.outputPeak[ch].push (buffer.getMagnitude (ch, 0, numSamples));

        meters.reduction.push (0.0f);
        return;
    }

    // -------------------------------------------------------------- parameters
    const auto ceilingLin = juce::Decibels::decibelsToGain (ceilingParam->get());
    const auto inputLin   = juce::Decibels::decibelsToGain (inputParam->get());
    const auto deltaOn    = deltaParam->get();
    const auto linkOn     = linkParam->get() && numChannels == 2;
    const auto tpSafeOn   = tpSafeParam->get();

    // Folding the ceiling into the gain staging keeps the shaper itself in a
    // fixed 0...1 domain: everything below 1.0 passes untouched, and 1.0 is the
    // ceiling by construction.
    preGain   .setTargetValue (juce::jmax (1.0e-6f, inputLin / ceilingLin));
    postGain  .setTargetValue (juce::jmax (1.0e-6f, ceilingLin));
    makeupGain.setTargetValue (autoGainParam->get() ? juce::jmax (1.0e-6f, 1.0f / inputLin) : 1.0f);
    trimGain  .setTargetValue (juce::Decibels::decibelsToGain (outputParam->get()));
    osCorrection.setTargetValue (oversamplingGain[static_cast<size_t> (osIndex)]);
    mixAmount .setTargetValue (mixParam->get() * 0.01f);

    const auto mode      = static_cast<Mode> (modeParam->getIndex());
    const auto biasAmount = biasParam->get() * 0.01f;
    const auto baseKnee   = kneeParam->get() * 0.01f;
    const auto adaptation = adaptParam->get() * 0.01f;
    const auto adaptive   = adaptation > 1.0e-4f;

    shaper.setParameters (mode, baseKnee, biasAmount);

    // The envelope is driven against the *unadapted* threshold, so the
    // adaptation cannot feed back into its own trigger.
    const auto triggerLevel = juce::jmin (shaper.thresholdPos, shaper.thresholdNeg);

    // Knee is the corner for sustained material, which is where a corner can
    // actually be heard. Adapt pulls transients back towards a square one,
    // because a few samples taken squarely off a peak are inaudible and keep
    // the punch that a rounded corner would smear.
    const auto adaptedKnee = [baseKnee, adaptation] (float density)
    {
        return baseKnee * (1.0f - adaptation * (1.0f - ClipEnvelope::shape (density)));
    };

    displayThresholdPos.store (shaper.thresholdPos, std::memory_order_relaxed);
    displayThresholdNeg.store (shaper.thresholdNeg, std::memory_order_relaxed);

    const auto colourAmount = colourParam->get() * 0.01f;

    for (auto& c : colour)
        c.setAmount (colourAmount);

    // The sidechain runs on the clipper's difference signal, so it is only ever
    // fed something non-zero while the clipper is actually working.
    const auto sidechainHz = sidechainParam->get();
    const auto sidechainOn = sidechainHz >= sidechainOffBelowHz;

    if (sidechainOn)
    {
        if (! sidechainActive)
            for (auto& l : lowSplit)
                l.reset();

        for (auto& l : lowSplit)
            l.setCutoff (sidechainHz);
    }

    sidechainActive = sidechainOn;
    sidechainRunning.store (sidechainOn, std::memory_order_relaxed);

    // Only the asymmetric paths can produce an offset. With bias and colour at
    // zero the shaper is an odd function, so the high pass would cost phase
    // rotation in return for removing DC that does not exist.
    // Compared against a threshold rather than exactly zero: a bias of a
    // hundredth of a percent produces an offset far below the noise floor of
    // any word length this plugin can output, which is not worth a high pass
    // and the phase rotation that comes with it.
    const auto needsDcBlocker = std::abs (biasParam->get()) > 0.01f || colourAmount > 0.0001f;

    if (needsDcBlocker && ! dcBlockerActive)
        for (auto& d : dcBlockers)
            d.reset();

    dcBlockerActive = needsDcBlocker;
    dcBlockerRunning.store (needsDcBlocker, std::memory_order_relaxed);
    appliedOversamplingGain.store (oversamplingGain[static_cast<size_t> (osIndex)],
                                   std::memory_order_relaxed);

    for (auto& q : quantizers)
        q.setParameters (static_cast<WordLength> (wordParam->getIndex()),
                         static_cast<DitherType> (ditherParam->getIndex()));

    // --------------------------------------------------------------- dry copy
    dryBuffer.setSize (numChannels, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // --------------------------------------------------- input trim + ceiling
    for (int n = 0; n < numSamples; ++n)
    {
        const auto g = preGain.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[n] *= g;
    }

    // ------------------------------------------------------- clip, oversampled
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(),
                                        static_cast<size_t> (numChannels),
                                        static_cast<size_t> (numSamples));

    auto osBlock = oversampler.processSamplesUp (block);

    const auto numOsSamples = static_cast<int> (osBlock.getNumSamples());

    float frameDrive     = 0.0f;
    float frameReduction = 0.0f;
    bool  litL = false, litR = false;

    if (linkOn)
    {
        auto* left  = osBlock.getChannelPointer (0);
        auto* right = osBlock.getChannelPointer (1);

        for (int n = 0; n < numOsSamples; ++n)
        {
            const auto l = colour[0].process (left[n]);
            const auto r = colour[1].process (right[n]);

            // Clipping the loudest of the pair and applying the resulting gain
            // to both keeps the stereo image where it was; clipping the two
            // channels independently pulls hard-panned peaks toward the centre.
            const auto peak = juce::jmax (std::abs (l), std::abs (r));

            if (adaptive)
            {
                const auto density = clipEnvelope[0].process (peak > triggerLevel);

                shaper.setParameters (mode, adaptedKnee (density), biasAmount);
            }

            auto gain = 1.0f;

            if (peak > 1.0e-9f)
            {
                gain = shaper.process (peak) / peak;

                frameDrive     = juce::jmax (frameDrive, peak);
                frameReduction = juce::jmax (frameReduction, 1.0f - gain);

                if (gain < 0.999f)
                {
                    litL = litL || std::abs (l) > 1.0e-9f;
                    litR = litR || std::abs (r) > 1.0e-9f;
                }
            }

            auto outL = l * gain;
            auto outR = r * gain;

            if (sidechainActive)
            {
                // Hand back the low frequency part of what was just removed.
                outL += lowSplit[0].process (l - outL);
                outR += lowSplit[1].process (r - outR);
            }

            left[n]  = outL;
            right[n] = outR;
        }
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* samples = osBlock.getChannelPointer (static_cast<size_t> (ch));

            for (int n = 0; n < numOsSamples; ++n)
            {
                const auto x = colour[static_cast<size_t> (ch)].process (samples[n]);

                if (adaptive)
                {
                    const auto density = clipEnvelope[static_cast<size_t> (ch)]
                                             .process (std::abs (x) > triggerLevel);

                    shaper.setParameters (mode, adaptedKnee (density), biasAmount);
                }

                const auto y = shaper.process (x);

                const auto magnitude = std::abs (x);

                if (magnitude > 1.0e-9f)
                {
                    frameDrive     = juce::jmax (frameDrive, magnitude);
                    frameReduction = juce::jmax (frameReduction, 1.0f - std::abs (y) / magnitude);

                    if (std::abs (y) < magnitude * 0.999f)
                        (ch == 0 ? litL : litR) = true;
                }

                samples[n] = sidechainActive
                                 ? y + lowSplit[static_cast<size_t> (ch)].process (x - y)
                                 : y;
            }
        }
    }

    oversampler.processSamplesDown (block);

    {
        const auto density = adaptive ? clipEnvelope[0].value() : 0.0f;

        displayDensity.store (density, std::memory_order_relaxed);
        displayKnee.store (adaptive ? adaptedKnee (density) : baseKnee, std::memory_order_relaxed);
    }

    meters.driveMagnitude.push (frameDrive);
    meters.reduction.push (frameReduction);

    if (litL) meters.clipLightL.store (true, std::memory_order_relaxed);
    if (litR) meters.clipLightR.store (true, std::memory_order_relaxed);

    // ------------------------------------------------------------- output side
    for (int n = 0; n < numSamples; ++n)
    {
        const auto post       = postGain.getNextValue();
        const auto correction = osCorrection.getNextValue();
        const auto makeup     = makeupGain.getNextValue();
        const auto trim   = trimGain.getNextValue();
        const auto mix    = mixAmount.getNextValue();

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* data = buffer.getWritePointer (ch);

            auto wet = dcBlockerActive ? dcBlockers[static_cast<size_t> (ch)].process (data[n])
                                       : data[n];

            wet *= correction;

            // The band limited downsampling filter can ring a hair past the
            // ceiling; this catches the overshoot at the base rate, where the
            // host's true peak meter will see it. It sits after the gain
            // correction so that the clamp and the ceiling are the same number.
            if (tpSafeOn)
                wet = juce::jlimit (-1.0f, 1.0f, wet);

            wet *= post * makeup;

            dryDelay.pushSample (ch, dryBuffer.getSample (ch, n));
            const auto dry = dryDelay.popSample (ch);

            auto out = deltaOn ? (wet - dry)
                               : (dry + (wet - dry) * mix);

            out *= trim;

            data[n] = quantizers[static_cast<size_t> (ch)].process (out);
        }
    }

    for (int ch = 0; ch < numChannels; ++ch)
        meters.outputPeak[ch].push (buffer.getMagnitude (ch, 0, numSamples));
}

juce::AudioProcessorEditor* CrestProcessor::createEditor()
{
    return new CrestEditor (*this);
}

void CrestProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto state = apvts.copyState(); state.isValid())
    {
        juce::MemoryOutputStream stream (destData, false);
        state.writeToStream (stream);
    }
}

void CrestProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));

    if (tree.isValid() && tree.hasType (apvts.state.getType()))
        apvts.replaceState (tree);
}

} // namespace crest

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new crest::CrestProcessor();
}
