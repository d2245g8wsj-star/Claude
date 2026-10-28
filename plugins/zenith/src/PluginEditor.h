#pragma once
#include "PluginProcessor.h"
namespace zenith {
inline constexpr int designWidth = 1200, designHeight = 640;
class ZenithEditor final : public juce::GenericAudioProcessorEditor
{
public:
    explicit ZenithEditor (ZenithProcessor& p) : juce::GenericAudioProcessorEditor (p) {}
};
}
