#include "PluginEditor.h"

namespace transcender
{

namespace colours
{
    const juce::Colour backgroundTop    { 0xff141733 };
    const juce::Colour backgroundBottom { 0xff090a17 };
    const juce::Colour panel            { 0xff1a1e3d };
    const juce::Colour panelEdge        { 0xff2c3263 };
    const juce::Colour ink              { 0xffe8ebff };
    const juce::Colour inkDim           { 0xff8d93c2 };
    const juce::Colour track            { 0xff0c0e22 };
    const juce::Colour cyan             { 0xff3fe0ff };
    const juce::Colour magenta          { 0xffff4fc1 };
}

namespace
{
    juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    struct Section
    {
        const char* title;
        juce::Rectangle<float> bounds;
        juce::Colour accent;
    };

    // Plan of the face.
    constexpr float margin = 16.0f, gap = 12.0f;
    constexpr float row1 = 84.0f, row2 = 248.0f, row3 = 412.0f, rowHeight = 152.0f;

    const Section sections[] = {
        { "OSCILLATORS", { margin, row1, 420.0f, rowHeight }, colours::cyan },
        { "VOICE",       { 448.0f, row1, 180.0f, rowHeight }, colours::cyan },
        { "FILTER",      { 640.0f, row1, 344.0f, rowHeight }, colours::cyan },
        { "ENVELOPE",    { margin, row2, 420.0f, rowHeight }, colours::cyan },
        { "LFO",         { 448.0f, row2, 232.0f, rowHeight }, colours::cyan },
        { "DELAY",       { 692.0f, row2, 292.0f, rowHeight }, colours::cyan },
        { "TRANCE GATE", { margin, row3, 840.0f, rowHeight }, colours::magenta },
        { "OUTPUT",      { 868.0f, row3, 116.0f, rowHeight }, colours::cyan },
    };

    constexpr int knobWidth = 96, knobHeight = 118;
    constexpr float controlTop = 26.0f;

    juce::Rectangle<int> slot (const Section& s, float x, int width = knobWidth, int height = knobHeight)
    {
        return juce::Rectangle<int> ((int) (s.bounds.getX() + x), (int) (s.bounds.getY() + controlTop), width, height);
    }
}

// ---------------------------------------------------------------------------

TranscenderLook::TranscenderLook()
{
    setColour (juce::ComboBox::backgroundColourId, colours::track);
    setColour (juce::ComboBox::textColourId, colours::ink);
    setColour (juce::ComboBox::outlineColourId, colours::panelEdge);
    setColour (juce::ComboBox::arrowColourId, colours::cyan);
    setColour (juce::PopupMenu::backgroundColourId, colours::panel);
    setColour (juce::PopupMenu::textColourId, colours::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::cyan.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, colours::ink);
    setColour (juce::Label::textColourId, colours::ink);
    setColour (juce::TextButton::textColourOffId, colours::ink);
    setColour (juce::TextButton::textColourOnId, colours::ink);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panel);
    setColour (juce::TooltipWindow::textColourId, colours::ink);
    setColour (juce::TooltipWindow::outlineColourId, colours::panelEdge);
}

void TranscenderLook::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float position,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto arcRadius = radius - 3.0f;
    const auto angle = startAngle + position * (endAngle - startAngle);

    // Track and value arcs. A bipolar control fills from the top.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const auto from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (colours::track);
    g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (value, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Cap.
    const auto capRadius = radius * 0.66f;
    const auto cap = juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a4078), cap.getX(), cap.getY(),
                                             juce::Colour (0xff15183a), cap.getRight(), cap.getBottom(), false));
    g.fillEllipse (cap);
    g.setColour (colours::panelEdge.brighter (0.3f));
    g.drawEllipse (cap, 1.2f);

    const auto tip    = centre.getPointOnCircumference (capRadius * 0.85f, angle);
    const auto inside = centre.getPointOnCircumference (capRadius * 0.30f, angle);
    g.setColour (colours::ink);
    g.drawLine ({ inside, tip }, 2.6f);
}

void TranscenderLook::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);

    juce::Path arrow;
    const auto ax = (float) width - 14.0f, ay = (float) height * 0.5f;
    arrow.addTriangle (ax - 4.5f, ay - 2.5f, ax + 4.5f, ay - 2.5f, ax, ay + 3.5f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.fillPath (arrow);
}

juce::Font TranscenderLook::getComboBoxFont (juce::ComboBox& box)
{
    return font (juce::jmin (15.0f, (float) box.getHeight() * 0.55f));
}

void TranscenderLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void TranscenderLook::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto r = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto on = button.getToggleState();

    g.setColour (on ? colours::cyan.withAlpha (0.22f) : colours::track);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? colours::cyan : (highlighted ? colours::inkDim : colours::panelEdge));
    g.drawRoundedRectangle (r, 6.0f, 1.4f);

    g.setColour (on ? colours::ink : colours::inkDim);
    g.setFont (font (14.0f, true));
    g.drawText (on ? "POLY" : "MONO", r, juce::Justification::centred);
}

void TranscenderLook::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                            bool highlighted, bool down)
{
    const auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? colours::cyan.withAlpha (0.3f) : colours::track);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (highlighted ? colours::cyan : colours::panelEdge);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

// ---------------------------------------------------------------------------

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, juce::String text)
    : label (std::move (text)),
      attachment (state, parameterID, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, colours::cyan);
    slider.setDoubleClickReturnValue (true, (double) state.getParameter (parameterID)->convertFrom0to1 (
                                                state.getParameter (parameterID)->getDefaultValue()));
    slider.setPopupDisplayEnabled (false, false, nullptr);
    addAndMakeVisible (slider);

    valueLabel.setJustificationType (juce::Justification::centred);
    valueLabel.setFont (font (12.5f));
    valueLabel.setColour (juce::Label::textColourId, colours::inkDim);
    valueLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (valueLabel);

    auto* parameter = state.getParameter (parameterID);
    auto update = [this, parameter]
    {
        valueLabel.setText (parameter->getCurrentValueAsText(), juce::dontSendNotification);
    };
    slider.onValueChange = update;
    update();

    if (parameterID.startsWith ("gate"))
        slider.setColour (juce::Slider::rotarySliderFillColourId, colours::magenta);

    slider.setTooltip (parameter->getName (64) + " (double click to reset)");
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    valueLabel.setBounds (r.removeFromBottom (18));
    slider.setBounds (r);
}

void Knob::paint (juce::Graphics& g)
{
    g.setColour (colours::ink);
    g.setFont (font (13.5f, true));
    g.drawText (label, getLocalBounds().removeFromTop (18), juce::Justification::centred);
}

// ---------------------------------------------------------------------------

Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& parameterID, juce::String text)
    : label (std::move (text))
{
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterID)))
        box.addItemList (choice->choices, 1);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, parameterID, box);

    if (parameterID.startsWith ("gate"))
        box.setColour (juce::ComboBox::arrowColourId, colours::magenta);

    addAndMakeVisible (box);
}

void Choice::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (22);
    box.setBounds (r.removeFromTop (30));
}

void Choice::paint (juce::Graphics& g)
{
    g.setColour (colours::ink);
    g.setFont (font (13.5f, true));
    g.drawText (label, getLocalBounds().removeFromTop (18), juce::Justification::centred);
}

// ---------------------------------------------------------------------------

void StepLights::set (int newPattern, int newStep, bool isEnabled)
{
    if (newPattern != pattern || newStep != step || isEnabled != enabled)
    {
        pattern = newPattern;
        step = newStep;
        enabled = isEnabled;
        repaint();
    }
}

void StepLights::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const auto cell = r.getWidth() / 16.0f;

    for (int i = 0; i < 16; ++i)
    {
        auto pad = juce::Rectangle<float> (r.getX() + (float) i * cell, r.getY(), cell, r.getHeight()).reduced (3.0f, 0.0f);
        const bool on = gateStepOn (pattern, i);
        const bool now = enabled && i == step;

        auto colour = on ? colours::magenta.withAlpha (enabled ? 0.55f : 0.25f) : colours::track;
        if (now) colour = on ? colours::magenta : colours::inkDim.withAlpha (0.4f);

        if (now && on)
        {
            g.setColour (colours::magenta.withAlpha (0.3f));
            g.fillRoundedRectangle (pad.expanded (2.0f), 5.0f);
        }

        g.setColour (colour);
        g.fillRoundedRectangle (pad, 4.0f);
        g.setColour (i % 4 == 0 ? colours::inkDim : colours::panelEdge);
        g.drawRoundedRectangle (pad, 4.0f, 1.0f);
    }
}

// ---------------------------------------------------------------------------

