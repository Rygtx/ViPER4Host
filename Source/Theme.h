#pragma once

// 深色主题，青色点缀。所有样式集中在一个 LookAndFeel 里：
// 颜色在构造函数里一次性设好，再对默认样式表达不了的少数控件（细滑块、
// 开关式按钮、面板式分组框）重写绘制函数。

#include <JuceHeader.h>

namespace v4h
{
namespace theme
{
constexpr uint32_t background = 0xff14161b; // 窗口
constexpr uint32_t header = 0xff1b1e25; // 标题栏
constexpr uint32_t panel = 0xff1e212a; // 分组背景
constexpr uint32_t panelLight = 0xff262a35; // 按钮、悬停
constexpr uint32_t outline = 0xff2c313d; // 边框
constexpr uint32_t field = 0xff12141a; // 文本框、下拉框
constexpr uint32_t accent = 0xff2dd4bf; // 青色点缀
constexpr uint32_t accentBright = 0xff7ce8db;
constexpr uint32_t text = 0xffc9cdd6;
constexpr uint32_t textBright = 0xffedeff4;
constexpr uint32_t textDim = 0xff878d99;
constexpr uint32_t gr = 0xfffb923c; // 增益衰减琥珀色（行业惯例）

inline juce::Colour c(uint32_t v) { return juce::Colour(v); }
} // namespace theme

// 通用格式化：1234 Hz -> "1.23 kHz"，870 -> "870 Hz"（去掉末尾多余的 0）。
inline juce::String freqText(float f)
{
    if (f >= 1000.0f)
    {
        auto s = juce::String(f / 1000.0f, 2);
        while (s.containsChar('.') && (s.endsWithChar('0') || s.endsWithChar('.')))
            s = s.dropLastCharacters(1);
        return s + " kHz";
    }
    return juce::String(juce::roundToInt(f)) + " Hz";
}

class ViperLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ViperLookAndFeel()
    {
        using namespace theme;
        using juce::LookAndFeel_V4;

        setColour(juce::ResizableWindow::backgroundColourId, c(background));
        // 纯文本标签（背景透明）；滑块文本框自己按下面的
        // Slider::textBox* 颜色 id 来画。
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

    // 拇指半径 7 使 JUCE 的 sliderPos 映射与下面的绘制保持一致
    // （拇指正好走到轨道两端）。
    int getSliderThumbRadius(juce::Slider&) override { return 7; }

    // 细圆角轨道，填充到圆拇指为止。
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

        // JUCE 把 min/max 映射到组件全宽；不加上面的钳制的话，
        // 拇指在最小/最大值时会越出轨道端点一个半径。
        const float pos = juce::jlimit(left, right, sliderPos);

        g.setColour(c(outline));
        g.fillRoundedRectangle(track, radius);

        auto filled = track.withRight(juce::jlimit(track.getX(), track.getRight(), pos));
        if (filled.getWidth() > 0.0f)
        {
            g.setColour(c(accent));
            g.fillRoundedRectangle(filled, radius);
        }

        g.setColour(c(textBright));
        g.fillEllipse(pos - 6.5f, cy - 6.5f, 13.0f, 13.0f);
        g.setColour(c(accent));
        g.drawEllipse(pos - 6.5f, cy - 6.5f, 13.0f, 13.0f, 2.0f);
    }

    // 圆角下拉框加一个小箭头；悬停/聚焦时描边用点缀色。
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

        // 箭头居中于右缘。
        auto arrow = juce::Rectangle<float>(width - 22.0f, height * 0.5f - 3.5f, 8.0f, 7.0f);
        juce::Path p;
        p.startNewSubPath(arrow.getX(), arrow.getY());
        p.lineTo(arrow.getCentreX(), arrow.getBottom());
        p.lineTo(arrow.getRight(), arrow.getY());
        g.setColour(isButtonDown ? c(textBright) : c(textDim));
        g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    // 开关式切换按钮：胶囊轨道 + 滑动圆钮，不画文字（文字由标签负责）。
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool over, bool) override
    {
        using namespace theme;
        auto bounds = button.getLocalBounds().toFloat().withSizeKeepingCentre(40.0f, 20.0f);
        const bool on = button.getToggleState();

        // 填充先内缩，让 1px 描边正好贴着填充轮廓——直接画在原始
        // 边界上的描边会带出一圈硬光晕，看起来是方的。
        auto pill = bounds.reduced(0.75f);
        g.setColour(c(on ? accent : panelLight));
        if (over)
            g.setColour(c(on ? accentBright : outline).brighter(0.08f));
        g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
        g.setColour(c(on ? accentBright : outline).withAlpha(0.6f));
        g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.0f);

        const float knob = 15.0f;
        const float x = on ? pill.getRight() - knob - 2.0f : pill.getX() + 2.0f;
        g.setColour(c(textBright));
        g.fillEllipse(x, pill.getCentreY() - knob * 0.5f, knob, knob);
    }

    // 面板式分组框：标题坐在顶部边框线上，fieldset 风格。边框是一段
    // 在标题处留缺口的路径——文字背后不填背景，卡片填充从边框线开始。
    void drawGroupComponentOutline(juce::Graphics& g, int width, int height,
        const juce::String& text, const juce::Justification&, juce::GroupComponent&) override
    {
        using namespace theme;
        const float cs = 7.0f; // 圆角半径
        const float textH = 16.0f;
        const float yTop = textH * 0.5f; // 边框边缘穿过标题文字中心
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
