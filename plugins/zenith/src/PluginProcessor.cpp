#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace zenith
{

namespace
{
    juce::String decibelString (float value, int)
    {
        if (std::abs (value) < 0.05f)
            return "0.0 dB";

        return (value > 0.0f ? "+" : "") + juce::String (value, 1) + " dB";
    }

    juce::String ceilingString (float value, int)
    {
        return juce::String (value, 1) + " dB";
    }

    juce::String millisecondString (float value, int)
    {
        if (value < 1.0f)
            return juce::String (value, 2) + " ms";

        if (value < 100.0f)
            return juce::String (value, 1) + " ms";

        return juce::String (juce::roundToInt (value)) + " ms";
    }

    juce::String percentString (float value, int)
    {
        const auto steps = juce::roundToInt (value);
        return juce::String (steps) + " %";
    }

    juce::String signedPercentString (float value, int)
    {
        const auto steps = juce::roundToInt (value);
        return (steps > 0 ? "+" : "") + juce::String (steps) + " %";
    }

    /** A logarithmic travel between two positive limits. */
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

    constexpr double dcCornerHz = 5.0;
}

juce::AudioProcessorValueTreeState::ParameterLayout ZenithProcessor::createParameterLayout()
{
    using Layout  = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatP  = juce::AudioParameterFloat;
    using BoolP   = juce::AudioParameterBool;
    using ChoiceP = juce::AudioParameterChoice;
    using Attr    = juce::AudioParameterFloatAttributes;

    const auto id = [] (const char* name) { return juce::ParameterID { name, 1 }; };

    Layout layout;

    juce::NormalisableRange<float> gainRange (-12.0f, 30.0f, 0.1f);
    gainRange.setSkewForCentre (6.0f);

    layout.add (std::make_unique<FloatP> (id (pid::gain), "Gain", gainRange, 0.0f,
                                          Attr().withLabel ("dB").withStringFromValueFunction (decibelString)));
    layout.add (std::make_unique<FloatP> (id (pid::ceiling), "Ceiling",
                                          juce::NormalisableRange<float> (-24.0f, 0.0f, 0.1f), -1.0f,
                                          Attr().withLabel ("dB").withStringFromValueFunction (ceilingString)));

    layout.add (std::make_unique<ChoiceP> (id (pid::engine), "Engine",
                                           juce::StringArray { "Modern", "Analog" }, 0));

    juce::StringArray styleNames;
    for (const auto& s : styles)
        styleNames.add (s.name);

    layout.add (std::make_unique<ChoiceP> (id (pid::style), "Style", styleNames, 4));

    layout.add (std::make_unique<FloatP> (id (pid::lookahead), "Lookahead",
                                          juce::NormalisableRange<float> (0.0f, range::maxLookaheadMs, 0.1f, 0.6f), 2.0f,
                                          Attr().withLabel ("ms").withStringFromValueFunction (millisecondString)));
    layout.add (std::make_unique<FloatP> (id (pid::attack), "Attack", logRange (0.01f, 10.0f), 1.5f,
                                          Attr().withLabel ("ms").withStringFromValueFunction (millisecondString)));
    layout.add (std::make_unique<FloatP> (id (pid::release), "Release", logRange (1.0f, 1000.0f), 80.0f,
                                          Attr().withLabel ("ms").withStringFromValueFunction (millisecondString)));

    const juce::NormalisableRange<float> percent (0.0f, 100.0f, 1.0f);

    layout.add (std::make_unique<FloatP> (id (pid::linkTransients), "Link Transients", percent, 75.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));
    layout.add (std::make_unique<FloatP> (id (pid::linkRelease), "Link Release", percent, 100.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));

    layout.add (std::make_unique<FloatP> (id (pid::transients), "Transients",
                                          juce::NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (signedPercentString)));
    layout.add (std::make_unique<FloatP> (id (pid::detail), "Detail", percent, 0.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));
    layout.add (std::make_unique<FloatP> (id (pid::clip), "Clip", percent, 0.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));

    layout.add (std::make_unique<ChoiceP> (id (pid::oversampling), "Oversampling",
                                           juce::StringArray { "Off", "2x", "4x", "8x", "16x" }, 2));

    layout.add (std::make_unique<BoolP> (id (pid::truePeak), "True Peak", true));
    layout.add (std::make_unique<BoolP> (id (pid::dcFilter), "DC Filter", false));
    layout.add (std::make_unique<BoolP> (id (pid::match),    "Gain Match", false));
    layout.add (std::make_unique<BoolP> (id (pid::delta),    "Delta", false));

    layout.add (std::make_unique<ChoiceP> (id (pid::wordLength), "Word Length",
                                           juce::StringArray { "Off", "24 bit", "20 bit", "16 bit" }, 0));
    layout.add (std::make_unique<ChoiceP> (id (pid::dither), "Dither",
                                           juce::StringArray { "None", "TPDF", "Shaped" }, 2));

    layout.add (std::make_unique<BoolP> (id (pid::bypass), "Bypass", false));

    return layout;
}

ZenithProcessor::ZenithProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "ZENITH", createParameterLayout())
{
    gainParam           = findParameter<juce::AudioParameterFloat>  (pid::gain);
    ceilingParam        = findParameter<juce::AudioParameterFloat>  (pid::ceiling);
    engineParam         = findParameter<juce::AudioParameterChoice> (pid::engine);
    styleParam          = findParameter<juce::AudioParameterChoice> (pid::style);
    lookaheadParam      = findParameter<juce::AudioParameterFloat>  (pid::lookahead);
    attackParam         = findParameter<juce::AudioParameterFloat>  (pid::attack);
    releaseParam        = findParameter<juce::AudioParameterFloat>  (pid::release);
    linkTransientsParam = findParameter<juce::AudioParameterFloat>  (pid::linkTransients);
    linkReleaseParam    = findParameter<juce::AudioParameterFloat>  (pid::linkRelease);
    transientsParam     = findParameter<juce::AudioParameterFloat>  (pid::transients);
    detailParam         = findParameter<juce::AudioParameterFloat>  (pid::detail);
    clipParam           = findParameter<juce::AudioParameterFloat>  (pid::clip);
    oversamplingParam   = findParameter<juce::AudioParameterChoice> (pid::oversampling);
    truePeakParam       = findParameter<juce::AudioParameterBool>   (pid::truePeak);
    dcFilterParam       = findParameter<juce::AudioParameterBool>   (pid::dcFilter);
    matchParam          = findParameter<juce::AudioParameterBool>   (pid::match);
    deltaParam          = findParameter<juce::AudioParameterBool>   (pid::delta);
    wordLengthParam     = findParameter<juce::AudioParameterChoice> (pid::wordLength);
    ditherParam         = findParameter<juce::AudioParameterChoice> (pid::dither);
    bypassParameter     = findParameter<juce::AudioParameterBool>   (pid::bypass);
}

