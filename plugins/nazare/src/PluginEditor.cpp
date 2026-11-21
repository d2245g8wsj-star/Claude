#include "PluginEditor.h"
#include "BinaryData.h"
#include "gen/Layout.h"

namespace nazare
{

namespace L = layout;

namespace
{
    const juce::Colour navy { 0xff1f2d3d }, cream { 0xfff6eedc }, red { 0xffb8352a };

    juce::Image load (const char* data, int size) { return juce::ImageCache::getFromMemory (data, size); }

    juce::Point<float> polar (float cx, float cy, float r, float deg)
    {
        const float a = juce::degreesToRadians (deg - 90.0f);
        return { cx + r * std::cos (a), cy + r * std::sin (a) };
    }

    /** One filmstrip: a grid of rendered frames at 2x. */
    struct Film
    {
        juce::Image sheet;
        L::Strip strip;

        void draw (juce::Graphics& g, int frame, juce::Point<float> centre) const
        {
            if (sheet.isNull()) return;
            frame = juce::jlimit (0, strip.n - 1, frame);
            const int k = L::imageScale;
            const int sw = juce::roundToInt (strip.w * k), sh = juce::roundToInt (strip.h * k);
            const int sx = (frame % strip.cols) * sw, sy = (frame / strip.cols) * sh;
            g.drawImage (sheet, juce::roundToInt (centre.x - strip.w / 2), juce::roundToInt (centre.y - strip.h / 2),
                         juce::roundToInt (strip.w), juce::roundToInt (strip.h), sx, sy, sw, sh);
        }
    };
}

// ---------------------------------------------------------------------------
/** The small label that shows a control's setting while it is touched. */
class Tip final : public juce::Component
{
public:
    Tip() { setInterceptsMouseClicks (false, false); }

    void show (const juce::String& t, juce::Point<float> at) { text = t; where = at; visible = true; repaint(); }
    void hide() { visible = false; repaint(); }

    void paint (juce::Graphics& g) override
    {
        if (! visible || text.isEmpty()) return;
        const auto font = juce::Font (juce::FontOptions (10.5f, juce::Font::bold)).withExtraKerningFactor (0.18f);
        const float w = juce::GlyphArrangement::getStringWidth (font, text) + 20.0f, h = 19.0f;
        auto r = juce::Rectangle<float> (w, h).withCentre (where);
        g.setColour (navy.withAlpha (0.93f)); g.fillRoundedRectangle (r, 2.0f);
        g.setColour (cream); g.setFont (font); g.drawText (text, r, juce::Justification::centred);
    }

private:
    juce::String text; juce::Point<float> where; bool visible = false;
};

// ---------------------------------------------------------------------------
/** A rendered control tied to a parameter. Drag up and down; shift for fine;
    double click resets; the wheel works too. */
class Control : public juce::Component
{
public:
    Control (juce::RangedAudioParameter& p, Film f, juce::Point<float> c, float radius, Tip& t)
        : param (p), film (std::move (f)), centre (c), tip (t),
          attachment (p, [this] (float) { repaint(); })
    {
        setBounds (juce::Rectangle<int> ((int) (radius * 2), (int) (radius * 2)).withCentre (c.toInt()));
        attachment.sendInitialUpdate();
        setWantsKeyboardFocus (true);
        setTitle (p.getName (64));
    }

    std::function<int (float normalised)> frameFor;          // which frame shows a setting
    std::function<juce::String()> describe;                  // what the tip says
    std::function<float (float, float)> stepper;             // discrete: next value for a drag
    float travel = 260.0f;
    juce::Point<float> tipAt;

