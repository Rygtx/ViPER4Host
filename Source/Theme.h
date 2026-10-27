#pragma once

// Dark theme with a ViPER-red accent. Everything is styled through a single
// LookAndFeel: colours are set once in the constructor, plus a handful of
// draw overrides for the pieces the defaults can't express (slim sliders,
// switch-style toggles, panel-style groups).

#include <JuceHeader.h>

namespace v4h
{
namespace theme
{
constexpr uint32_t background = 0xff14161b; // window
constexpr uint32_t header = 0xff1b1e25; // title bar strip
constexpr uint32_t panel = 0xff1e212a; // group background
constexpr uint32_t panelLight = 0xff262a35; // buttons, hover
constexpr uint32_t outline = 0xff2c313d; // borders
constexpr uint32_t field = 0xff12141a; // text boxes, combos
constexpr uint32_t accent = 0xff2dd4bf; // teal accent
constexpr uint32_t accentBright = 0xff7ce8db;
constexpr uint32_t text = 0xffc9cdd6;
constexpr uint32_t textBright = 0xffedeff4;
constexpr uint32_t textDim = 0xff878d99;
constexpr uint32_t gr = 0xfffb923c; // gain-reduction amber (industry convention)

inline juce::Colour c(uint32_t v) { return juce::Colour(v); }
} // namespace theme

class ViperLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ViperLookAndFeel()
    {
        using namespace theme;
        using juce::LookAndFeel_V4;

        setColour(juce::ResizableWindow::backgroundColourId, c(background));
        // Plain labels are text-only (transparent); slider text boxes style
        // themselves from the Slider::textBox* ids below.
        setColour(juce::Label::textColourId, c(text));
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::textWhenEditingColourId, c(textBright));
        setColour(juce::Label::backgroundWhenEditingColourId, c(field));

        setColour(juce::Slider::textBoxTextColourId, c(text));
        setColour(juce::Slider::textBoxBackgroundColourId, c(field));
        setColour(juce::Slider::textBoxOutlineColourId, c(outline));
        setColour(juce::Slider::textBoxHighlightColourId, c(accent).withAlpha(0.45f));
        setColour(juce::Slider::trackColourId, c(accent));

        setColour(juce::ComboBox::backgroundColourId, c(field));
        setColour(juce::ComboBox::outlineColourId, c(outline));
        setColour(juce::ComboBox::textColourId, c(text));
        setColour(juce::ComboBox::arrowColourId, c(textDim));
        setColour(juce::ComboBox::focusedOutlineColourId, c(accent));