bool ZenithProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

// ---------------------------------------------------------------------------

void ZenithProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    hostRate  = sampleRate;
    blockSize = juce::jmax (1, maximumExpectedSamplesPerBlock);

    int maxOversamplingLatency = 0;

    for (int i = 0; i < numOversamplingChoices; ++i)
    {
        oversamplers[(size_t) i].prepare (i, blockSize);
        maxOversamplingLatency = juce::jmax (maxOversamplingLatency, oversamplers[(size_t) i].getLatency());
    }

    const auto maxFactor     = 1 << (numOversamplingChoices - 1);
    const auto maxLookahead  = (int) std::ceil (range::maxLookaheadMs * 0.001 * sampleRate);
    const auto finalLookahead = juce::roundToInt (range::finalLookaheadMs * 0.001 * sampleRate);

    peakLimiter.prepare (sampleRate, maxLookahead * maxFactor);
    analogLimiter.prepare (sampleRate);
    finalLimiter.prepare (sampleRate, finalLookahead);

    for (auto& a : finalAlign)
        a.prepare (TruePeakInterpolator::delay);

    const auto maxLatency = maxOversamplingLatency + maxLookahead + TruePeakInterpolator::delay + finalLookahead;

    dryDelay.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    dryDelay.setMaximumDelayInSamples (maxLatency + 1);
    dryBuffer.setSize (2, blockSize);
    reductionTrace.assign ((size_t) blockSize, 0.0f);

    enhancer.prepare (sampleRate);
    leveller.prepare (sampleRate);

    dcCoeff = (float) std::exp (-juce::MathConstants<double>::twoPi * dcCornerHz / sampleRate);
    dcState.fill (0.0f);

    inputGain  .reset (sampleRate, 0.05);
    ceilingGain.reset (sampleRate, 0.05);
    monitorGain.reset (sampleRate, 0.3);
    smoothersSnapped = false;

    for (auto& q : quantizers)
        q.reset();

    quantizers[1].setSeed (7u);

    for (auto& t : outputTruePeak)
        t.reset();

    inputLoudness .prepare (sampleRate, 2);
    outputLoudness.prepare (sampleRate, 2);
    samplesSinceLoudness = 0;
    truePeakMax = 0.0f;

    // A column every 5 ms or so: about ten seconds across the display.
    meters.history.prepare (juce::jmax (1, juce::roundToInt (sampleRate * 0.005)));

    structure = { };
    applyStructure (readStructure());
    prepared = true;
}

