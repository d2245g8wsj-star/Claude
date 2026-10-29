#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace breakwater
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

    juce::String ratioString (float value, int)
    {
        return juce::String (value, value < 10.0f ? 1 : 0) + ":1";
    }

    juce::String frequencyString (float value, int)
    {
        if (value <= 20.5f)
            return "Off";

        return juce::String (juce::roundToInt (value)) + " Hz";
    }

    juce::String balanceString (float value, int)
    {
        const auto steps = juce::roundToInt (value);

        if (steps == 0)
            return "Centre";

        return juce::String (std::abs (steps)) + (steps < 0 ? " % Lows" : " % Highs");
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

juce::AudioProcessorValueTreeState::ParameterLayout BreakwaterProcessor::createParameterLayout()
{
    using Layout  = juce::AudioProcessorValueTreeState::ParameterLayout;
    using FloatP  = juce::AudioParameterFloat;
    using BoolP   = juce::AudioParameterBool;
    using ChoiceP = juce::AudioParameterChoice;
    using Attr    = juce::AudioParameterFloatAttributes;

    const auto id = [] (const char* name) { return juce::ParameterID { name, 1 }; };
    const juce::NormalisableRange<float> percent (0.0f, 100.0f, 1.0f);
    const juce::NormalisableRange<float> bipolar (-100.0f, 100.0f, 1.0f);

    Layout layout;

    // -- Limiter --------------------------------------------------------------
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

    juce::StringArray windowNames;
    for (const auto& w : windowChoices)
        windowNames.add (w.name);

    layout.add (std::make_unique<ChoiceP> (id (pid::window), "Window", windowNames, 0));

    layout.add (std::make_unique<FloatP> (id (pid::linkTransients), "Link Transients", percent, 75.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));
    layout.add (std::make_unique<FloatP> (id (pid::linkRelease), "Link Release", percent, 100.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));
    layout.add (std::make_unique<BoolP> (id (pid::adaptiveLink), "Adaptive Link", false));

    // -- Compressor -------------------------------------------------------------
    layout.add (std::make_unique<BoolP> (id (pid::compOn), "Comp On", false));
    layout.add (std::make_unique<FloatP> (id (pid::compThreshold), "Comp Threshold",
                                          juce::NormalisableRange<float> (-30.0f, 6.0f, 0.1f), -6.0f,
                                          Attr().withLabel ("dB").withStringFromValueFunction (decibelString)));
    layout.add (std::make_unique<FloatP> (id (pid::compRatio), "Comp Ratio", logRange (1.0f, 10.0f), 2.0f,
                                          Attr().withStringFromValueFunction (ratioString)));
    layout.add (std::make_unique<FloatP> (id (pid::compAttack), "Comp Attack", logRange (0.1f, 100.0f), 10.0f,
                                          Attr().withLabel ("ms").withStringFromValueFunction (millisecondString)));
    layout.add (std::make_unique<FloatP> (id (pid::compRelease), "Comp Release", logRange (10.0f, 2000.0f), 150.0f,
                                          Attr().withLabel ("ms").withStringFromValueFunction (millisecondString)));
    layout.add (std::make_unique<FloatP> (id (pid::compHighPass), "Comp SC High Pass", logRange (20.0f, 300.0f), 20.0f,
                                          Attr().withLabel ("Hz").withStringFromValueFunction (frequencyString)));
    layout.add (std::make_unique<ChoiceP> (id (pid::compMode), "Comp Mode",
                                           juce::StringArray { "Stereo", "Mid/Side" }, 0));

    // -- Shape ------------------------------------------------------------------
    layout.add (std::make_unique<FloatP> (id (pid::transients), "Transients", bipolar, 0.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (signedPercentString)));
    layout.add (std::make_unique<FloatP> (id (pid::lowPunch), "Low Punch", bipolar, 0.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (signedPercentString)));
    layout.add (std::make_unique<FloatP> (id (pid::detail), "Detail", percent, 0.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));
    layout.add (std::make_unique<FloatP> (id (pid::balance), "Balance", bipolar, 0.0f,
                                          Attr().withStringFromValueFunction (balanceString)));
    layout.add (std::make_unique<FloatP> (id (pid::clip), "Clip", percent, 0.0f,
                                          Attr().withLabel ("%").withStringFromValueFunction (percentString)));

    // -- Output -----------------------------------------------------------------
    layout.add (std::make_unique<ChoiceP> (id (pid::oversampling), "Oversampling",
                                           juce::StringArray { "Off", "2x", "4x", "8x", "16x", "32x" }, 2));

    layout.add (std::make_unique<BoolP> (id (pid::truePeak),  "True Peak", true));
    layout.add (std::make_unique<BoolP> (id (pid::dcFilter),  "DC Filter", false));
    layout.add (std::make_unique<BoolP> (id (pid::match),     "Gain Match", false));
    layout.add (std::make_unique<BoolP> (id (pid::delta),     "Delta", false));
    layout.add (std::make_unique<BoolP> (id (pid::sidechain), "External Sidechain", false));

    juce::StringArray targetNames;
    for (const auto& t : loudnessTargets)
        targetNames.add (t.name);

    layout.add (std::make_unique<ChoiceP> (id (pid::target), "Loudness Target", targetNames, 3));

    layout.add (std::make_unique<ChoiceP> (id (pid::wordLength), "Word Length",
                                           juce::StringArray { "Off", "24 bit", "20 bit", "16 bit" }, 0));
    layout.add (std::make_unique<ChoiceP> (id (pid::dither), "Dither",
                                           juce::StringArray { "None", "TPDF", "Shaped" }, 2));

    layout.add (std::make_unique<BoolP> (id (pid::bypass), "Bypass", false));

    return layout;
}

BreakwaterProcessor::BreakwaterProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                                .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "BREAKWATER", createParameterLayout())
{
    gainParam           = findParameter<juce::AudioParameterFloat>  (pid::gain);
    ceilingParam        = findParameter<juce::AudioParameterFloat>  (pid::ceiling);
    engineParam         = findParameter<juce::AudioParameterChoice> (pid::engine);
    styleParam          = findParameter<juce::AudioParameterChoice> (pid::style);
    lookaheadParam      = findParameter<juce::AudioParameterFloat>  (pid::lookahead);
    attackParam         = findParameter<juce::AudioParameterFloat>  (pid::attack);
    releaseParam        = findParameter<juce::AudioParameterFloat>  (pid::release);
    windowParam         = findParameter<juce::AudioParameterChoice> (pid::window);
    linkTransientsParam = findParameter<juce::AudioParameterFloat>  (pid::linkTransients);
    linkReleaseParam    = findParameter<juce::AudioParameterFloat>  (pid::linkRelease);
    adaptiveLinkParam   = findParameter<juce::AudioParameterBool>   (pid::adaptiveLink);

    compOnParam         = findParameter<juce::AudioParameterBool>   (pid::compOn);
    compThresholdParam  = findParameter<juce::AudioParameterFloat>  (pid::compThreshold);
    compRatioParam      = findParameter<juce::AudioParameterFloat>  (pid::compRatio);
    compAttackParam     = findParameter<juce::AudioParameterFloat>  (pid::compAttack);
    compReleaseParam    = findParameter<juce::AudioParameterFloat>  (pid::compRelease);
    compHighPassParam   = findParameter<juce::AudioParameterFloat>  (pid::compHighPass);
    compModeParam       = findParameter<juce::AudioParameterChoice> (pid::compMode);

    transientsParam     = findParameter<juce::AudioParameterFloat>  (pid::transients);
    lowPunchParam       = findParameter<juce::AudioParameterFloat>  (pid::lowPunch);
    detailParam         = findParameter<juce::AudioParameterFloat>  (pid::detail);
    balanceParam        = findParameter<juce::AudioParameterFloat>  (pid::balance);
    clipParam           = findParameter<juce::AudioParameterFloat>  (pid::clip);

    oversamplingParam   = findParameter<juce::AudioParameterChoice> (pid::oversampling);
    truePeakParam       = findParameter<juce::AudioParameterBool>   (pid::truePeak);
    dcFilterParam       = findParameter<juce::AudioParameterBool>   (pid::dcFilter);
    matchParam          = findParameter<juce::AudioParameterBool>   (pid::match);
    deltaParam          = findParameter<juce::AudioParameterBool>   (pid::delta);
    sidechainParam      = findParameter<juce::AudioParameterBool>   (pid::sidechain);
    wordLengthParam     = findParameter<juce::AudioParameterChoice> (pid::wordLength);
    ditherParam         = findParameter<juce::AudioParameterChoice> (pid::dither);
    bypassParameter     = findParameter<juce::AudioParameterBool>   (pid::bypass);
}