    float value() const { return param.getValue(); }
    void set (float normalised) { attachment.setValueAsPartOfGesture (param.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, normalised))); }

    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        const auto frame = frameFor ? frameFor (value()) : juce::roundToInt (value() * (float) (film.strip.n - 1));
        film.draw (g, frame, (centre - getPosition().toFloat()));
    }

    bool hitTest (int x, int y) override
    {
        return juce::Point<float> ((float) x, (float) y).getDistanceFrom ({ getWidth() / 2.0f, getHeight() / 2.0f }) <= getWidth() / 2.0f;
    }

    void mouseEnter (const juce::MouseEvent&) override { showTip(); }
    void mouseExit (const juce::MouseEvent&) override  { if (! dragging) tip.hide(); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = true; startValue = value(); startY = e.position.y; lastStep = 0;
        attachment.beginGesture(); showTip();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const float dy = startY - e.position.y;
        if (stepper) { const int st = (int) std::round (dy / 30.0f); if (st != lastStep) { set (stepper (startValue, (float) st)); lastStep = st; } }
        else set (startValue + dy / (e.mods.isShiftDown() ? travel * 10.0f : travel));
        showTip();
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; attachment.endGesture(); if (! isMouseOver()) tip.hide(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { attachment.setValueAsCompleteGesture (param.convertFrom0to1 (param.getDefaultValue())); showTip(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float d = (w.deltaY > 0 ? 1.0f : -1.0f) * (stepper ? 1.0f : (e.mods.isShiftDown() ? 0.002f : 0.02f));
        attachment.beginGesture(); set (stepper ? stepper (value(), d) : value() + d); attachment.endGesture(); showTip();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const float d = stepper ? 1.0f : (k.getModifiers().isShiftDown() ? 0.002f : 0.02f);
        int dir = 0;
        if (k == juce::KeyPress::upKey || k == juce::KeyPress::rightKey) dir = 1;
        if (k == juce::KeyPress::downKey || k == juce::KeyPress::leftKey) dir = -1;
        if (dir == 0) return false;
        attachment.beginGesture(); set (stepper ? stepper (value(), (float) dir) : value() + (float) dir * d); attachment.endGesture(); showTip();
        return true;
    }

    juce::RangedAudioParameter& param;

private:
    void showTip() { tip.show (describe ? describe() : param.getCurrentValueAsText(), tipAt); }

    Film film;
    juce::Point<float> centre;
    Tip& tip;
    juce::ParameterAttachment attachment;
    bool dragging = false; float startValue = 0, startY = 0; int lastStep = 0;
};

// ---------------------------------------------------------------------------
/** A bat-handle toggle: frame 0 lever up, frame 1 lever down. */
class Toggle final : public juce::Component
{
public:
    Toggle (juce::RangedAudioParameter& p, Film f, juce::Point<float> c, std::function<bool (float)> down)
        : param (p), film (std::move (f)), centre (c), isDown (std::move (down)),
          attachment (p, [this] (float) { repaint(); if (onChange) onChange(); })
    {
        setBounds (juce::Rectangle<int> (68, 140).withCentre (c.toInt()));
        attachment.sendInitialUpdate();
        setTitle (p.getName (64));
    }

    std::function<void()> onChange;

    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        film.draw (g, isDown (param.getValue()) ? 1 : 0, centre - getPosition().toFloat());
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasClicked()) return;
        const float now = param.getValue();
        attachment.setValueAsCompleteGesture (param.convertFrom0to1 (now > 0.5f ? 0.0f : 1.0f));
    }

private:
    juce::RangedAudioParameter& param;
    Film film;
    juce::Point<float> centre;
    std::function<bool (float)> isDown;
    juce::ParameterAttachment attachment;
};

// ---------------------------------------------------------------------------
/** An invisible button over printed text. */
class Hotspot final : public juce::Component
{
public:
    Hotspot (juce::Rectangle<float> r, std::function<void()> f, const juce::String& name) : click (std::move (f))
    {
        setBounds (r.toNearestInt()); setTitle (name);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && click) click(); }
    std::function<void()> click;
};

// ---------------------------------------------------------------------------
/** Two needles over the rendered meter face, with VU ballistics. */
class Meter final : public juce::Component, private juce::Timer
{
public:
    explicit Meter (NazareProcessor& p) : processor (p)
    {
        setInterceptsMouseClicks (false, false);
        setBounds (juce::Rectangle<float> (L::vuFaceW, L::vuFaceH).withCentre ({ L::vuCentre.x, L::vuCentre.y }).toNearestInt());
        startTimerHz (60);
    }

    void paint (juce::Graphics& g) override
    {
        auto angle = [] (double v) { const double t = juce::jlimit (0.0, 1.1, (v + 20.0) / 23.0); return (float) (-44.0 + 88.0 * std::pow (t, 1.35)); };
        const juce::Point<float> pivot { L::vuPivot.x, L::vuPivot.y };
        g.reduceClipRegion (getLocalBounds());
        for (int c : { 1, 0 })
        {
            const auto tip = polar (pivot.x, pivot.y, L::vuRadius + 4.0f, angle (c == 0 ? vuL : vuR));
            juce::Path p; p.startNewSubPath (pivot); p.lineTo (tip);
            g.setColour (juce::Colour (0x553a2410));
            g.strokePath (p, juce::PathStrokeType (1.4f), juce::AffineTransform::translation (2.2f, 3.0f));
            g.setColour (c == 0 ? navy : red);
            g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        juce::ColourGradient glare (juce::Colours::white.withAlpha (0.30f), 0, 0, juce::Colours::white.withAlpha (0.0f), getWidth() * 0.45f, getHeight() * 0.8f, false);
        g.setGradientFill (glare); g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);
    }

private:
    void timerCallback() override
    {
        static constexpr double calValues[] = { -24.0, -18.0, -14.0, -12.0 };
        const double cal = calValues[juce::jlimit (0, 3, juce::roundToInt (processor.apvts.getParameter (pid::cal)->getValue() * 3.0f))];
        auto vu = [cal] (float ms) { return 10.0 * std::log10 (std::max (1.0e-12, (double) ms)) + 3.01 - cal; };
        const double k = 1.0 - std::exp (-1.0 / (60.0 * 0.065));
        vuL += (vu (processor.meterL.load()) - vuL) * k;
        vuR += (vu (processor.meterR.load()) - vuR) * k;
        repaint();
    }