ZenithProcessor::Structure ZenithProcessor::readStructure() const
{
    Structure s;
    s.oversamplingIndex = juce::jlimit (0, numOversamplingChoices - 1, oversamplingParam->getIndex());
    s.engine            = static_cast<Engine> (engineParam->getIndex());
    s.lookaheadSamples  = s.engine == Engine::modern
                              ? juce::roundToInt (lookaheadParam->get() * 0.001 * hostRate)
                              : 0;
    return s;
}

void ZenithProcessor::applyStructure (const Structure& next)
{
    structure = next;

    const auto factor = 1 << structure.oversamplingIndex;
    auto& os = oversamplers[(size_t) structure.oversamplingIndex];
    os.reset();

    peakLimiter  .setSampleRate (hostRate * factor);
    analogLimiter.prepare       (hostRate * factor);
    finalLimiter .reset();

    for (auto& d : finalDetectors) d.reset();
    for (auto& a : finalAlign)     a.setDelay (TruePeakInterpolator::delay);

    enhancer.reset();
    leveller.reset();

    updateSettings();

    latency = os.getLatency()
              + structure.lookaheadSamples
              + TruePeakInterpolator::delay
              + finalLimiter.getLatency();

    dryDelay.setDelay ((float) latency);
    dryDelay.reset();

    setLatencySamples (latency);
}

void ZenithProcessor::updateSettings()
{
    const auto& style  = styles[(size_t) juce::jlimit (0, (int) styles.size() - 1, styleParam->getIndex())];
    const auto factor  = 1 << structure.oversamplingIndex;
    const auto innerRate = hostRate * factor;

    const auto lookaheadMs = (float) structure.lookaheadSamples * 1000.0f / (float) hostRate;
    const auto attackMs    = juce::jmin (attackParam->get(), lookaheadMs) * style.attackShare;

    PeakLimiter::Settings peak;
    peak.lookahead      = structure.lookaheadSamples * factor;
    peak.attack         = juce::roundToInt (attackMs * 0.001 * innerRate);
    peak.releaseMs      = releaseParam->get() * style.releaseScale;
    peak.adaptive       = style.adaptive;
    peak.bassAware      = style.bassAware;
    peak.linkTransients = linkTransientsParam->get() * 0.01f;
    peak.linkRelease    = linkReleaseParam->get() * 0.01f;
    peakLimiter.configure (peak);

    leveller.configure ({ style.levelRatio, style.levelHeadroomDb, style.levelAttackMs, style.levelReleaseMs });

    // The Attack control spans 0.01 to 10 ms; with no lookahead to hide it in,
    // the analog charge runs ten times quicker across the same travel.
    analogLimiter.configure ({ attackParam->get() * 0.1f, releaseParam->get(), linkTransientsParam->get() * 0.01f });

    clipper.setAmount (clipParam->get() * 0.01f);
    enhancer.setAmounts (transientsParam->get() * 0.01f, detailParam->get() * 0.01f);

    const auto finalLookahead = juce::roundToInt (range::finalLookaheadMs * 0.001 * hostRate);

    PeakLimiter::Settings safety;
    safety.lookahead      = finalLookahead;
    safety.attack         = finalLookahead;
    safety.releaseMs      = range::finalReleaseMs;
    safety.adaptive       = 0.5f;
    safety.bassAware      = 0.5f;
    safety.linkTransients = 1.0f;
    safety.linkRelease    = 1.0f;
    finalLimiter.configure (safety);

    const auto word   = static_cast<WordLength> (wordLengthParam->getIndex());
    const auto dither = static_cast<DitherType> (ditherParam->getIndex());

    for (auto& q : quantizers)
        q.setParameters (word, dither);
}

// ---------------------------------------------------------------------------

void ZenithProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numChannels = juce::jmin (2, getTotalNumInputChannels(), buffer.getNumChannels());

    for (auto ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (numChannels == 0 || buffer.getNumSamples() == 0 || ! prepared)
        return;

    if (const auto next = readStructure(); ! (next == structure))
        applyStructure (next);

    updateSettings();

    inputGain  .setTargetValue (juce::Decibels::decibelsToGain (gainParam->get()));
    ceilingGain.setTargetValue (juce::Decibels::decibelsToGain (ceilingParam->get()));

    if (! smoothersSnapped)
    {
        inputGain  .setCurrentAndTargetValue (inputGain  .getTargetValue());
        ceilingGain.setCurrentAndTargetValue (ceilingGain.getTargetValue());
        smoothersSnapped = true;
    }

    if (meters.resetRequested.exchange (false))
    {
        outputLoudness.reset();
        inputLoudness.reset();
        truePeakMax = 0.0f;
    }

    // The host may hand over more than it promised; the oversamplers cannot
    // take more than they were prepared for, so work in slices.
    for (int start = 0; start < buffer.getNumSamples(); start += blockSize)
    {
        const auto count = juce::jmin (blockSize, buffer.getNumSamples() - start);
        juce::AudioBuffer<float> slice (buffer.getArrayOfWritePointers(), numChannels, start, count);
        processChain (slice, numChannels);
    }
}