        setColour(juce::PopupMenu::backgroundColourId, c(panel));
        setColour(juce::PopupMenu::textColourId, c(text));
        setColour(juce::PopupMenu::headerTextColourId, c(textBright));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, c(accent).withAlpha(0.30f));
        setColour(juce::PopupMenu::highlightedTextColourId, c(textBright));

        setColour(juce::TextButton::buttonColourId, c(panelLight));
        setColour(juce::TextButton::buttonOnColourId, c(accent));
        setColour(juce::TextButton::textColourOffId, c(text));
        setColour(juce::TextButton::textColourOnId, c(textBright));

        setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::ScrollBar::thumbColourId, c(outline).brighter(0.35f));
    }

    juce::Font getTextButtonFont(juce::TextButton&, int) override
    {
        return withDefaultMetrics(juce::FontOptions(13.0f));
    }

    // Thumb radius 7 keeps JUCE's sliderPos mapping in sync with the drawing
    // below (thumb travels exactly to the track ends).
    int getSliderThumbRadius(juce::Slider&) override { return 7; }

    // Slim rounded track, accent fill up to a round thumb.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
        float /*min*/, float /*max*/, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        using namespace theme;
        const float cy = static_cast<float>(y) + h * 0.5f;
        const float trackH = 5.0f;
        const float radius = trackH * 0.5f;
        const float margin = static_cast<float>(getSliderThumbRadius(slider));
        const float left = static_cast<float>(x) + margin;
        const float right = static_cast<float>(x + w) - margin;
        auto track = juce::Rectangle<float>(left, cy - radius, juce::jmax(right - left, 0.0f), trackH);

        g.setColour(c(outline));
        g.fillRoundedRectangle(track, radius);

        auto filled = track.withRight(juce::jlimit(track.getX(), track.getRight(), sliderPos));
        if (filled.getWidth() > 0.0f)
        {
            g.setColour(c(accent));
            g.fillRoundedRectangle(filled, radius);
        }

        g.setColour(c(textBright));
        g.fillEllipse(sliderPos - 6.5f, cy - 6.5f, 13.0f, 13.0f);
        g.setColour(c(accent));
        g.drawEllipse(sliderPos - 6.5f, cy - 6.5f, 13.0f, 13.0f, 2.0f);
    }

    // Rounded field with a small chevron; hover/focus gets the accent outline.
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
        int, int, int, int, juce::ComboBox& box) override
    {
        using namespace theme;
        auto bounds = box.getLocalBounds().toFloat().reduced(0.5f);
        const bool highlight = box.isMouseOver() || box.hasKeyboardFocus(true) || isButtonDown;

        g.setColour(c(field));
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(highlight ? c(accent) : c(outline));
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

        // Chevron centred in the right edge.
        auto arrow = juce::Rectangle<float>(width - 22.0f, height * 0.5f - 3.5f, 8.0f, 7.0f);
        juce::Path p;
        p.startNewSubPath(arrow.getX(), arrow.getY());
        p.lineTo(arrow.getCentreX(), arrow.getBottom());
        p.lineTo(arrow.getRight(), arrow.getY());
        g.setColour(isButtonDown ? c(textBright) : c(textDim));
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    // Switch-style toggle: pill + sliding knob, no text (labels carry it).
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool over, bool) override
    {
        using namespace theme;
        auto bounds = button.getLocalBounds().toFloat().withSizeKeepingCentre(40.0f, 20.0f);
        const bool on = button.getToggleState();

        g.setColour(c(on ? accent : panelLight));
        if (over)
            g.setColour(c(on ? accentBright : outline).brighter(0.08f));
        g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.5f);
        g.setColour(c(on ? accentBright : outline));
        g.drawRoundedRectangle(bounds, bounds.getHeight() * 0.5f, 1.0f);

        const float knob = 14.0f;
        const float x = on ? bounds.getRight() - knob - 3.0f : bounds.getX() + 3.0f;
        g.setColour(c(textBright));
        g.fillEllipse(x, bounds.getCentreY() - knob * 0.5f, knob, knob);
    }

    // Panel-style group: title sits on the top border line, fieldset-style.
    // The border is a path with a gap where the text goes — no background
    // fill behind the text, and the card fill starts at the border line.
    void drawGroupComponentOutline(juce::Graphics& g, int width, int height,
        const juce::String& text, const juce::Justification&, juce::GroupComponent&) override
    {
        using namespace theme;
        const float cs = 7.0f; // corner radius
        const float textH = 16.0f;
        const float yTop = textH * 0.5f; // border edge passes through the text centre
        const float textX = 14.0f;
        const float textGap = 6.0f;

        auto font = withDefaultMetrics(juce::FontOptions(13.0f, juce::Font::bold));
        const float textW = text.isEmpty() ? 0.0f
            : juce::jlimit(0.0f, juce::jmax(0.0f, static_cast<float>(width) - textX * 2.0f - textGap * 2.0f),
                static_cast<float>(juce::GlyphArrangement::getStringWidthInt(font, text)));

        auto card = juce::Rectangle<float>(0.5f, yTop, width - 1.0f, height - yTop - 0.5f);
        g.setColour(c(panel));
        g.fillRoundedRectangle(card, cs);

        const float l = card.getX(), r = card.getRight();
        const float t = card.getY(), b = card.getBottom();
        const float gapL = textX - textGap, gapR = textX + textW + textGap;
        juce::Path p;
        p.startNewSubPath(gapR, t);
        p.lineTo(r - cs, t);
        p.quadraticTo(r, t, r, t + cs);
        p.lineTo(r, b - cs);
        p.quadraticTo(r, b, r - cs, b);
        p.lineTo(l + cs, b);
        p.quadraticTo(l, b, l, b - cs);
        p.lineTo(l, t + cs);
        p.quadraticTo(l, t, l + cs, t);
        p.lineTo(gapL, t);
        g.setColour(c(outline));
        g.strokePath(p, juce::PathStrokeType(1.0f));

        if (text.isNotEmpty())
        {
            g.setFont(font);
            g.setColour(c(accent));
            g.drawText(text, juce::Rectangle<float>(textX, t - textH * 0.5f, textW + 1.0f, textH),
                juce::Justification::centredLeft);
        }
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour&, bool over, bool down) override
    {
        using namespace theme;
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        auto fill = c(down ? outline : panelLight).brighter(down ? 0.15f : 0.0f);
        if (button.getToggleState())
            fill = c(accent);
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, 5.0f);
        if (over && !down)
        {
            g.setColour(c(outline));
            g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
        }
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool over, bool down) override
    {
        using namespace theme;
        g.setColour(button.getToggleState() || over || down ? c(textBright) : c(text));
        g.setFont(getTextButtonFont(button, button.getHeight()));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
    }
};
} // namespace v4h
