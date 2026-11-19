#include "Rendered.h"

namespace indiewave::ui::rendered
{

namespace
{
    juce::String resourceName (juce::String file)
    {
        return file.replaceCharacter ('.', '_').replaceCharacter ('-', '_');
    }

    juce::Rectangle<int> around (juce::Point<float> centre, float w, float h)
    {
        return juce::Rectangle<float> (w, h).withCentre (centre).toNearestInt();
    }
}

// ---------------------------------------------------------------------------

Art::Art (Lookup l) : lookup (std::move (l))
{
    int size = 0;
    if (const auto* data = lookup ("panel_json", size))
        meta = juce::JSON::parse (juce::String::fromUTF8 (data, size));

    jassert (meta.isObject());
    imageScale = (float) (double) meta.getProperty ("scale", 2.0);

    if (auto* kinds = meta["kinds"].getDynamicObject())
    {
        for (const auto& kind : kinds->getProperties())
        {
            Sheet s;
            s.type = kind.value["type"].toString();
            s.w = (float) (double) kind.value["w"];
            s.h = (float) (double) kind.value["h"];
            s.n = (int) kind.value["n"];
            s.cols = (int) kind.value["cols"];

            int bytes = 0;
            if (const auto* data = lookup (resourceName (kind.name.toString() + ".png").toRawUTF8(), bytes))
                s.image = juce::ImageCache::getFromMemory (data, bytes);

            jassert (s.image.isValid());
            sheets[kind.name.toString()] = std::move (s);
        }
    }

    if (auto* list = meta["controls"].getArray())
        for (const auto& c : *list)
            controlList.push_back ({ c["id"].toString(), c["kind"].toString(), c["face"].toString(),
                                     { (float) (double) c["cx"], (float) (double) c["cy"] } });
}

juce::Image Art::plate (const juce::String& face) const
{
    if (auto it = plates.find (face); it != plates.end())
        return it->second;

    int bytes = 0;
    juce::Image image;
    if (const auto* data = lookup (resourceName ("plate_" + face + ".jpg").toRawUTF8(), bytes))
        image = juce::ImageCache::getFromMemory (data, bytes);

    plates[face] = image;
    return image;
}

juce::Point<int> Art::faceSize (const juce::String& face) const
{
    const auto size = meta["faces"][juce::Identifier (face)]["size"];
    return { (int) size[0], (int) size[1] };
}

const Art::Sheet* Art::sheet (const juce::String& kind) const
{
    auto it = sheets.find (kind);
    return it == sheets.end() ? nullptr : &it->second;
}

const Art::Control* Art::control (const juce::String& id, const juce::String& face) const
{
    for (const auto& c : controlList)
        if (c.id == id && (face.isEmpty() || c.face == face))
            return &c;
    return nullptr;
}

juce::var Art::part (const juce::String& face, const juce::String& id) const
{
    if (auto* list = meta["parts"][juce::Identifier (face)].getArray())
        for (const auto& p : *list)
            if (p["id"].toString() == id)
                return p;
    return {};
}

void Art::Sheet::draw (juce::Graphics& g, int frame, juce::Point<float> centre, float opacity) const
{
    if (! image.isValid())
        return;

    frame = juce::jlimit (0, n - 1, frame);
    const auto fw = image.getWidth() / cols;
    const auto rows = (n + cols - 1) / cols;
    const auto fh = image.getHeight() / rows;
    const auto src = juce::Rectangle<int> ((frame % cols) * fw, (frame / cols) * fh, fw, fh);
    const auto dst = juce::Rectangle<float> (w, h).withCentre (centre);

    g.setOpacity (opacity);
    g.drawImage (image, dst.getX(), dst.getY(), dst.getWidth(), dst.getHeight(),
                 src.getX(), src.getY(), src.getWidth(), src.getHeight());
    g.setOpacity (1.0f);
}

// ---------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, const Art::Sheet& s)
    : sheet (s),
      attachment (state, parameterID, *this)
{
    setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    setVelocityBasedMode (false);

    // A switch with a few positions moves a step per finger's width; a sweep
    // takes a long drag end to end.
    setMouseDragSensitivity (sheet.n <= 12 ? juce::jmax (60, sheet.n * 26) : 240);

    if (auto* parameter = state.getParameter (parameterID))
    {
        setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        setTooltip (parameter->getName (64));
    }

    setPopupDisplayEnabled (true, true, nullptr, 900);
}

void Knob::paint (juce::Graphics& g)
{
    const auto proportion = valueToProportionOfLength (getValue());
    const auto frame = juce::roundToInt (proportion * (sheet.n - 1));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    sheet.draw (g, frame, getLocalBounds().toFloat().getCentre(), isEnabled() ? 1.0f : 0.55f);
}

bool Knob::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= juce::jmin (sheet.w, sheet.h) * 0.38f;
}

