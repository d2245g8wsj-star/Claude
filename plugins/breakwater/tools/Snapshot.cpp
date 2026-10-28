/*
    Renders the editor to a PNG without opening a window, so the layout can be
    looked at from a terminal or in CI.

    Usage:  breakwater_shot <output.png> [width]
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
                                            .getChildFile ("breakwater.png").getFullPathName());

    const auto width = argc > 2 ? juce::String (argv[2]).getIntValue() : breakwater::designWidth;

    breakwater::BreakwaterProcessor processor;
    processor.setPlayConfigDetails (2, 2, 48000.0, 512);

    auto set = [&processor] (const juce::String& id, float value)
    {
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    };

    // Settings that show the limiter working, rather than idling.
    set (breakwater::pid::gain,       9.0f);
    set (breakwater::pid::ceiling,    -1.0f);
    set (breakwater::pid::style,      4.0f);
    set (breakwater::pid::transients, 35.0f);
    set (breakwater::pid::detail,     20.0f);
    set (breakwater::pid::clip,       25.0f);

    processor.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());

    if (editor == nullptr)
    {
        std::cerr << "no editor" << std::endl;
        return 1;
    }

    editor->setSize (width, juce::roundToInt ((double) width * breakwater::designHeight / breakwater::designWidth));

    // Feed the plugin while the message loop runs, so the meters and lamps are
    // lit at the moment the frame is taken.
    struct Settle final : private juce::Timer
    {
        Settle (breakwater::BreakwaterProcessor& p, int ticksToWait) : processor (p), remaining (ticksToWait)
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
                    // A kick, a snare on the backbeat, a bass line and hats.
                    const auto t     = (double) n / 48000.0;
                    const auto twoPi = juce::MathConstants<double>::twoPi;
                    const auto beat  = std::fmod (t, 0.5);
                    const auto bar   = std::fmod (t, 1.0);
                    const auto kick  = 0.8 * std::exp (-beat * 16.0) * std::sin (twoPi * (48.0 + 110.0 * std::exp (-beat * 40.0)) * beat);
                    const auto snare = bar > 0.5 ? 0.5 * std::exp (-(bar - 0.5) * 22.0) * random.nextFloat() * 2.0 - 0.5 * std::exp (-(bar - 0.5) * 22.0) : 0.0;
                    const auto swell = 0.6 + 0.4 * std::sin (twoPi * 0.15 * t);
                    const auto bass  = 0.3 * swell * std::sin (twoPi * 55.0 * t);
                    const auto hats  = 0.06 * (random.nextFloat() * 2.0 - 1.0);

                    buffer.setSample (0, i, (float) (kick + snare + bass + hats));
                    buffer.setSample (1, i, (float) (kick + 0.8 * snare + bass - hats));
                }

                processor.processBlock (buffer, midi);
            }

            if (--remaining <= 0)
            {
                stopTimer();
                juce::MessageManager::getInstance()->stopDispatchLoop();
            }
        }

        breakwater::BreakwaterProcessor& processor;
        int remaining;
        long long n { 0 };
        juce::Random random { 42 };
    };

    Settle settle (processor, 120);
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