bool BreakwaterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainInputChannelSet() != out)
        return false;

    // The sidechain may be absent, mono or stereo, whatever the main bus is.
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sidechain = layouts.getChannelSet (true, 1);

        if (! sidechain.isDisabled()
            && sidechain != juce::AudioChannelSet::mono()
            && sidechain != juce::AudioChannelSet::stereo())
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------

void BreakwaterProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    hostRate  = sampleRate;
    blockSize = juce::jmax (1, maximumExpectedSamplesPerBlock);

    int maxOversamplingLatency = 0;

    for (int i = 0; i < numOversamplingChoices; ++i)
    {
        oversamplers[(size_t) i].prepare (i, blockSize);
        sidechainOversamplers[(size_t) i].prepare (i, blockSize);
        maxOversamplingLatency = juce::jmax (maxOversamplingLatency, oversamplers[(size_t) i].getLatency());
    }

    const auto maxFactor      = 1 << (numOversamplingChoices - 1);
    const auto maxLookahead   = (int) std::ceil (range::maxLookaheadMs * 0.001 * sampleRate);
    const auto maxWindow      = (int) std::ceil (windowChoices.back().milliseconds * 0.001 * sampleRate);
    const auto finalLookahead = juce::roundToInt (range::finalLookaheadMs * 0.001 * sampleRate);

    peakLimiter.prepare (sampleRate, maxLookahead * maxFactor, maxWindow * maxFactor);
    analogLimiter.allocate (maxWindow * maxFactor);
    analogLimiter.prepare (sampleRate);
    finalLimiter.prepare (sampleRate, finalLookahead);

    for (auto& a : finalAlign)
        a.prepare (TruePeakInterpolator::delay);

    const auto maxLatency = maxOversamplingLatency + maxLookahead + TruePeakInterpolator::delay + finalLookahead;

    dryDelay.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
    dryDelay.setMaximumDelayInSamples (maxLatency + 1);
    dryBuffer.setSize (2, blockSize);
    sidechainScratch.setSize (2, blockSize);
    reductionTrace.assign ((size_t) blockSize, 0.0f);

    compressor.prepare (sampleRate);
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

    // VU: a 300 ms integration. Correlation: 100 ms.
    vuCoeff = onePole (300.0, sampleRate);
    vuPower = 0.0f;
    correlationCoeff = onePole (100.0, sampleRate);
    sumLR = sumLL = sumRR = 0.0f;

    // A column every 5 ms or so: about seven seconds across the display.
    meters.history.prepare (juce::jmax (1, juce::roundToInt (sampleRate * 0.005)));

    structure = { };
    applyStructure (readStructure());
    prepared = true;
}