// ---------------------------------------------------------------------------

Switch::Switch (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, const Art::Sheet& s)
    : juce::Button (parameterID), sheet (s)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, parameterID, *this);
    if (auto* parameter = state.getParameter (parameterID))
        setTooltip (parameter->getName (64));
}

Switch::Switch (const Art::Sheet& s) : juce::Button ({}), sheet (s) {}

void Switch::paintButton (juce::Graphics& g, bool, bool down)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    const auto on = sheet.type == "key" ? (getToggleState() || down) : getToggleState();
    sheet.draw (g, on ? 1 : 0, getLocalBounds().toFloat().getCentre(), isEnabled() ? 1.0f : 0.55f);
}

bool Switch::hitTest (int x, int y)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    return c.getDistanceFrom ({ (float) x, (float) y }) <= juce::jmin (sheet.w, sheet.h) * 0.42f;
}

// ---------------------------------------------------------------------------

Lamp::Lamp (const Art::Sheet& s) : sheet (s)
{
    setInterceptsMouseClicks (false, false);
}

void Lamp::setLevel (float l)
{
    l = juce::jlimit (0.0f, 1.0f, l);
    if (std::abs (l - level) > 0.01f)
    {
        level = l;
        repaint();
    }
}

void Lamp::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    sheet.draw (g, 0, c);
    if (level > 0.0f)
        sheet.draw (g, 1, c, level);
}

// ---------------------------------------------------------------------------

Selector::Selector (const Art::Sheet& s) : sheet (s) {}

void Selector::setIndex (int i)
{
    i = juce::jlimit (0, sheet.n - 1, i);
    if (i != index)
    {
        index = i;
        repaint();
    }
}

void Selector::choose (int i)
{
    i = juce::jlimit (0, sheet.n - 1, i);
    if (i != index)
    {
        setIndex (i);
        if (onChange)
            onChange (index);
    }
}

void Selector::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    sheet.draw (g, index, getLocalBounds().toFloat().getCentre());
}

void Selector::mouseDown (const juce::MouseEvent&)
{
    dragStart = index;
    dragged = false;
}