    NazareProcessor& processor;
    double vuL = -22, vuR = -22;
};

// ---------------------------------------------------------------------------
/** The lit window with the channel number. */
class ChannelWindow final : public juce::Component
{
public:
    explicit ChannelWindow (juce::RangedAudioParameter& p) : param (p), attachment (p, [this] (float) { repaint(); })
    {
        setInterceptsMouseClicks (false, false);
        setBounds (juce::Rectangle<float> (L::channelW, L::channelH).withCentre ({ L::rear_channel.x, L::rear_channel.y }).toNearestInt());
        attachment.sendInitialUpdate();
    }
    void paint (juce::Graphics& g) override
    {
        const auto text = juce::String (juce::roundToInt (param.convertFrom0to1 (param.getValue()))).paddedLeft ('0', 2);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 21.0f, juce::Font::bold)));
        g.setColour (juce::Colour (0x88ffaa50));
        for (auto d : { -1.0f, 1.0f }) g.drawText (text, getLocalBounds().toFloat().translated (d * 0.6f, 0), juce::Justification::centred);
        g.setColour (juce::Colour (0xffffd9a0)); g.drawText (text, getLocalBounds().toFloat(), juce::Justification::centred);
    }
    void step (int d)
    {
        const int now = juce::roundToInt (param.convertFrom0to1 (param.getValue()));
        const int next = now + d < 1 ? 48 : (now + d > 48 ? 1 : now + d);
        attachment.setValueAsCompleteGesture ((float) next);
    }
private:
    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
};

// ---------------------------------------------------------------------------
class Face final : public juce::Component
{
public:
    Face (NazareProcessor& p, NazareEditor& ed, bool front) : processor (p), editor (ed)
    {
        setSize (L::width, L::height);
        plate = front ? load (BinaryData::plate_front_jpg, BinaryData::plate_front_jpgSize)
                      : load (BinaryData::plate_rear_jpg, BinaryData::plate_rear_jpgSize);
        front ? buildFront() : buildRear();
        addAndMakeVisible (tip);
        tip.setBounds (getLocalBounds());
    }

    void paint (juce::Graphics& g) override
    {
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        g.drawImage (plate, getLocalBounds().toFloat());
        if (jewel != nullptr)
            jewelFilm.draw (g, processor.apvts.getParameter (pid::bypass)->getValue() > 0.5f ? 0 : 1, { L::jewel.x, L::jewel.y });
    }

private:
    juce::RangedAudioParameter& par (const char* id) { return *processor.apvts.getParameter (id); }

    Control& add (std::unique_ptr<Control> c) { addAndMakeVisible (*c); controls.push_back (std::move (c)); return *controls.back(); }

