/*
    Renders the editor to a PNG without opening a window.

    Usage:  nazare_shot <output.png> [width] [rear]
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <iostream>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    const juce::File output (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("nazare.png").getFullPathName());
    const int width = argc > 2 ? juce::String (argv[2]).getIntValue() : 1600;
    const bool rear = argc > 3 && juce::String (argv[3]) == "rear";

    nazare::NazareProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.uiState.setProperty ("rear", rear, nullptr);
    processor.prepareToPlay (48000.0, 512);

    // a 1 kHz tone at 0 VU so the meter shows something
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    for (int block = 0, n = 0; block < 40; ++block)
    {
        for (int i = 0; i < 512; ++i, ++n)
            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, i, (float) (0.126 * std::sin (juce::MathConstants<double>::twoPi * 1000.0 * n / 48000.0)));
        processor.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (width, juce::roundToInt (width * 440.0 / 1600.0));

    struct Settle final : private juce::Timer
    {
        explicit Settle (int ticks) : remaining (ticks) { startTimerHz (30); }
        void timerCallback() override { if (--remaining <= 0) { stopTimer(); juce::MessageManager::getInstance()->stopDispatchLoop(); } }
        int remaining;
    } settle (20);
    juce::MessageManager::getInstance()->runDispatchLoop();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
    output.deleteFile();
    juce::FileOutputStream stream (output);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream)) { std::cerr << "could not write " << output.getFullPathName() << std::endl; return 1; }
    std::cout << "wrote " << output.getFullPathName() << std::endl;
    return 0;
}
