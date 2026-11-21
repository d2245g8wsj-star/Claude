#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace nazare
{

namespace
{
    constexpr int chunk = 64;            // control rate: settings are read every 64 samples
    const juce::StringArray consoleNames { "GLASS", "PIPELINE", "MAVERICKS", "JAWS", "TEAHUPO'O", "SUPERTUBES" };
    const double calValues[] = { -24.0, -18.0, -14.0, -12.0 };
}

NazareProcessor::NazareProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NAZARE", createParameterLayout())
{
    consoleParam = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (pid::console));
    osParam      = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (pid::oversampling));
    calParam     = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (pid::cal));
    modeParam    = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (pid::mode));
    bypassParam  = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter (pid::bypass));
    autoParam    = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter (pid::autoGain));
    matchedParam = dynamic_cast<juce::AudioParameterBool*>   (apvts.getParameter (pid::matched));
    channelParam = dynamic_cast<juce::AudioParameterInt*>    (apvts.getParameter (pid::channel));
    drive   = apvts.getRawParameterValue (pid::drive);
    spreadP = apvts.getRawParameterValue (pid::spread);
    outputP = apvts.getRawParameterValue (pid::output);
    ageP    = apvts.getRawParameterValue (pid::age);
    noiseP  = apvts.getRawParameterValue (pid::noise);
}

juce::AudioProcessorValueTreeState::ParameterLayout NazareProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto percent = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (juce::roundToInt (v * 100)) + " %"; });

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::console, 1 }, "Console", consoleNames, 3));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::drive, 1 }, "Drive", NormalisableRange<float> (0.0f, 1.0f), 0.49f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (zoneName (v)); })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::spread, 1 }, "Spread", NormalisableRange<float> (0.0f, 1.0f), 0.45f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::output, 1 }, "Output", NormalisableRange<float> (-12.0f, 12.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid::bypass, 1 }, "Bypass", false));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::age, 1 }, "Age", NormalisableRange<float> (0.0f, 1.0f), 0.2f, percent));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { pid::noise, 1 }, "Noise", NormalisableRange<float> (0.0f, 1.0f), 0.3f, percent));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::oversampling, 1 }, "Oversampling", StringArray { "1x", "2x", "4x", "8x" }, 2));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::cal, 1 }, "0 VU", StringArray { "-24 dBFS", "-18 dBFS", "-14 dBFS", "-12 dBFS" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::mode, 1 }, "Mode", StringArray { "Channel", "Bus" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid::autoGain, 1 }, "Auto Gain", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid::matched, 1 }, "Matched Pair", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { pid::channel, 1 }, "Channel", 1, 48, 7));
    return layout;
}

bool NazareProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    return in == out && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void NazareProcessor::prepareToPlay (double sampleRate, int)
{
    fs = sampleRate;
    console.prepare (sampleRate, chunk);
    bufL.assign (chunk, 0.0); bufR.assign (chunk, 0.0);
    int maxLatency = 0;
    for (int o = 0; o <= Console::maxOrder; ++o) maxLatency = juce::jmax (maxLatency, Console::latencyFor (o, sampleRate, chunk));
    dryLength = maxLatency + chunk + 1;
    dryL.assign ((size_t) dryLength, 0.0); dryR.assign ((size_t) dryLength, 0.0); dryWrite = 0;
    runningModel = consoleParam->getIndex(); pendingModel = -1;
    runningOrder = osParam->getIndex();
    console.setOrder (runningOrder);
    setLatencySamples (console.latency());
    wet = bypassParam->get() ? 0.0 : 1.0;
    smoothed = false;
}

Settings NazareProcessor::currentSettings() const
{
    Settings s;
    s.model    = runningModel < 0 ? consoleParam->getIndex() : runningModel;
    auto [d, a] = driveForKnob (s.model, drive->load());
    s.driveDb  = smoothed ? smoothDrive : d;
    s.autoGainDb = smoothed ? smoothAuto : a;
    s.autoGain = autoParam->get();
    s.spread   = spreadP->load();
    s.matched  = matchedParam->get();
    s.channel  = channelParam->get();
    s.age      = ageP->load();
    s.noise    = noiseP->load();
    s.calDbfs  = calValues[calParam->getIndex()];
    s.bus      = modeParam->getIndex() == 1;
    s.outputDb = outputP->load();
    return s;
}

void NazareProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)  { run (buffer); }
void NazareProcessor::processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) { run (buffer); }

template <typename T>
void NazareProcessor::run (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    const int numCh = juce::jmin (buffer.getNumChannels(), 2), n = buffer.getNumSamples();
    if (numCh == 0) return;
    double sumL = 0, sumR = 0, pk = 0;

    for (int start = 0; start < n; start += chunk)
    {
        const int len = juce::jmin (chunk, n - start);
        T* l = buffer.getWritePointer (0, start);
        T* r = numCh > 1 ? buffer.getWritePointer (1, start) : l;
        for (int i = 0; i < len; ++i) { bufL[(size_t) i] = (double) l[i]; bufR[(size_t) i] = (double) r[i]; }
        processChunk (bufL.data(), bufR.data(), len);
        for (int i = 0; i < len; ++i)
        {
            l[i] = (T) bufL[(size_t) i];
            if (numCh > 1) r[i] = (T) bufR[(size_t) i];
            sumL += bufL[(size_t) i] * bufL[(size_t) i]; sumR += bufR[(size_t) i] * bufR[(size_t) i];
            pk = std::max ({ pk, std::abs (bufL[(size_t) i]), std::abs (bufR[(size_t) i]) });
        }
    }

    meterL.store ((float) (sumL / juce::jmax (1, n)));
    meterR.store ((float) (sumR / juce::jmax (1, n)));
    peak.store ((float) pk);
}

void NazareProcessor::processChunk (double* l, double* r, int n)
{
    // console or oversampling changed: fade to the dry signal, change over, fade back
    const int wantModel = consoleParam->getIndex(), wantOrder = osParam->getIndex();
    if ((wantModel != runningModel || wantOrder != runningOrder) && pendingModel < 0) pendingModel = 1;

    const double target = (bypassParam->get() || pendingModel >= 0) ? 0.0 : 1.0;
    const double step = 1.0 / (0.01 * fs);   // 10 ms fades

    if (pendingModel >= 0 && wet <= 0.0)
    {
        if (wantOrder != runningOrder)
        {
            runningOrder = wantOrder;
            console.setOrder (runningOrder);
            setLatencySamples (console.latency());
        }
        runningModel = wantModel;
        console.reset();
        pendingModel = -1;
        smoothed = false;
    }

    // drive follows the knob through the console's table, smoothed over ~30 ms
    auto s = currentSettings();
    auto [d, a] = driveForKnob (s.model, drive->load());
    if (! smoothed) { smoothDrive = d; smoothAuto = a; smoothed = true; }
    const double k = 1.0 - std::exp (-(double) n / (0.03 * fs));
    smoothDrive += (d - smoothDrive) * k; smoothAuto += (a - smoothAuto) * k;
    s.driveDb = smoothDrive; s.autoGainDb = smoothAuto;

    // the dry path, delayed by the same latency as the console
    const int lat = console.latency();
    std::vector<double>* dry[2] = { &dryL, &dryR };
    double* io[2] = { l, r };
    double dryOut[2][chunk];
    for (int i = 0; i < n; ++i)
    {
        for (int c = 0; c < 2; ++c)
        {
            (*dry[c])[(size_t) dryWrite] = io[c][i];
            int rd = dryWrite - lat; if (rd < 0) rd += dryLength;
            dryOut[c][i] = (*dry[c])[(size_t) rd];
        }
        if (++dryWrite >= dryLength) dryWrite = 0;
    }

    console.process (l, r, n, s);

    for (int i = 0; i < n; ++i)
    {
        wet = target > wet ? std::min (target, wet + step) : std::max (target, wet - step);
        for (int c = 0; c < 2; ++c)
            io[c][i] = wet * io[c][i] + (1.0 - wet) * dryOut[c][i];
    }
}

void NazareProcessor::getStateInformation (juce::MemoryBlock& data)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("UI"), nullptr);
    state.appendChild (uiState.createCopy(), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, data);
}

void NazareProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;
        auto ui = state.getChildWithName ("UI");
        if (ui.isValid()) { uiState.copyPropertiesFrom (ui, nullptr); state.removeChild (ui, nullptr); }
        apvts.replaceState (state);
    }
}

juce::AudioProcessorEditor* NazareProcessor::createEditor() { return new NazareEditor (*this); }

} // namespace nazare

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new nazare::NazareProcessor(); }