    void buildFront()
    {
        const Film drive { load (BinaryData::drive_png, BinaryData::drive_pngSize), L::driveStrip };
        const Film small { load (BinaryData::small_png, BinaryData::small_pngSize), L::smallStrip };
        const Film head  { load (BinaryData::console_png, BinaryData::console_pngSize), L::consoleStrip };
        jewelFilm = { load (BinaryData::jewel_png, BinaryData::jewel_pngSize), L::jewelStrip };
        const Film toggle { load (BinaryData::toggle_png, BinaryData::toggle_pngSize), L::toggleStrip };

        auto& con = add (std::make_unique<Control> (par (pid::console), head, juce::Point<float> { L::console.x, L::console.y }, 46.0f, tip));
        con.frameFor = [] (float v) { return juce::roundToInt (v * 5.0f); };
        con.stepper = [] (float v, float st) { return juce::jlimit (0.0f, 1.0f, (std::round (v * 5.0f) + st) / 5.0f); };
        con.tipAt = { L::console.x, L::console.y + 70.0f };
        auto* conp = &con;
        const float widths[6] = { 44, 58, 74, 40, 74, 84 };
        for (int i = 0; i < 6; ++i)
        {
            const float a = L::consoleAngles[i];
            const auto pt = polar (L::console.x, L::console.y, L::consoleLabelRadius, a);
            const float left = a < -5 ? pt.x - widths[i] : (a > 5 ? pt.x : pt.x - widths[i] / 2);
            spots.push_back (std::make_unique<Hotspot> (juce::Rectangle<float> (left - 4, pt.y - 9, widths[i] + 8, 18),
                [conp, i] { conp->param.beginChangeGesture(); conp->param.setValueNotifyingHost ((float) i / 5.0f); conp->param.endChangeGesture(); conp->repaint(); }, gen::models[i].name));
            addAndMakeVisible (*spots.back());
        }

        auto& dr = add (std::make_unique<Control> (par (pid::drive), drive, juce::Point<float> { L::drive.x, L::drive.y }, L::driveDiameter / 2, tip));
        dr.travel = 340.0f;
        dr.describe = [this] { return juce::String (zoneName (processor.apvts.getParameter (pid::drive)->getValue())); };
        dr.tipAt = { L::drive.x, L::drive.y - L::driveDiameter / 2 - 22.0f };

        auto& sp = add (std::make_unique<Control> (par (pid::spread), small, juce::Point<float> { L::spread.x, L::spread.y }, L::smallDiameter / 2, tip));
        sp.tipAt = { L::spread.x, L::spread.y - L::smallDiameter / 2 - 26.0f };
        auto& out = add (std::make_unique<Control> (par (pid::output), small, juce::Point<float> { L::output.x, L::output.y }, L::smallDiameter / 2, tip));
        out.tipAt = { L::output.x, L::output.y - L::smallDiameter / 2 - 26.0f };
        out.describe = [this] { const float v = processor.apvts.getParameter (pid::output)->convertFrom0to1 (processor.apvts.getParameter (pid::output)->getValue());
                                return (v >= 0 ? "+" : "-") + juce::String (std::abs (v), 1) + " dB"; };

        toggles.push_back (std::make_unique<Toggle> (par (pid::bypass), toggle, juce::Point<float> { L::power.x, L::power.y }, [] (float v) { return v > 0.5f; }));
        toggles.back()->onChange = [this] { repaint(); };
        addAndMakeVisible (*toggles.back());
        jewel = toggles.back().get();

        flipSpot (L::frontFlip, "Show the rear panel");
        meter = std::make_unique<Meter> (processor);
        addAndMakeVisible (*meter);
    }

    void buildRear()
    {
        const Film trim { load (BinaryData::trim_png, BinaryData::trim_pngSize), L::trimStrip };
        const Film toggle { load (BinaryData::toggle_png, BinaryData::toggle_pngSize), L::toggleStrip };
        const float tr = L::trimDiameter / 2 + 6;

        auto& age = add (std::make_unique<Control> (par (pid::age), trim, juce::Point<float> { L::rear_age.x, L::rear_age.y }, tr, tip));
        age.tipAt = { L::rear_age.x, L::rear_age.y - 58.0f };
        auto& noise = add (std::make_unique<Control> (par (pid::noise), trim, juce::Point<float> { L::rear_noise.x, L::rear_noise.y }, tr, tip));
        noise.tipAt = { L::rear_noise.x, L::rear_noise.y - 58.0f };

        for (auto [id, pos, w] : { std::tuple { pid::oversampling, L::rear_os, 22.0f }, std::tuple { pid::cal, L::rear_cal, 26.0f } })
        {
            auto& c = add (std::make_unique<Control> (par (id), trim, juce::Point<float> { pos.x, pos.y }, tr, tip));
            c.frameFor = [] (float v) { return L::stepFrames[juce::jlimit (0, 3, juce::roundToInt (v * 3.0f))]; };
            c.stepper = [] (float v, float st) { return juce::jlimit (0.0f, 1.0f, (std::round (v * 3.0f) + st) / 3.0f); };
            c.tipAt = { pos.x, pos.y - 58.0f };
            auto* cp = &c;
            for (int i = 0; i < 4; ++i)
            {
                const float a = L::stepAngles[i];
                const auto pt = polar (pos.x, pos.y, L::trimDiameter / 2 + 26, a);
                const float left = a < 0 ? pt.x - w : pt.x;
                spots.push_back (std::make_unique<Hotspot> (juce::Rectangle<float> (left - 4, pt.y - 9, w + 8, 18),
                    [cp, i] { cp->param.beginChangeGesture(); cp->param.setValueNotifyingHost ((float) i / 3.0f); cp->param.endChangeGesture(); cp->repaint(); }, cp->param.getAllValueStrings()[i]));
                addAndMakeVisible (*spots.back());
            }
        }

        auto addToggle = [&] (const char* id, L::Point pos, std::function<bool (float)> down)
        {
            toggles.push_back (std::make_unique<Toggle> (par (id), toggle, juce::Point<float> { pos.x, pos.y }, std::move (down)));
            addAndMakeVisible (*toggles.back());
        };
        addToggle (pid::mode, L::rear_mode, [] (float v) { return v > 0.5f; });          // down = BUS
        addToggle (pid::autoGain, L::rear_auto, [] (float v) { return v < 0.5f; });      // up = ON
        addToggle (pid::matched, L::rear_matched, [] (float v) { return v > 0.5f; });    // down = ON

        channel = std::make_unique<ChannelWindow> (par (pid::channel));
        addAndMakeVisible (*channel);
        for (int d : { -1, 1 })
        {
            spots.push_back (std::make_unique<Hotspot> (juce::Rectangle<float> (28, 32).withCentre ({ L::rear_channel.x + d * 58.0f, L::rear_channel.y }),
                [this, d] { channel->step (d); }, d < 0 ? "Previous channel" : "Next channel"));
            addAndMakeVisible (*spots.back());
        }
        flipSpot (L::rearFlip, "Show the front panel");
    }

