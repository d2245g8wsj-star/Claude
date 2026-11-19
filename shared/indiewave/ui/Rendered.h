#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/**
    The range's rendered panels: a plate and a filmstrip for each kind of moving
    part, rendered in Blender by shared/indiewave/art and compiled into the plugin
    with its panel.json. The editor builds its controls from that file, so a
    control sits exactly where the render put its shadow.
*/
namespace indiewave::ui::rendered
{

/** Looks a resource up by its BinaryData name (e.g. "panel_json"). */
using Lookup = std::function<const char* (const char* name, int& size)>;

class Art
{
public:
    explicit Art (Lookup);

    struct Sheet
    {
        juce::Image image;
        juce::String type;
        float w = 0, h = 0;     ///< one frame, in panel points
        int n = 1, cols = 1;
        void draw (juce::Graphics&, int frame, juce::Point<float> centre, float opacity = 1.0f) const;
    };

    struct Control
    {
        juce::String id, kind, face;
        juce::Point<float> centre;
    };

    juce::Image plate (const juce::String& face) const;
    juce::Point<int> faceSize (const juce::String& face) const;
    const Sheet* sheet (const juce::String& kind) const;
    const std::vector<Control>& controls() const noexcept { return controlList; }
    const Control* control (const juce::String& id) const;

    /** Fixed parts the editor draws into: windows and meters, by id. */
    juce::var part (const juce::String& face, const juce::String& id) const;

    float scale() const noexcept { return imageScale; }

private:
    Lookup lookup;
    juce::var meta;
    float imageScale = 2.0f;
    std::map<juce::String, Sheet> sheets;
    std::vector<Control> controlList;
    mutable std::map<juce::String, juce::Image> plates;
};

/** A knob or selector played from its filmstrip, one frame per step or a sweep. */
class Knob final : public juce::Slider
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, const Art::Sheet&);

    void paint (juce::Graphics&) override;
    bool hitTest (int x, int y) override;

private:
    const Art::Sheet& sheet;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Knob)
};

/** A toggle or a push key: frame 0 off, frame 1 on. */
class Switch final : public juce::Button
{
public:
    Switch (juce::AudioProcessorValueTreeState&, const juce::String& parameterID, const Art::Sheet&);
    Switch (const Art::Sheet&);    ///< not bound to a parameter

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    bool hitTest (int x, int y) override;

private:
    const Art::Sheet& sheet;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Switch)
};

/** A jewel lamp: frame 0 dark, frame 1 lit. */
class Lamp final : public juce::Component
{
public:
    explicit Lamp (const Art::Sheet&);
    void setLit (bool);
    void paint (juce::Graphics&) override;

private:
    const Art::Sheet& sheet;
    bool lit { false };
};

/**
    A face: its plate, and a control for every entry in panel.json that sits on
    it, bound to the parameter of the same id. Controls whose id is not a
    parameter are left for the editor to create (lamps, keys with actions).
*/
class Face : public juce::Component
{
public:
    Face (const Art&, juce::AudioProcessorValueTreeState&, const juce::String& faceName);

    void paint (juce::Graphics&) override;

    juce::Component* find (const juce::String& id) const;
    template <typename T> T* get (const juce::String& id) const { return dynamic_cast<T*> (find (id)); }

    /** Places an editor-made component on its control's spot. */
    void place (juce::Component&, const juce::String& id);

    const Art& art;
    const juce::String faceName;

private:
    juce::Image plateImage;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::map<juce::String, juce::Component*> byId;
};

} // namespace indiewave::ui::rendered