void ZenithProcessor::processChain (juce::AudioBuffer<float>& buffer, int numChannels)
{
    const auto numSamples = buffer.getNumSamples();
    const auto bypassed   = bypassParameter->get();
    const auto modern     = structure.engine == Engine::modern;

    // Dry, delayed to line up with the output: for bypass, delta and display.
    dryBuffer.setSize (2, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* dry = dryBuffer.getWritePointer (ch);
        float peak = 0.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            peak = juce::jmax (peak, std::abs (in[i]));
            dryDelay.pushSample (ch, in[i]);
            dry[i] = dryDelay.popSample (ch);
        }

        meters.inputPeak[ch].push (peak);
    }

    inputLoudness.process (buffer.getArrayOfReadPointers(), numChannels, numSamples);

    // -- Gain, DC filter, transients and detail, leveller: host rate --------
    std::array<float*, 2> data { buffer.getWritePointer (0), buffer.getWritePointer (numChannels > 1 ? 1 : 0) };
    std::array<float, 2> frame { };

    const auto dcOn = dcFilterParam->get();
    const auto enhancerOn = enhancer.isActive();
    const auto levellerOn = modern && leveller.isActive();
    float levellerReduction = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto scale = inputGain.getNextValue() / ceilingGain.getNextValue();
        float loudest = 0.0f;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto x = data[(size_t) ch][i] * scale;

            if (dcOn)
            {
                // One pole high pass: subtract a one pole low pass of the input.
                auto& s = dcState[(size_t) ch];
                s = x + dcCoeff * (s - x);
                x -= s;
            }

            frame[(size_t) ch] = x;
            loudest = juce::jmax (loudest, std::abs (x));
        }

        auto g = 1.0f;

        if (enhancerOn)
            g *= enhancer.process (loudest);

        if (levellerOn)
        {
            g *= leveller.process (loudest * g);
            levellerReduction = juce::jmax (levellerReduction, leveller.getReductionDb());
        }

        for (int ch = 0; ch < numChannels; ++ch)
            data[(size_t) ch][i] = frame[(size_t) ch] * g;
    }

    // -- Clipper and main limiter: oversampled -------------------------------
    const auto factor = 1 << structure.oversamplingIndex;
    auto& os = oversamplers[(size_t) structure.oversamplingIndex];

    const auto innerSamples = os.processUp (buffer.getArrayOfReadPointers(), numChannels, numSamples);

    std::array<float*, 2> up { os.getChannel (0), os.getChannel (numChannels > 1 ? 1 : 0) };
    std::array<float, 2> detect { }, result { };

    std::fill (reductionTrace.begin(), reductionTrace.begin() + numSamples, 0.0f);

    for (int i = 0; i < innerSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto x = clipper.process (up[(size_t) ch][i]);
            frame[(size_t) ch] = x;
            detect[(size_t) ch] = std::abs (x);
        }

        float reduction;

        if (modern)
        {
            peakLimiter.process (frame.data(), detect.data(), result.data(), numChannels);
            reduction = peakLimiter.getReductionDb();
        }
        else
        {
            analogLimiter.process (frame.data(), result.data(), numChannels);
            reduction = analogLimiter.getReductionDb();
        }

        for (int ch = 0; ch < numChannels; ++ch)
            up[(size_t) ch][i] = result[(size_t) ch];

        auto& trace = reductionTrace[(size_t) (i / factor)];
        trace = juce::jmax (trace, reduction);
    }

    os.processDown (buffer.getArrayOfWritePointers(), numChannels, numSamples);

    // -- Final stage, ceiling, delta, monitor gain, dither: host rate --------
    const auto truePeak = truePeakParam->get();
    const auto margin   = truePeak ? juce::Decibels::decibelsToGain (range::truePeakMarginDb) : 1.0f;
    const auto delta    = deltaParam->get();
    const auto matching = matchParam->get() && ! delta;

    monitorGain.setTargetValue (matching ? juce::Decibels::decibelsToGain (meters.matchGainDb.load()) : 1.0f);

    std::array<float, 2> outPeak { };
    float blockTruePeak = 0.0f;
    float blockReduction = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto x = data[(size_t) ch][i];
            const auto interSample = finalDetectors[(size_t) ch].push (x);
            const auto aligned = finalAlign[(size_t) ch].process (x);

            frame[(size_t) ch]  = aligned;
            detect[(size_t) ch] = (truePeak ? interSample : std::abs (aligned)) * margin;
        }

        finalLimiter.process (frame.data(), detect.data(), result.data(), numChannels);

        const auto ceiling = ceilingGain.getCurrentValue();
        const auto drive   = inputGain.getCurrentValue();
        const auto monitor = monitorGain.getNextValue();
        const auto reduction = levellerReduction + reductionTrace[(size_t) i] + finalLimiter.getReductionDb();
        blockReduction = juce::jmax (blockReduction, reduction);

        float drivenPeak = 0.0f, outputPeak = 0.0f;
        std::array<float, 2> delivered { };

        for (int ch = 0; ch < numChannels; ++ch)
        {
            // The final clamp: the limiters' proof leaves only float rounding.
            auto y = juce::jlimit (-1.0f, 1.0f, result[(size_t) ch]) * ceiling;
            const auto dry = dryBuffer.getSample (ch, i);

            drivenPeak = juce::jmax (drivenPeak, std::abs (dry * drive));

            if (bypassed)
                y = dry;
            else if (delta)
                y = dry * drive - y;

            delivered[(size_t) ch] = y;
            outputPeak = juce::jmax (outputPeak, std::abs (y));
            outPeak[(size_t) ch] = juce::jmax (outPeak[(size_t) ch], std::abs (y));
            blockTruePeak = juce::jmax (blockTruePeak, outputTruePeak[(size_t) ch].push (y));
        }

        meters.history.push (drivenPeak, outputPeak, bypassed ? 0.0f : reduction);

        // The meters read the signal the limiter delivers; the monitor gain
        // and the dither come after every measurement.
        const float* measured[2] = { &delivered[0], &delivered[1] };
        outputLoudness.process (measured, numChannels, 1);

        for (int ch = 0; ch < numChannels; ++ch)
            data[(size_t) ch][i] = quantizers[(size_t) ch].process (delivered[(size_t) ch] * monitor);
    }

    for (int ch = 0; ch < numChannels; ++ch)
        meters.outputPeak[ch].push (outPeak[(size_t) ch]);

    truePeakMax = juce::jmax (truePeakMax, blockTruePeak);
    meters.outputTruePeak.push (blockTruePeak);
    meters.reduction.push (blockReduction);

    samplesSinceLoudness += numSamples;

    if (samplesSinceLoudness >= juce::roundToInt (hostRate * 0.1))
    {
        samplesSinceLoudness = 0;
        publishLoudness();
    }
}

void ZenithProcessor::publishLoudness()
{
    meters.momentary .store (outputLoudness.momentary());
    meters.shortTerm .store (outputLoudness.shortTerm());
    meters.integrated.store (outputLoudness.integrated());
    meters.range     .store (outputLoudness.range());
    meters.truePeakMax.store (truePeakMax);

    // Gain match: the difference between what came in and what goes out, on
    // the short term loudness, so a level change is judged on equal footing.
    const auto in  = inputLoudness.shortTerm();
    const auto out = outputLoudness.shortTerm();

    if (in > -70.0f && out > -70.0f)
        meters.matchGainDb.store (juce::jlimit (-24.0f, 24.0f, in - out));
}

// ---------------------------------------------------------------------------

void ZenithProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void ZenithProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* ZenithProcessor::createEditor()
{
    return new ZenithEditor (*this);
}

} // namespace zenith

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new zenith::ZenithProcessor();
}
