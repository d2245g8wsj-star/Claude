/*
    Renders the editor to a PNG without opening a window, so the layout can be
    looked at from a terminal or in CI.

    Usage:  transcender_shot <output.png> [width]
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
                                            .getChildFile ("transcender.png").getFullPathName());

    const auto width = argc > 2 ? juce::String (argv[2]).getIntValue() : transcender::designWidth;

    transcender::TranscenderProcessor processor;
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);

    auto set = [&processor] (const juce::String& id, float value)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    // The first factory sound, with the gate on so the step lights show.
    processor.setCurrentProgram (0);
    juce::ignoreUnused (set);

    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "no editor" << std::endl;
        return 1;
    }

    editor->setSize (width, juce::roundToInt ((double) width * transcender::designHeight / transcender::designWidth));

    // Feed the plugin while the message loop runs, so the editor has settled
    // when the frame is taken.
    struct Settle final : private juce::Timer
    {
        Settle (transcender::TranscenderProcessor& p, int ticksToWait) : processor (p), remaining (ticksToWait)
        {
            startTimerHz (30);
        }

        void timerCallback() override
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;

            for (int block = 0; block < 6; ++block)
            {
                midi.clear();
                if (n == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.9f), 0);
                n += buffer.getNumSamples();
                processor.processBlock (buffer, midi);
            }

            if (--remaining <= 0)
            {
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
            }
        }

        transcender::TranscenderProcessor& processor;
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