    void flipSpot (L::Point at, const juce::String& name)
    {
        spots.push_back (std::make_unique<Hotspot> (juce::Rectangle<float> (at.x - 110, at.y - 12, 116, 24), [this] { editor.flip(); }, name));
        addAndMakeVisible (*spots.back());
    }

    NazareProcessor& processor;
    NazareEditor& editor;
    juce::Image plate;
    Film jewelFilm;
    Toggle* jewel = nullptr;
    Tip tip;
    std::vector<std::unique_ptr<Control>> controls;
    std::vector<std::unique_ptr<Toggle>> toggles;
    std::vector<std::unique_ptr<Hotspot>> spots;
    std::unique_ptr<Meter> meter;
    std::unique_ptr<ChannelWindow> channel;
};

// ---------------------------------------------------------------------------
NazareEditor::NazareEditor (NazareProcessor& p) : AudioProcessorEditor (p), processor (p)
{
    front = std::make_unique<Face> (p, *this, true);
    back  = std::make_unique<Face> (p, *this, false);
    addAndMakeVisible (*front);
    addChildComponent (*back);
    rear = (bool) p.uiState.getProperty ("rear", false);
    front->setVisible (! rear); back->setVisible (rear);

    constrainer.setFixedAspectRatio ((double) L::width / L::height);
    constrainer.setSizeLimits (L::width / 2, L::height / 2, L::width * 3 / 2, L::height * 3 / 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const double scale = (double) p.uiState.getProperty ("scale", 0.8);
    setSize (juce::roundToInt (L::width * scale), juce::roundToInt (L::height * scale));
}

NazareEditor::~NazareEditor() { stopTimer(); }

void NazareEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colour (0xff1c1a1a)); }

void NazareEditor::resized()
{
    processor.uiState.setProperty ("scale", (double) getWidth() / L::width, nullptr);
    applyTransforms();
}

void NazareEditor::applyTransforms()
{
    const float s = (float) getWidth() / (float) L::width;
    // turning over: squeeze to an edge, change sides, open out again
    const float squeeze = flipping ? std::abs (std::cos ((float) flipPhase * juce::MathConstants<float>::pi)) : 1.0f;
    const auto t = juce::AffineTransform::scale (juce::jmax (0.001f, squeeze), 1.0f, L::width / 2.0f, 0.0f).scaled (s);
    front->setTransform (t); back->setTransform (t);
}

void NazareEditor::flip()
{
    if (flipping) return;
    flipping = true; flipPhase = 0.0;
    startTimerHz (60);
}

void NazareEditor::timerCallback()
{
    const double before = flipPhase;
    flipPhase += 1.0 / (60.0 * 0.42);
    if (before < 0.5 && flipPhase >= 0.5)
    {
        rear = ! rear;
        front->setVisible (! rear); back->setVisible (rear);
        processor.uiState.setProperty ("rear", rear, nullptr);
    }
    if (flipPhase >= 1.0) { flipping = false; stopTimer(); }
    applyTransforms();
}

} // namespace nazare