TranscenderEditor::Face::Face (TranscenderProcessor& p)
    : processor (p),
      wave          (p.apvts, pid::wave, "Wave"),
      detune        (p.apvts, pid::detune, "Detune"),
      octave        (p.apvts, pid::octave, "Octave"),
      stereo        (p.apvts, pid::stereo, "Stereo"),
      polyAttachment (p.apvts, pid::poly, poly),
      glide         (p.apvts, pid::glide, "Glide"),
      cutoff        (p.apvts, pid::cutoff, "Cutoff"),
      resonance     (p.apvts, pid::resonance, "Resonance"),
      envAmount     (p.apvts, pid::envAmount, "Env amount"),
      attack        (p.apvts, pid::attack, "Attack"),
      decay         (p.apvts, pid::decay, "Decay"),
      sustain       (p.apvts, pid::sustain, "Sustain"),
      release       (p.apvts, pid::release, "Release"),
      lfoRate       (p.apvts, pid::lfoRate, "Speed"),
      lfoDepth      (p.apvts, pid::lfoDepth, "Depth"),
      volume        (p.apvts, pid::volume, "Volume"),
      gateDepth     (p.apvts, pid::gateDepth, "Depth"),
      gateLength    (p.apvts, pid::gateLength, "Length"),
      gateRate      (p.apvts, pid::gateRate, "Rate"),
      gatePattern   (p.apvts, pid::gatePattern, "Pattern"),
      delayTime     (p.apvts, pid::delayTime, "Time"),
      delayMix      (p.apvts, pid::delayMix, "Level"),
      delayFeedback (p.apvts, pid::delayFeedback, "Feedback")
{
    for (int i = 0; i < processor.getNumPrograms(); ++i)
        presets.addItem (processor.getProgramName (i), i + 1);

    presets.onChange = [this]
    {
        const auto index = presets.getSelectedItemIndex();
        if (index >= 0 && index != processor.getCurrentProgram())
            choosePreset (index);
    };
    presets.setTooltip ("Factory sounds");

    previous.onClick = [this] { choosePreset ((processor.getCurrentProgram() + processor.getNumPrograms() - 1) % processor.getNumPrograms()); };
    next.onClick     = [this] { choosePreset ((processor.getCurrentProgram() + 1) % processor.getNumPrograms()); };

    poly.setTooltip ("Poly: up to eight notes. Mono: one note, legato, with glide between held keys.");

    for (auto* c : std::initializer_list<juce::Component*> {
             &presets, &previous, &next, &wave, &detune, &octave, &stereo, &poly, &glide,
             &cutoff, &resonance, &envAmount, &attack, &decay, &sustain, &release,
             &lfoRate, &lfoDepth, &volume, &gateDepth, &gateLength, &gateRate, &gatePattern, &steps,
             &delayTime, &delayMix, &delayFeedback })
        addAndMakeVisible (c);

    refresh();
}

void TranscenderEditor::Face::choosePreset (int index)
{
    processor.setCurrentProgram (index);
    refresh();
}

void TranscenderEditor::Face::refresh()
{
    const auto program = processor.getCurrentProgram();
    if (program != shownProgram)
    {
        shownProgram = program;
        presets.setSelectedItemIndex (program, juce::dontSendNotification);
    }

    const auto depth = processor.apvts.getRawParameterValue (pid::gateDepth)->load();
    const auto pattern = juce::roundToInt (processor.apvts.getRawParameterValue (pid::gatePattern)->load());
    steps.set (pattern, processor.getGateStep(), depth > 0.0001f);
}