BreakwaterProcessor::Structure BreakwaterProcessor::readStructure() const
{
    Structure s;
    s.oversamplingIndex = juce::jlimit (0, numOversamplingChoices - 1, oversamplingParam->getIndex());
    s.engine            = static_cast<Engine> (engineParam->getIndex());
    s.lookaheadSamples  = s.engine == Engine::modern
                              ? juce::roundToInt (lookaheadParam->get() * 0.001 * hostRate)
                              : 0;
    return s;
}

void BreakwaterProcessor::applyStructure (const Structure& next)
{
    structure = next;

    const auto factor = 1 << structure.oversamplingIndex;
    auto& os = oversamplers[(size_t) structure.oversamplingIndex];
    os.reset();
    sidechainOversamplers[(size_t) structure.oversamplingIndex].reset();

    peakLimiter  .setSampleRate (hostRate * factor);
    analogLimiter.prepare       (hostRate * factor);
    balance      .prepare       (hostRate * factor);
    finalLimiter .reset();

    for (auto& d : finalDetectors) d.reset();
    for (auto& a : finalAlign)     a.setDelay (TruePeakInterpolator::delay);

    compressor.reset();
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

void BreakwaterProcessor::updateSettings()
{
    const auto& style  = styles[(size_t) juce::jlimit (0, (int) styles.size() - 1, styleParam->getIndex())];
    const auto factor  = 1 << structure.oversamplingIndex;
    const auto innerRate = hostRate * factor;

    const auto lookaheadMs = (float) structure.lookaheadSamples * 1000.0f / (float) hostRate;
    const auto attackMs    = juce::jmin (attackParam->get(), lookaheadMs) * style.attackShare;
    const auto windowMs    = windowChoices[(size_t) juce::jlimit (0, (int) windowChoices.size() - 1, windowParam->getIndex())].milliseconds;
    const auto window      = juce::roundToInt (windowMs * 0.001 * innerRate);

    // Adaptive link: the more alike the two channels, the more they are
    // linked, so a mono-ish mix holds its centre while a wide one keeps the
    // independence the Link control gives it.
    const auto userLink = linkTransientsParam->get() * 0.01f;
    linkInUse = userLink;

    if (adaptiveLinkParam->get())
        linkInUse = userLink + (1.0f - userLink) * juce::jlimit (0.0f, 1.0f, (meters.correlation.load() - 0.5f) * 2.0f);

    PeakLimiter::Settings peak;
    peak.lookahead      = structure.lookaheadSamples * factor;
    peak.attack         = juce::roundToInt (attackMs * 0.001 * innerRate);
    peak.window         = window;
    peak.releaseMs      = releaseParam->get() * style.releaseScale;
    peak.adaptive       = style.adaptive;
    peak.bassAware      = style.bassAware;
    peak.linkTransients = linkInUse;
    peak.linkRelease    = linkReleaseParam->get() * 0.01f;
    peakLimiter.configure (peak);

    leveller.configure ({ style.levelRatio, style.levelHeadroomDb, style.levelAttackMs, style.levelReleaseMs });

    // The Attack control spans 0.01 to 10 ms; with no lookahead to hide it in,
    // the analog charge runs ten times quicker across the same travel.
    analogLimiter.configure ({ attackParam->get() * 0.1f, releaseParam->get(), linkInUse, window });

    Compressor::Settings comp;
    comp.thresholdDb = compThresholdParam->get();
    comp.ratio       = compOnParam->get() ? compRatioParam->get() : 1.0f;
    comp.attackMs    = compAttackParam->get();
    comp.releaseMs   = compReleaseParam->get();
    comp.highPassHz  = compHighPassParam->get();
    comp.mode        = static_cast<Compressor::Mode> (compModeParam->getIndex());
    compressor.configure (comp);

    clipper.setAmount (clipParam->get() * 0.01f);
    enhancer.setAmounts (transientsParam->get() * 0.01f, detailParam->get() * 0.01f, lowPunchParam->get() * 0.01f);
    balance.setAmount (balanceParam->get() * 0.01f);

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

void BreakwaterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto mainBus = getBusBuffer (buffer, true, 0);
    const auto numChannels = juce::jmin (2, mainBus.getNumChannels());

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    if (numChannels == 0 || buffer.getNumSamples() == 0 || ! prepared)
        return;

    // The sidechain is only used when it is switched on and actually connected.
    juce::AudioBuffer<float>* sidechain = nullptr;
    juce::AudioBuffer<float> sidechainBus;

    if (sidechainParam->get() && getBusCount (true) > 1 && getBus (true, 1)->isEnabled())
    {
        sidechainBus = getBusBuffer (buffer, true, 1);

        if (sidechainBus.getNumChannels() > 0)
            sidechain = &sidechainBus;
    }

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
    for (int start = 0; start < mainBus.getNumSamples(); start += blockSize)
    {
        const auto count = juce::jmin (blockSize, mainBus.getNumSamples() - start);
        juce::AudioBuffer<float> slice (mainBus.getArrayOfWritePointers(), numChannels, start, count);

        if (sidechain != nullptr)
        {
            juce::AudioBuffer<float> sideSlice (sidechain->getArrayOfWritePointers(),
                                                sidechain->getNumChannels(), start, count);
            processChain (slice, &sideSlice, numChannels);
        }
        else
        {
            processChain (slice, nullptr, numChannels);
        }
    }
}

void BreakwaterProcessor::processChain (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>* sidechainBuffer,
                                        int numChannels)
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

    // Correlation of the input, for the adaptive link and the display.
    if (numChannels == 2)
    {
        const auto* l = buffer.getReadPointer (0);
        const auto* r = buffer.getReadPointer (1);

        for (int i = 0; i < numSamples; ++i)
        {
            sumLR = l[i] * r[i] + correlationCoeff * (sumLR - l[i] * r[i]);
            sumLL = l[i] * l[i] + correlationCoeff * (sumLL - l[i] * l[i]);
            sumRR = r[i] * r[i] + correlationCoeff * (sumRR - r[i] * r[i]);
        }

        const auto energy = std::sqrt (sumLL * sumRR);
        meters.correlation.store (energy > 1.0e-10f ? juce::jlimit (-1.0f, 1.0f, sumLR / energy) : 1.0f);
    }
    else
    {
        meters.correlation.store (1.0f);
    }

    meters.link.store (linkInUse);

    // The external sidechain, scaled like the main signal, drives the
    // detectors in place of the programme.
    const auto useSidechain = sidechainBuffer != nullptr;

    if (useSidechain)
    {
        sidechainScratch.setSize (2, numSamples, false, false, true);

        for (int ch = 0; ch < numChannels; ++ch)
            sidechainScratch.copyFrom (ch, 0, *sidechainBuffer, juce::jmin (ch, sidechainBuffer->getNumChannels() - 1), 0, numSamples);
    }

    // -- Gain, DC filter, compressor, transients and detail, leveller --------
    std::array<float*, 2> data { buffer.getWritePointer (0), buffer.getWritePointer (numChannels > 1 ? 1 : 0) };
    std::array<float*, 2> side { sidechainScratch.getWritePointer (0), sidechainScratch.getWritePointer (numChannels > 1 ? 1 : 0) };
    std::array<float, 2> frame { };

    const auto dcOn = dcFilterParam->get();
    const auto compressorOn = compOnParam->get() && compressor.isActive();
    const auto enhancerOn = enhancer.isActive();
    const auto levellerOn = modern && leveller.isActive();
    float levellerReduction = 0.0f;
    float compressorReduction = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto scale = inputGain.getNextValue() / ceilingGain.getNextValue();

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

            if (useSidechain)
                side[(size_t) ch][i] *= scale;
        }

        if (compressorOn)
        {
            compressor.process (frame.data(), numChannels);
            compressorReduction = juce::jmax (compressorReduction, compressor.getReductionDb());
        }

        if (enhancerOn)
            enhancer.process (frame.data(), numChannels);

        if (levellerOn)
        {
            float loudest = 0.0f;

            for (int ch = 0; ch < numChannels; ++ch)
                loudest = juce::jmax (loudest, std::abs (useSidechain ? side[(size_t) ch][i] : frame[(size_t) ch]));

            const auto g = leveller.process (loudest);
            levellerReduction = juce::jmax (levellerReduction, leveller.getReductionDb());

            for (int ch = 0; ch < numChannels; ++ch)
            {
                frame[(size_t) ch] *= g;

                if (useSidechain)
                    side[(size_t) ch][i] *= g;
            }
        }

        for (int ch = 0; ch < numChannels; ++ch)
            data[(size_t) ch][i] = frame[(size_t) ch];
    }

    // -- Clipper and main limiter: oversampled -------------------------------
    const auto factor = 1 << structure.oversamplingIndex;
    auto& os = oversamplers[(size_t) structure.oversamplingIndex];

    const auto innerSamples = os.processUp (buffer.getArrayOfReadPointers(), numChannels, numSamples);
    std::array<float*, 2> up { os.getChannel (0), os.getChannel (numChannels > 1 ? 1 : 0) };

    std::array<float*, 2> sideUp { };

    if (useSidechain)
    {
        auto& sideOs = sidechainOversamplers[(size_t) structure.oversamplingIndex];
        sideOs.processUp (sidechainScratch.getArrayOfReadPointers(), numChannels, numSamples);
        sideUp = { sideOs.getChannel (0), sideOs.getChannel (numChannels > 1 ? 1 : 0) };
    }

    std::array<float, 2> detect { }, result { };
    const auto balanceOn = balance.isActive();

    std::fill (reductionTrace.begin(), reductionTrace.begin() + numSamples, 0.0f);

    for (int i = 0; i < innerSamples; ++i)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto x = clipper.process (up[(size_t) ch][i]);
            frame[(size_t) ch] = x;
            detect[(size_t) ch] = std::abs (useSidechain ? sideUp[(size_t) ch][i] : x);
        }

        float reduction;

        if (modern)
        {
            peakLimiter.process (frame.data(), detect.data(), result.data(), numChannels);
            reduction = peakLimiter.getReductionDb();

            if (balanceOn)
                for (int ch = 0; ch < numChannels; ++ch)
                    result[(size_t) ch] = balance.process (ch, peakLimiter.getChannelDelayed (ch),
                                                           peakLimiter.getChannelReductionDb (ch));
        }
        else
        {
            // The analog engine detects on what it is given; with an external
            // sidechain, the detector is fed that instead.
            if (useSidechain)
            {
                std::array<float, 2> sideFrame { sideUp[0][i], sideUp[1][i] };
                std::array<float, 2> sideOut { };
                analogLimiter.process (sideFrame.data(), sideOut.data(), numChannels);

                for (int ch = 0; ch < numChannels; ++ch)
                    result[(size_t) ch] = frame[(size_t) ch] * dbToGain (-analogLimiter.getChannelReductionDb (ch));
            }
            else
            {
                analogLimiter.process (frame.data(), result.data(), numChannels);
            }

            reduction = analogLimiter.getReductionDb();

            if (balanceOn)
                for (int ch = 0; ch < numChannels; ++ch)
                    result[(size_t) ch] = balance.process (ch, frame[(size_t) ch], analogLimiter.getChannelReductionDb (ch));
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

        float drivenPeak = 0.0f, outputPeak = 0.0f, power = 0.0f;
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
            power += y * y;
            outputPeak = juce::jmax (outputPeak, std::abs (y));
            outPeak[(size_t) ch] = juce::jmax (outPeak[(size_t) ch], std::abs (y));
            blockTruePeak = juce::jmax (blockTruePeak, outputTruePeak[(size_t) ch].push (y));
        }

        power /= (float) numChannels;
        vuPower = power + vuCoeff * (vuPower - power);

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
    meters.compressorReduction.push (compressorReduction);

    // VU of a sine reads its RMS plus 3 dB, so a 0 dBFS sine sits at +18 VU
    // when 0 VU is -18 dBFS.
    meters.vu.store (10.0f * std::log10 (juce::jmax (vuPower * 2.0f, 1.0e-12f)) - range::vuReferenceDb);

    samplesSinceLoudness += numSamples;

    if (samplesSinceLoudness >= juce::roundToInt (hostRate * 0.1))
    {
        samplesSinceLoudness = 0;
        publishLoudness();
    }
}

void BreakwaterProcessor::publishLoudness()
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

void BreakwaterProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void BreakwaterProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* BreakwaterProcessor::createEditor()
{
    return new BreakwaterEditor (*this);
}

} // namespace breakwater

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new breakwater::BreakwaterProcessor();
}
