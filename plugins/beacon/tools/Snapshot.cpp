/*
    Renders the editor to a PNG without opening a window, so the layout can be
    looked at from a terminal or in CI.

    Usage:  beacon_shot <output.png> [width] [side]

    With "side", the panel is shown in mid/side mode with the side's
    controls on show.
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::File output (argc > 1 ? juce::String (argv[1])
                                      : juce::File::getCurrentWorkingDirectory()
                                            .getChildFile ("beacon.png").getFullPathName());

    const auto width = argc > 2 ? juce::String (argv[2]).getIntValue() : beacon::designWidth;

    beacon::BeaconProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);

    auto set = [&processor] (const juce::String& id, float value)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    // Settings that show the controls doing something, rather than all at rest.
    set (beacon::pid::mode,      (float) beacon::ChannelMode::stereo);
    set (beacon::pid::input,     3.0f);
    set (beacon::pid::threshold, -30.0f);
    set (beacon::pid::attack,    8.0f);
    set (beacon::pid::release,   0.6f);
    set (beacon::pid::hpfIn,     1.0f);
    set (beacon::pid::hpf,       90.0f);
    set (beacon::pid::thd,       2.5f);
    set (beacon::pid::tube,      6.0f);
    set (beacon::pid::mix,       80.0f);
    set (beacon::pid::iron,      1.0f);
    set (beacon::pid::output,    4.0f);
    set (beacon::pid::ironPad,   3.0f);

    if (argc > 3 && juce::String (argv[3]) == "side")
    {
        set (beacon::pid::mode, (float) beacon::ChannelMode::midSide);
        set (beacon::pid::side (beacon::pid::threshold), -18.0f);
        set (beacon::pid::side (beacon::pid::thd),       5.0f);
        set (beacon::pid::side (beacon::pid::feedFwd),   1.0f);
        processor.apvts.state.setProperty ("editorView", 1, nullptr);
    }

    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "no editor" << std::endl;
        return 1;
    }

    editor->setSize (width, juce::roundToInt ((double) width * beacon::designHeight / beacon::designWidth));

    // Feed the plugin while the message loop runs, so the meters and lamps are
    // lit at the moment the frame is taken.
    struct Settle final : private juce::Timer
    {
        Settle (beacon::BeaconProcessor& p, int ticksToWait) : processor (p), remaining (ticksToWait)
        {
            startTimerHz (30);
        }

        void timerCallback() override
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;

            for (int block = 0; block < 6; ++block)
            {
                for (int i = 0; i < buffer.getNumSamples(); ++i, ++n)
                {
                    const auto t = (double) n / 48000.0;
                    buffer.setSample (0, i, (float) (0.45 * std::sin (juce::MathConstants<double>::twoPi * 110.0 * t)
                                                   + 0.15 * std::sin (juce::MathConstants<double>::twoPi * 1700.0 * t)));
                    buffer.setSample (1, i, (float) (0.30 * std::sin (juce::MathConstants<double>::twoPi * 440.0 * t)));
                }

                processor.processBlock (buffer, midi);
            }

            if (--remaining <= 0)
            {
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
            }
        }

        beacon::BeaconProcessor& processor;
        int remaining;
        long long n { 0 };
    };

    Settle settle (processor, 12);
    juce::MessageManager::getInstance()->runDispatchLoop();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);

    output.deleteFile();
    juce::FileOutputStream stream (output);
    juce::PNGImageFormat png;

    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::cerr << "could not write " << output.getFullPathName() << std::endl;
        return 1;
    }

    std::cout << "wrote " << output.getFullPathName() << " (" << image.getWidth() << " x " << image.getHeight() << ")" << std::endl;
    return 0;
}