void TranscenderEditor::Face::resized()
{
    presets.setBounds (614, 22, 290, 34);
    previous.setBounds (912, 22, 34, 34);
    next.setBounds (950, 22, 34, 34);

    const auto& osc = sections[0];
    wave.setBounds (slot (osc, 14, 108, 60).withY ((int) osc.bounds.getY() + 48));
    detune.setBounds (slot (osc, 128));
    octave.setBounds (slot (osc, 224));
    stereo.setBounds (slot (osc, 320));

    const auto& voice = sections[1];
    poly.setBounds (juce::Rectangle<int> (80, 30).withCentre ({ (int) voice.bounds.getX() + 46, (int) voice.bounds.getY() + 84 }));
    glide.setBounds (slot (voice, 84));

    const auto& filter = sections[2];
    cutoff.setBounds (slot (filter, 16));
    resonance.setBounds (slot (filter, 124));
    envAmount.setBounds (slot (filter, 232));

    const auto& env = sections[3];
    attack.setBounds (slot (env, 14));
    decay.setBounds (slot (env, 114));
    sustain.setBounds (slot (env, 214));
    release.setBounds (slot (env, 314));

    const auto& lfo = sections[4];
    lfoRate.setBounds (slot (lfo, 12));
    lfoDepth.setBounds (slot (lfo, 124));

    const auto& echo = sections[5];
    delayTime.setBounds (slot (echo, 12, 84, 60).withY ((int) echo.bounds.getY() + 48));
    delayMix.setBounds (slot (echo, 100));
    delayFeedback.setBounds (slot (echo, 192));

    const auto& gateSection = sections[6];
    gateDepth.setBounds (slot (gateSection, 12));
    gateLength.setBounds (slot (gateSection, 108));
    gateRate.setBounds (slot (gateSection, 214, 84, 60).withY ((int) gateSection.bounds.getY() + 34));
    gatePattern.setBounds (slot (gateSection, 214, 84, 60).withY ((int) gateSection.bounds.getY() + 90));
    steps.setBounds ((int) gateSection.bounds.getX() + 318, (int) gateSection.bounds.getY() + 56, 506, 54);

    const auto& out = sections[7];
    volume.setBounds (slot (out, 10));
}

void TranscenderEditor::Face::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::backgroundTop, 0.0f, 0.0f,
                                             colours::backgroundBottom, 0.0f, r.getBottom(), false));
    g.fillAll();

    // Title.
    g.setColour (colours::cyan);
    g.setFont (font (34.0f, true));
    g.drawText ("TRANSCENDER", juce::Rectangle<float> (margin + 2.0f, 14.0f, 320.0f, 40.0f), juce::Justification::centredLeft);
    g.setColour (colours::inkDim);
    g.setFont (font (13.0f));
    g.drawText (juce::String::fromUTF8 ("ten oscillator trance synth  \xc2\xb7  after Transcender SE by Martin Best"),
                juce::Rectangle<float> (margin + 4.0f, 50.0f, 520.0f, 20.0f), juce::Justification::centredLeft);
    g.setColour (colours::ink);
    g.setFont (font (12.0f, true));
    g.drawText ("PRESET", juce::Rectangle<float> (556.0f, 22.0f, 52.0f, 34.0f), juce::Justification::centredRight);

    for (const auto& s : sections)
    {
        g.setColour (colours::panel);
        g.fillRoundedRectangle (s.bounds, 10.0f);
        g.setColour (colours::panelEdge);
        g.drawRoundedRectangle (s.bounds.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (s.accent);
        g.fillRoundedRectangle (s.bounds.getX() + 12.0f, s.bounds.getY() + 10.0f, 3.0f, 12.0f, 1.5f);
        g.setColour (colours::ink);
        g.setFont (font (12.5f, true));
        g.drawText (s.title, juce::Rectangle<float> (s.bounds.getX() + 20.0f, s.bounds.getY() + 6.0f, 200.0f, 20.0f),
                    juce::Justification::centredLeft);
    }

    // Small print under the gate steps.
    const auto& gateSection = sections[6];
    g.setColour (colours::inkDim);
    g.setFont (font (11.5f));
    g.drawText ("locks to the host tempo; free runs at " + juce::String (TranscenderProcessor::fallbackBpm, 0)
                    + " BPM when the transport is stopped",
                juce::Rectangle<float> (gateSection.bounds.getX() + 318.0f, gateSection.bounds.getY() + 116.0f, 506.0f, 18.0f),
                juce::Justification::centred);
}

// ---------------------------------------------------------------------------

TranscenderEditor::TranscenderEditor (TranscenderProcessor& p)
    : AudioProcessorEditor (p), processor (p), face (p)
{
    setLookAndFeel (&look);
    addAndMakeVisible (face);
    face.setBounds (0, 0, designWidth, designHeight);

    setResizable (true, true);
    setResizeLimits (designWidth / 2, designHeight / 2, designWidth * 2, designHeight * 2);
    if (auto* aspect = getConstrainer())
        aspect->setFixedAspectRatio ((double) designWidth / designHeight);

    setSize (designWidth, designHeight);
    startTimerHz (30);
}

TranscenderEditor::~TranscenderEditor()
{
    setLookAndFeel (nullptr);
}

void TranscenderEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours::backgroundBottom);
}

void TranscenderEditor::resized()
{
    const auto scale = (float) getWidth() / (float) designWidth;
    face.setTransform (juce::AffineTransform::scale (scale));
}

void TranscenderEditor::timerCallback()
{
    face.refresh();
}

} // namespace transcender
