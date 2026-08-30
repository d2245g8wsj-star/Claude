#include "Theme.h"

namespace crest::theme
{

juce::Font labelFont (float height)
{
    return juce::Font (juce::FontOptions (height, juce::Font::bold));
}

juce::Font valueFont (float height)
{
    return juce::Font (juce::FontOptions (height, juce::Font::plain));
}

juce::Font displayFont (float height)
{
    return juce::Font (juce::FontOptions (height, juce::Font::bold));
}

void drawTracked (juce::Graphics& g,
                  const juce::String& textToDraw,
                  juce::Rectangle<float> area,
                  juce::Justification justification,
                  float tracking)
{
    if (textToDraw.isEmpty())
        return;

    const auto font = g.getCurrentFont();

    float total = 0.0f;
    juce::Array<float> widths;

    for (auto character : textToDraw)
    {
        const auto w = juce::GlyphArrangement::getStringWidth (font, juce::String::charToString (character));
        widths.add (w);
        total += w + tracking;
    }

    total -= tracking;

    auto x = area.getX();

    if (justification.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags (juce::Justification::right))
        x = area.getRight() - total;

    const auto y = area.getCentreY() - font.getHeight() * 0.5f;

    int index = 0;

    for (auto character : textToDraw)
    {
        const auto glyph = juce::String::charToString (character);

        g.drawText (glyph, juce::Rectangle<float> (x, y, widths[index] + 2.0f, font.getHeight()),
                    juce::Justification::centredLeft, false);

        x += widths[index] + tracking;
        ++index;
    }
}

LookAndFeel::LookAndFeel()
{
    setColour (juce::TooltipWindow::backgroundColourId, panelRaised);
    setColour (juce::TooltipWindow::textColourId,       text);
    setColour (juce::TooltipWindow::outlineColourId,    outline);

    setColour (juce::TextEditor::backgroundColourId,    panel);
    setColour (juce::TextEditor::textColourId,          text);
    setColour (juce::TextEditor::highlightColourId,     amber.withAlpha (0.30f));
    setColour (juce::TextEditor::outlineColourId,       outline);
    setColour (juce::TextEditor::focusedOutlineColourId, amber);
    setColour (juce::CaretComponent::caretColourId,     amber);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText,
                                                    juce::Point<int> screenPos,
                                                    juce::Rectangle<int> parentArea)
{
    const auto font = valueFont (12.0f);

    juce::TextLayout layout;
    juce::AttributedString s (tipText);
    s.setFont (font);
    layout.createLayout (s, 320.0f);

    const auto w = juce::roundToInt (layout.getWidth()) + 18;
    const auto h = juce::roundToInt (layout.getHeight()) + 12;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 16,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6)  : screenPos.y + 18,
                                 w, h)
        .constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& tipText, int width, int height)
{
    const juce::Rectangle<float> bounds (0.0f, 0.0f, (float) width, (float) height);

    g.setColour (panelRaised);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (outline);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

    g.setColour (text);
    g.setFont (valueFont (12.0f));
    g.drawFittedText (tipText, bounds.reduced (9.0f, 6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 3);
}

} // namespace crest::theme
