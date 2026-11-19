/*
    Renders the editor to a PNG without opening a window, so the layout can be
    looked at from a terminal or in CI.

    Usage:  lantern_shot <output.png> [scale] [stereo|dual|ms]

    "dual" shows dual mono split, the right channel's panel folded out; "ms"
    mid/side with the side's panel out.
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
                                            .getChildFile ("lantern.png").getFullPathName());

    const auto scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.0f;
    const auto view  = argc > 3 ? juce::String (argv[3]) : juce::String ("stereo");

    lantern::LanternProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);

    auto set = [&processor] (const juce::String& id, float value)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    // Settings that show the controls doing something, rather than all at rest.
    using lantern::pid::band;

    set (lantern::pid::mode,   (float) lantern::ChannelMode::stereo);
    set (lantern::pid::hpfIn,  1.0f);
    set (lantern::pid::hpf,    28.0f);
    set (band (0, "gain"),     4.0f);
    set (band (0, "freq"),     3.0f);    // 100 Hz shelf
    set (band (1, "gain"),     -3.0f);
    set (band (1, "freq"),     4.0f);    // 430 Hz
    set (band (1, "q"),        0.7f);
    set (band (2, "gain"),     2.5f);
    set (band (2, "freq"),     7.0f);    // 3.8 kHz
    set (band (2, "q"),        2.2f);
    set (band (3, "gain"),     5.0f);
    set (band (3, "freq"),     10.0f);   // 18 kHz shelf
    set (lantern::pid::thd,    35.0f);
    set (lantern::pid::outputIron, 1.5f);
    set (lantern::pid::iron,   1.0f);
    set (lantern::pid::ironPad, 2.0f);

    if (view == "ms" || view == "dual")
    {
        set (lantern::pid::mode, (float) (view == "ms" ? lantern::ChannelMode::midSide : lantern::ChannelMode::dualMono));
        set (lantern::pid::dualSplit, view == "dual" ? 1.0f : 0.0f);
        set (lantern::pid::side (lantern::pid::hpfIn), 1.0f);
        set (lantern::pid::side (lantern::pid::hpf),   150.0f);
        set (lantern::pid::side (lantern::pid::hpfSteep), 1.0f);
        set (lantern::pid::side (band (3, "gain")),    7.0f);
        set (lantern::pid::side (band (3, "freq")),    8.0f);
        set (lantern::pid::side (band (2, "gain")),    -2.0f);
        set (lantern::pid::side (lantern::pid::gainRange), 0.0f);
    }

    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "no editor" << std::endl;
        return 1;
    }

    // Let the editor settle its fold first, then draw it at the scale asked for.
    const auto design = dynamic_cast<lantern::LanternEditor&> (*editor).designSize();
    editor->setSize (juce::roundToInt ((float) design.x * scale), juce::roundToInt ((float) design.y * scale));

    // Feed the plugin while the message loop runs, so the meters and lamps are
    // lit at the moment the frame is taken.
    struct Settle final : private juce::Timer
    {
        Settle (lantern::LanternProcessor& p, int ticksToWait) : processor (p), remaining (ticksToWait)
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

        lantern::LanternProcessor& processor;
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
