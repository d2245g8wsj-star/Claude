/*
    Renders the editor to a PNG without opening a window, so the layout can be
    looked at from a terminal or in CI.

    Usage:  lacquer_shot <output.png> [width] [white|black]
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
                                            .getChildFile ("lacquer.png").getFullPathName());

    const auto width = argc > 2 ? juce::String (argv[2]).getIntValue() : lacquer::designWidth;

    lacquer::LacquerProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);

    auto set = [&processor] (const juce::String& id, float value)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    // Settings that show the controls doing something, rather than all at zero.
    using lacquer::pid::forStrip;
    set (lacquer::pid::link, 0.0f);
    set (forStrip (0, lacquer::pid::lowBoostFreq),      1.0f);
    set (forStrip (0, lacquer::pid::lowBoostAmount),    12.0f);
    set (forStrip (0, lacquer::pid::lowCutFreq),        3.0f);
    set (forStrip (0, lacquer::pid::lowCutAmount),      8.0f);
    set (forStrip (0, lacquer::pid::midBoostFreq),      6.0f);
    set (forStrip (0, lacquer::pid::midBoostAmount),    9.0f);
    set (forStrip (0, lacquer::pid::midCutFreq),        3.0f);
    set (forStrip (0, lacquer::pid::midCutAmount),      7.0f);
    set (forStrip (0, lacquer::pid::trebleBoostFreq),   5.0f);
    set (forStrip (0, lacquer::pid::trebleQ),           3.0f);
    set (forStrip (0, lacquer::pid::trebleBoostAmount), 11.0f);
    set (forStrip (0, lacquer::pid::trebleCutFreq),     4.0f);
    set (forStrip (0, lacquer::pid::trebleCutAmount),   4.0f);
    set (forStrip (0, lacquer::pid::shift),             1.0f);
    set (forStrip (1, lacquer::pid::lowBoostAmount),    6.0f);
    set (forStrip (1, lacquer::pid::trebleBoostFreq),   8.0f);
    set (forStrip (1, lacquer::pid::trebleBoostAmount), 8.0f);
    set (forStrip (1, lacquer::pid::transformer),       0.0f);

    if (argc > 3 && juce::String (argv[3]) == "black")
        processor.setFinish (1);

    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "no editor" << std::endl;
        return 1;
    }

    editor->setSize (width, juce::roundToInt ((double) width * lacquer::designHeight / lacquer::designWidth));

    // Run the plugin while the message loop runs, as a host would.
    struct Settle final : private juce::Timer
    {
        Settle (lacquer::LacquerProcessor& p, int ticksToWait) : processor (p), remaining (ticksToWait)
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
                    buffer.setSample (0, i, (float) (0.55 * std::sin (juce::MathConstants<double>::twoPi * 70.0 * t)
                                                   + 0.20 * std::sin (juce::MathConstants<double>::twoPi * 9000.0 * t)));
                    buffer.setSample (1, i, (float) (0.12 * std::sin (juce::MathConstants<double>::twoPi * 2500.0 * t)));
                }

                processor.processBlock (buffer, midi);
            }

            if (--remaining <= 0)
            {
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
            }
        }

        lacquer::LacquerProcessor& processor;
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