void Selector::mouseDrag (const juce::MouseEvent& e)
{
    const auto steps = juce::roundToInt ((float) (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 28.0f);
    if (steps != 0)
        dragged = true;
    choose (dragStart + steps);
}

void Selector::mouseUp (const juce::MouseEvent&)
{
    if (! dragged)
        choose ((index + 1) % sheet.n);
}

// ---------------------------------------------------------------------------

VuNeedles::VuNeedles (const Art& art, const juce::String& face, const juce::String& id, float ref)
    : reference (ref)
{
    setInterceptsMouseClicks (false, false);
    const auto p = art.part (face, id);
    const auto fw = (float) (double) p["face"][0], fh = (float) (double) p["face"][1];
    const auto cx = (float) (double) p["cx"], cy = (float) (double) p["cy"];
    setBounds (juce::Rectangle<float> (fw, fh).withCentre ({ cx, cy }).toNearestInt());
    pivotX = (float) (double) p["pivot"][0];
    pivotY = (float) (double) p["pivot"][1];
    radius = (float) (double) p["r"];
}

void VuNeedles::setNeedles (int count)
{
    needles = juce::jlimit (1, 2, count);
    repaint();
}

void VuNeedles::push (float a, float b)
{
    // Moving coil ballistics, about 300 ms to settle at 30 frames a second.
    const float in[2] { a - reference, b - reference };
    for (int i = 0; i < 2; ++i)
        shown[(size_t) i] += (juce::jlimit (-22.0f, 3.5f, in[i]) - shown[(size_t) i]) * 0.3f;
    repaint();
}

void VuNeedles::paint (juce::Graphics& g)
{
    // The printed scale: the angle for a reading v in VU, as prints.html draws it.
    const auto angle = [] (float v)
    {
        const auto t = juce::jlimit (0.0f, 1.1f, (v + 20.0f) / 23.0f);
        return juce::degreesToRadians (-44.0f + 88.0f * std::pow (t, 1.35f));
    };

    g.reduceClipRegion (getLocalBounds());
    const juce::Colour colours[] { juce::Colour (0xff1f2d3d), juce::Colour (0xffb8352a) };

    for (int i = needles - 1; i >= 0; --i)
    {
        const auto a = angle (shown[(size_t) i]);
        const juce::Point<float> pivot (pivotX, pivotY);
        const auto tip = pivot + juce::Point<float> (std::sin (a), -std::cos (a)) * (radius + 6.0f);
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.drawLine ({ pivot.translated (2.0f, 2.5f), tip.translated (2.0f, 2.5f) }, 1.6f);
        g.setColour (colours[i]);
        g.drawLine ({ pivot, tip }, 1.4f);
    }
}

// ---------------------------------------------------------------------------

Face::Face (const Art& a, juce::AudioProcessorValueTreeState& state, const juce::String& name)
    : art (a), faceName (name)
{
    const auto size = art.faceSize (faceName);
    setSize (size.x, size.y);
    plateImage = art.plate (faceName);

    for (const auto& c : art.controls())
    {
        if (c.face != faceName || state.getParameter (c.id) == nullptr)
            continue;

        const auto* sheet = art.sheet (c.kind);
        if (sheet == nullptr)
            continue;

        std::unique_ptr<juce::Component> component;
        if (sheet->type == "toggle" || sheet->type == "key")
            component = std::make_unique<Switch> (state, c.id, *sheet);
        else if (sheet->type == "jewel")
            continue;
        else
            component = std::make_unique<Knob> (state, c.id, *sheet);

        component->setBounds (around (c.centre, sheet->w, sheet->h));
        addAndMakeVisible (*component);
        byId[c.id] = component.get();
        owned.push_back (std::move (component));
    }
}

void Face::paint (juce::Graphics& g)
{
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    if (plateImage.isValid())
        g.drawImage (plateImage, getLocalBounds().toFloat());
}

juce::Component* Face::find (const juce::String& id) const
{
    auto it = byId.find (id);
    return it == byId.end() ? nullptr : it->second;
}

void Face::place (juce::Component& component, const juce::String& id)
{
    if (const auto* c = art.control (id, faceName))
        if (const auto* sheet = art.sheet (c->kind))
        {
            component.setBounds (around (c->centre, sheet->w, sheet->h));
            addAndMakeVisible (component);
            byId[id] = &component;
        }
}

// ---------------------------------------------------------------------------

Follow::Follow (juce::Slider& knob, juce::RangedAudioParameter* enableParameter,
                std::vector<std::pair<juce::RangedAudioParameter*, float>> targets,
                std::function<bool()> condition)
    : source (knob),
      enable (enableParameter),
      followers (std::move (targets)),
      extraCondition (std::move (condition))
{
    source.onDragStart   = [this] { begin(); };
    source.onValueChange = [this] { update(); };
    source.onDragEnd     = [this] { end(); };
}

void Follow::begin()
{
    dragging = enable != nullptr && enable->getValue() >= 0.5f && (! extraCondition || extraCondition());
    if (! dragging)
        return;

    sourceStart = (float) source.getValue();
    followerStart.clear();
    for (auto& [parameter, sign] : followers)
    {
        followerStart.push_back (parameter->convertFrom0to1 (parameter->getValue()));
        parameter->beginChangeGesture();
    }
}

void Follow::update()
{
    if (! dragging)
        return;

    const auto travel = (float) source.getValue() - sourceStart;
    for (size_t i = 0; i < followers.size(); ++i)
    {
        auto* parameter = followers[i].first;
        const auto target = parameter->getNormalisableRange().snapToLegalValue (followerStart[i] + followers[i].second * travel);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (target));
    }
}

void Follow::end()
{
    if (! dragging)
        return;

    for (auto& [parameter, sign] : followers)
        parameter->endChangeGesture();
    dragging = false;
}

} // namespace indiewave::ui::rendered
