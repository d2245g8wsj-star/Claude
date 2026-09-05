/*
    Renders the editor to a PNG without opening a window.

    Layout bugs are invisible in a compiler: a control can be positioned on top
    of another one, or off the panel entirely, and everything still builds and
    runs. This paints the real editor, with real parameter values, into an image
    so the layout can be looked at in CI or from a terminal.

    Usage:  crest_shot <output.png> [width]
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
                                            .getChildFile ("crest.png").getFullPathName());

    const auto width = argc > 2 ? juce::String (argv[2]).getIntValue() : crest::designWidth;

    crest::CrestProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    // Settings that show the controls doing something, rather than all at zero.
    auto set = [&processor] (const char* id, float value)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    set (crest::pid::input,   6.5f);
    set (crest::pid::ceiling, -0.30f);
    set (crest::pid::knee,    40.0f);
    set (crest::pid::bias,    -22.0f);
    set (crest::pid::colour,  55.0f);
    set (crest::pid::mix,     100.0f);
    set (crest::pid::output,  -1.0f);
    set (crest::pid::mode,    2.0f);
    set (crest::pid::harmonics, 30.0f);
    set (crest::pid::series,  -45.0f);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "no editor" << std::endl;
        return 1;
    }

    editor->setSize (width, juce::roundToInt ((double) width * crest::designHeight / crest::designWidth));

    // Feed the plugin while the message loop runs, so the meters and the curve
    // marker are showing a live signal at the moment the frame is taken rather
    // than whatever is left after their fall time.
    struct Settle final : private juce::Timer
    {
        Settle (crest::CrestProcessor& p, int ticksToWait)
            : processor (p), remaining (ticksToWait)
        {
            startTimerHz (30);
        }

        void timerCallback() override
        {
            juce::AudioBuffer<float> buffer (2, 512);
            juce::MidiBuffer midi;

            // A third of a second of audio per frame keeps the meters fed.
            for (int block = 0; block < 3; ++block)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < buffer.getNumSamples(); ++n)
                        buffer.setSample (ch, n, 0.62f * (float) std::sin (
                            juce::MathConstants<double>::twoPi * 110.0
                            * (double) (position + n) / 48000.0));

                processor.processBlock (buffer, midi);
                position += buffer.getNumSamples();
            }

            if (--remaining > 0)
                return;

            stopTimer();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }

        crest::CrestProcessor& processor;
        int remaining;
        int position { 0 };
    };

    Settle settle (processor, 20);
    juce::MessageManager::getInstance()->runDispatchLoop();

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true);

    if (! image.isValid())
    {
        std::cerr << "snapshot failed" << std::endl;
        return 1;
    }

    output.deleteFile();

    juce::FileOutputStream stream (output);

    if (! stream.openedOk())
    {
        std::cerr << "cannot write " << output.getFullPathName() << std::endl;
        return 1;
    }

    juce::PNGImageFormat png;

    if (! png.writeImageToStream (image, stream))
    {
        std::cerr << "encode failed" << std::endl;
        return 1;
    }

    std::cout << "wrote " << output.getFullPathName()
              << " (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;

    return 0;
}
