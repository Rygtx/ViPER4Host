#pragma once

// 专业深色主题（参考 FabFilter / TDR 一类商业插件的干净深色界面）：
// 近黑的蓝灰底 + 单一青色点缀；分组画成"卡片"（标题栏 + 正文 + 发丝线），
// 页签是无边框文字 + 底部短线，滑块走细轨道 + 白色圆钮，开关是胶囊。
// 所有样式集中在一个 LookAndFeel 里：颜色在构造函数里一次性设好，再对
// 默认样式表达不了的少数控件（细滑块、开关、卡片分组、页签按钮）重写绘制。

#include <JuceHeader.h>

namespace v4h
{
namespace theme
{
constexpr uint32_t background = 0xff0e1013; // 窗口
constexpr uint32_t backgroundHi = 0xff14171c; // 窗口渐变亮端
constexpr uint32_t header = 0xff14171c; // 顶栏
constexpr uint32_t panel = 0xff171a20; // 卡片正文
constexpr uint32_t panelHeader = 0xff1e232b; // 卡片标题栏
constexpr uint32_t panelLight = 0xff252b35; // 按钮、悬停
constexpr uint32_t outline = 0xff282e38; // 卡片边框
constexpr uint32_t hairline = 0xff212630; // 分隔发丝线
constexpr uint32_t field = 0xff0a0c0f; // 输入框、下拉框、曲线图
constexpr uint32_t accent = 0xff2fd6bd; // 青色点缀
constexpr uint32_t accentBright = 0xff8defe0;
constexpr uint32_t accentDark = 0xff159c8a;
constexpr uint32_t text = 0xffb6bdc8;
constexpr uint32_t textBright = 0xffedf0f4;
constexpr uint32_t textDim = 0xff7a8290;
constexpr uint32_t gr = 0xfff5a86a; // 增益衰减琥珀色（行业惯例）
constexpr uint32_t danger = 0xffe0564a; // 告警（GR 超量程）

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

// 页签按钮：无边框纯文字，激活态 = 亮色文字 + 底部青色短线。
class TabButton final : public juce::TextButton
{
public:
    void paintButton(juce::Graphics& g, bool over, bool /*down*/) override
    {
        using namespace theme;
        auto b = getLocalBounds().toFloat();
        auto font = juce::FontOptions(13.0f,
            getToggleState() ? juce::Font::bold : juce::Font::plain);
        g.setFont(getLookAndFeel().withDefaultMetrics(font));
        // 注意：Button 有同名私有成员 text，这里必须用 theme:: 限定。
        g.setColour(getToggleState() ? c(theme::textBright) : (over ? c(theme::text) : c(theme::textDim)));
        g.drawText(getButtonText(), b, juce::Justification::centred);
        if (getToggleState())
        {
            g.setColour(c(accent));
            g.fillRoundedRectangle(b.getCentreX() - 14.0f, b.getBottom() - 4.0f, 28.0f, 2.5f, 1.25f);
        }
    }
};

class ViperLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ViperLookAndFeel()
    {
        using namespace theme;
        using juce::LookAndFeel_V4;

        setColour(juce::ResizableWindow::backgroundColourId, c(background));
        // 纯文本标签（背景透明）；滑块数值框走"数值即文字"：无框透明底 +
        // 亮色文字（颜色 id 在下面 Slider::textBox* 里设）。
        setColour(juce::Label::textColourId, c(text));
        setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::textWhenEditingColourId, c(textBright));
        setColour(juce::Label::backgroundWhenEditingColourId, c(field));

        setColour(juce::Slider::textBoxTextColourId, c(textBright));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, c(accent).withAlpha(0.35f));
        setColour(juce::Slider::trackColourId, c(accent));

        setColour(juce::ComboBox::backgroundColourId, c(field));
        setColour(juce::ComboBox::outlineColourId, c(hairline));
        setColour(juce::ComboBox::textColourId, c(text));
        setColour(juce::ComboBox::arrowColourId, c(textDim));
        setColour(juce::ComboBox::focusedOutlineColourId, c(accent).withAlpha(0.7f));

        setColour(juce::PopupMenu::backgroundColourId, c(panel));
        setColour(juce::PopupMenu::textColourId, c(text));
        setColour(juce::PopupMenu::headerTextColourId, c(textBright));
        setColour(juce::PopupMenu::highlightedBackgroundColourId, c(accent).withAlpha(0.28f));
        setColour(juce::PopupMenu::highlightedTextColourId, c(textBright));

        setColour(juce::TextButton::buttonColourId, c(panelLight));
        setColour(juce::TextButton::buttonOnColourId, c(accent));
        setColour(juce::TextButton::textColourOffId, c(text));
        setColour(juce::TextButton::textColourOnId, c(textBright));

        setColour(juce::TextEditor::backgroundColourId, c(field));
        setColour(juce::TextEditor::outlineColourId, c(hairline));
        setColour(juce::TextEditor::focusedOutlineColourId, c(accent).withAlpha(0.6f));
        setColour(juce::TextEditor::textColourId, c(textBright));
        setColour(juce::TextEditor::highlightColourId, c(accent).withAlpha(0.35f));

        setColour(juce::AlertWindow::backgroundColourId, c(panel));
        setColour(juce::AlertWindow::outlineColourId, c(outline));
        setColour(juce::AlertWindow::textColourId, c(text));

        setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::ScrollBar::thumbColourId, c(panelLight));
    }

    // --- 统一字号：标签 / 按钮 / 下拉 / 菜单 13px --------------------------

    juce::Font getLabelFont(juce::Label&) override { return withDefaultMetrics(juce::FontOptions(13.0f)); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return withDefaultMetrics(juce::FontOptions(13.0f)); }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return withDefaultMetrics(juce::FontOptions(13.0f)); }
    juce::Font getPopupMenuFont() override { return withDefaultMetrics(juce::FontOptions(13.0f)); }
    juce::Font getAlertWindowMessageFont() override { return withDefaultMetrics(juce::FontOptions(13.5f)); }

    // 滑块数值编辑框（JUCE 里它是个 Label）：13px。
    juce::Label* createSliderTextBox(juce::Slider& slider) override
    {
        auto* t = LookAndFeel_V4::createSliderTextBox(slider);
        t->setFont(withDefaultMetrics(juce::FontOptions(13.0f)));
        return t;
    }

    // 拇指半径 6，与下面的绘制保持一致（拇指正好走到轨道两端）。
    int getSliderThumbRadius(juce::Slider&) override { return 6; }

    // 细圆角轨道（4px）+ 渐变已填充段 + 白色圆钮。
    void drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
        float /*min*/, float /*max*/, juce::Slider::SliderStyle, juce::Slider& slider) override
    {
        using namespace theme;
        const float cy = static_cast<float>(y) + h * 0.5f;
        const float trackH = 4.0f;
        const float radius = trackH * 0.5f;
        const float margin = static_cast<float>(getSliderThumbRadius(slider));
        const float left = static_cast<float>(x) + margin;
        const float right = static_cast<float>(x + w) - margin;
        auto track = juce::Rectangle<float>(left, cy - radius, juce::jmax(right - left, 0.0f), trackH);

        // JUCE 把 min/max 映射到组件全宽；钳制让拇指在端点时不越出轨道。
        const float pos = juce::jlimit(left, right, sliderPos);

        g.setColour(c(hairline));
        g.fillRoundedRectangle(track, radius);

        auto filled = track.withRight(juce::jlimit(track.getX(), track.getRight(), pos));
        if (filled.getWidth() > 0.5f)
        {
            juce::ColourGradient fill(c(accentDark), filled.getX(), cy, c(accent), filled.getRight(), cy, false);
            g.setGradientFill(fill);
            g.fillRoundedRectangle(filled, radius);
        }

        // 白色圆钮带一点点立体：下半圈用暗青勾边。
        g.setColour(c(textBright));
        g.fillEllipse(pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        g.setColour(c(accent));
        g.drawEllipse(pos - 6.0f, cy - 6.0f, 12.0f, 12.0f, 1.5f);
    }

    // 圆角下拉框 + 右缘小箭头；悬停/聚焦时描边走点缀色。
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
        int, int, int, int, juce::ComboBox& box) override
    {
        using namespace theme;
        auto bounds = box.getLocalBounds().toFloat().reduced(0.5f);
        const bool highlight = box.isMouseOver() || box.hasKeyboardFocus(true) || isButtonDown;

        g.setColour(c(field));
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(highlight ? c(accent).withAlpha(0.7f) : c(hairline));
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);

        auto arrow = juce::Rectangle<float>(width - 21.0f, height * 0.5f - 3.0f, 8.0f, 6.0f);
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
        auto bounds = button.getLocalBounds().toFloat().withSizeKeepingCentre(38.0f, 18.0f);
        const bool on = button.getToggleState();

        // 填充先内缩，让 1px 描边正好贴着填充轮廓。
        auto pill = bounds.reduced(0.75f);
        if (on)
        {
            juce::ColourGradient grad(c(accentBright), 0.0f, pill.getY(), c(accentDark), 0.0f, pill.getBottom(), false);
            g.setGradientFill(grad);
        }
        else
            g.setColour(over ? c(outline).brighter(0.08f) : c(outline));
        g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
        g.setColour(c(on ? accentBright : outline).withAlpha(0.6f));
        g.drawRoundedRectangle(pill, pill.getHeight() * 0.5f, 1.0f);

        const float knob = 14.0f;
        const float kx = on ? pill.getRight() - knob - 2.0f : pill.getX() + 2.0f;
        g.setColour(c(textBright));
        g.fillEllipse(kx, pill.getCentreY() - knob * 0.5f, knob, knob);
    }

    // 卡片式分组：标题栏（左缀青色小竖条 + 名称）+ 发丝分隔线 + 正文。
    // 标题栏高度 30 必须与 PluginEditor.cpp 的 titleHeight 一致。
    void drawGroupComponentOutline(juce::Graphics& g, int width, int height,
        const juce::String& text, const juce::Justification&, juce::GroupComponent& group) override
    {
        using namespace theme;
        constexpr float cs = 8.0f;
        constexpr float headerH = 30.0f;

        auto card = juce::Rectangle<float>(0.5f, 0.5f, width - 1.0f, height - 1.0f);
        g.setColour(c(panel));
        g.fillRoundedRectangle(card, cs);

        // 标题栏：圆角矩形向下多画 8px，再用正文色盖掉底部圆角。
        // 四角全圆仅限"只剩标题栏"的分组。折叠状态由编辑器通过组件属性
        // 传入（refreshCollapse 里维护），不靠卡片高度猜。
        const bool titleOnly = group.getProperties().getVarPointer("v4hCollapsed") != nullptr;
        auto hdr = card.removeFromTop(juce::jmin(headerH, card.getHeight()));
        g.setColour(c(panelHeader));
        if (titleOnly)
        {
            g.fillRoundedRectangle(hdr, cs);
        }
        else
        {
            // 标题栏：圆角矩形向下多画 8px，再用正文色盖掉底部圆角。
            g.fillRoundedRectangle(hdr.withHeight(hdr.getHeight() + cs), cs);
            g.setColour(c(panel));
            g.fillRect(juce::Rectangle<float>(hdr.getX(), hdr.getBottom(), hdr.getWidth(), cs));
            g.setColour(c(hairline));
            g.fillRect(juce::Rectangle<float>(hdr.getX(), hdr.getBottom() - 0.5f, hdr.getWidth(), 1.0f));
        }

        // card 已被 removeFromTop 改成正文区域；外框必须按整卡描，
        // 否则正文顶部会出现与标题栏接不上的两个圆角。
        g.setColour(c(outline));
        g.drawRoundedRectangle(juce::Rectangle<float>(0.5f, 0.5f, width - 1.0f, height - 1.0f), cs, 1.0f);

        if (text.isNotEmpty())
        {
            // 折叠成只剩标题栏的窄卡片不画点缀条，避免与开关挤在一起。
            g.setColour(c(accent));
            g.fillRoundedRectangle(12.0f, hdr.getCentreY() - 6.0f, 3.0f, 12.0f, 1.5f);
            g.setFont(withDefaultMetrics(juce::FontOptions(12.5f, juce::Font::bold)));
            g.setColour(c(textBright));
            g.drawText(text, juce::Rectangle<float>(23.0f, hdr.getY(),
                static_cast<float>(width) - 70.0f, headerH), juce::Justification::centredLeft);
        }
    }

    // 普通按钮：静默 = 面板色 + 发丝描边；按下压暗；激活态 = 青色渐变胶囊。
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour&, bool over, bool down) override
    {
        using namespace theme;
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        if (button.getToggleState())
        {
            juce::ColourGradient grad(c(accentBright), 0.0f, bounds.getY(),
                c(accentDark), 0.0f, bounds.getBottom(), false);
            g.setGradientFill(grad);
            g.fillRoundedRectangle(bounds, 5.0f);
            return;
        }
        g.setColour(down ? c(outline) : (over ? c(panelLight).brighter(0.07f) : c(panelLight)));
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(down ? c(outline) : c(hairline));
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
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
