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
constexpr uint32_t header = 0xff14171c; // 顶栏
constexpr uint32_t panel = 0xff171a20; // 卡片正文
constexpr uint32_t panelHeader = 0xff1e232b; // 卡片标题栏
constexpr uint32_t panelLight = 0xff252b35; // 按钮、悬停
constexpr uint32_t outline = 0xff282e38; // 卡片边框
constexpr uint32_t hairline = 0xff212630; // 分隔发丝线
constexpr uint32_t field = 0xff0a0c0f; // 输入框、下拉框、曲线图
constexpr uint32_t accent = 0xff2fd6bd; // 青色点缀
constexpr uint32_t accentBright = 0xff8defe0;
constexpr uint32_t text = 0xffb6bdc8;
constexpr uint32_t textBright = 0xffedf0f4;
constexpr uint32_t textDim = 0xff7a8290;
constexpr uint32_t gr = 0xfff5a86a; // 增益衰减琥珀色（行业惯例）
constexpr uint32_t danger = 0xffe0564a; // 告警（GR 超量程）
constexpr int comboTextPad = 10; // 下拉框文字左内边距（px）

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

// 页签按钮：无边框纯文字，激活态 = 亮色文字 + 底部青色短线；
// 该页有分组启用中时文字右侧加一个青点（由编辑器通过 setActiveDot 维护）。
class TabButton final : public juce::TextButton
{
public:
    void setActiveDot(bool on)
    {
        if (active == on)
            return;
        active = on;
        repaint();
    }

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
        if (active)
        {
            const float tw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), getButtonText());
            g.setColour(c(accentBright));
            g.fillEllipse(b.getCentreX() + tw * 0.5f + 5.5f, b.getCentreY() - 1.5f, 3.0f, 3.0f);
        }
    }

private:
    bool active = false;
};

// 滑块数值框：和下拉框同一套圆角小字段（深底 + 发丝描边），悬停描边
// 提亮、进入编辑描边走点缀色；内嵌编辑 TextEditor 透明化，边框由字段
// 自己画，编辑态才不会出现尖角叠影。
class ValueField final : public juce::Label
{
public:
    ValueField()
    {
        // 库里默认工厂(SliderLabelComp)定的对齐/键盘类型,替换后要保持一致。
        setJustificationType(juce::Justification::centred);
        setKeyboardType(juce::TextInputTarget::decimalKeyboard);
        setBorderSize(juce::BorderSize<int>(0, 8, 0, 8));
    }

    void paint(juce::Graphics& g) override
    {
        using namespace theme;
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(c(field));
        g.fillRoundedRectangle(b, 5.0f);
        g.setColour(isBeingEdited() ? c(accent).withAlpha(0.6f)
            : isMouseOver(true)     ? c(outline).brighter(0.25f)
                                    : c(hairline));
        g.drawRoundedRectangle(b, 5.0f, 1.0f);
        juce::Label::paint(g); // 文字仍走 LookAndFeel（背景描边都是透明）
    }

    juce::TextEditor* createEditorComponent() override
    {
        auto* ed = juce::Label::createEditorComponent();
        ed->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        ed->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        ed->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        return ed;
    }
};

// 预设入口按钮：画成下拉框的样子（深色小字段 + 发丝描边 + 右侧箭头），
// 点击弹出统一预设菜单。ComboBox 的弹层不支持子菜单，所以用按钮 + PopupMenu。
class PresetButton final : public juce::Button
{
public:
    PresetButton() : juce::Button({}) {}

    // 未载入任何预设时文字用暗色（占位语义）。
    void setPlaceholder(bool on)
    {
        if (placeholder == on)
            return;
        placeholder = on;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool /*over*/, bool /*down*/) override
    {
        using namespace theme;
        auto b = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(c(field));
        g.fillRoundedRectangle(b, 5.0f);
        g.setColour(isMouseOver(true) ? c(outline).brighter(0.25f) : c(hairline));
        g.drawRoundedRectangle(b, 5.0f, 1.0f);
        g.setFont(getLookAndFeel().withDefaultMetrics(juce::FontOptions(13.0f)));
        g.setColour(placeholder ? c(textDim) : c(textBright));
        auto textArea = b.reduced(comboTextPad, 0.0f);
        // 右侧留出箭头位置，文字左对齐（与原下拉框同位）。
        textArea.removeFromRight(16.0f);
        g.drawText(getButtonText(), textArea, juce::Justification::centredLeft);
        juce::Path arrow;
        const float cx = b.getRight() - comboTextPad - 5.0f;
        const float cy = b.getCentreY();
        arrow.addTriangle(cx - 5.0f, cy - 2.5f, cx + 5.0f, cy - 2.5f, cx, cy + 2.5f);
        g.setColour(c(textDim));
        g.fillPath(arrow);
    }

private:
    bool placeholder = true;
};

class ViperLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ViperLookAndFeel()
    {
        using namespace theme;
        using juce::LookAndFeel_V4;

        setColour(juce::ResizableWindow::backgroundColourId, c(background));
        // 纯文本标签（背景透明）；滑块数值框画成圆角小字段（见下面
        // ValueField / createSliderTextBox），颜色 id 在 Slider::textBox* 里设。
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

        // 0.99 而非 1.0：弹层窗口按"背景色是否不透明"决定是否逐像素透明
        // （juce_PopupMenu.cpp 的 setOpaque），完全-opaque 的颜色会让圆角
        // 外露出窗口白底；0.99 视觉无差但启用透明。
        setColour(juce::PopupMenu::backgroundColourId, c(panel).withAlpha(0.99f));
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

    // V4 默认把下拉框文字区从 x=1 开始，文字贴左边缘；统一加左内边距。
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override
    {
        LookAndFeel_V4::positionComboBoxText(box, label);
        label.setBounds(label.getX() + theme::comboTextPad, label.getY(),
            label.getWidth() - theme::comboTextPad, label.getHeight());
    }

    // V2 默认占位文字用 label 的"自身坐标"画在下拉框原点，忽略 label 实际位置，
    // 导致它与选中文字不同位；这里按 label 实际区域（含其边距）绘制，
    // 占位与选中两条路径共用 positionComboBoxText 定下的同一套几何。
    void drawComboBoxTextWhenNothingSelected(juce::Graphics& g, juce::ComboBox& box, juce::Label& label) override
    {
        g.setFont(getComboBoxFont(box));
        g.setColour(box.findColour(juce::ComboBox::textColourId).withMultipliedAlpha(0.5f));
        g.drawText(box.getTextWhenNothingSelected(),
            label.getBorderSize().subtractedFrom(label.getBoundsInParent()),
            label.getJustificationType());
    }
    juce::Font getPopupMenuFont() override { return withDefaultMetrics(juce::FontOptions(13.0f)); }
    juce::Font getAlertWindowMessageFont() override { return withDefaultMetrics(juce::FontOptions(13.5f)); }

    // --- 弹层重绘：跟上卡片设计语言（V4 继承的默认样式是直角平铺）---------
    // 圆角面板 + 发丝描边；条目高亮内缩圆角、点缀色；勾选用点缀色。
    // 对全部 PopupMenu 生效（预设菜单、语言/选项下拉等）。

    void drawPopupMenuBackgroundWithOptions(juce::Graphics& g, int width, int height,
        const juce::PopupMenu::Options&) override
    {
        using namespace theme;
        auto b = juce::Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f);
        g.setColour(c(panel));
        g.fillRoundedRectangle(b, 8.0f);
        g.setColour(c(outline));
        g.drawRoundedRectangle(b, 8.0f, 1.0f);
    }

    int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&) override { return 8; }

    // 弹层不要阴影：DropShadower 按方形窗口轮廓画灰晕，圆角 + 逐像素透明后
    // 灰晕会从圆角外露出来形成可见边界；无边框平面风（发丝描边定边界）更干净。
    int getMenuWindowFlags() override { return 0; }

    void drawPopupMenuItemWithOptions(juce::Graphics& g, const juce::Rectangle<int>& area,
        bool isHighlighted, const juce::PopupMenu::Item& item,
        const juce::PopupMenu::Options&) override
    {
        using namespace theme;
        const bool highlighted = isHighlighted && item.isEnabled;
        const bool hasSubMenu = item.subMenu != nullptr
            && (item.itemID == 0 || item.subMenu->getNumItems() > 0);

        if (item.isSeparator)
        {
            g.setColour(c(hairline));
            g.drawHorizontalLine(area.getCentreY(),
                (float) area.getX() + 10.0f, (float) area.getRight() - 10.0f);
            return;
        }

        auto r = area.reduced(3);
        if (highlighted)
        {
            g.setColour(c(accent).withAlpha(0.25f));
            g.fillRoundedRectangle(r.toFloat(), 4.0f);
        }
        if (! item.isEnabled)
            g.setOpacity(0.35f);

        g.setFont(getPopupMenuFont());

        auto iconArea = r.removeFromLeft((r.getHeight() * 5) / 4).reduced(3).toFloat();
        if (item.image != nullptr)
            item.image->drawWithin(g, iconArea,
                juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
        else if (item.isTicked)
        {
            auto tick = getTickShape(1.0f);
            g.setColour(c(accent));
            g.fillPath(tick, tick.getTransformToScaleToFit(iconArea, true));
        }

        if (hasSubMenu)
        {
            const auto arrowH = 0.6f * getPopupMenuFont().getAscent();
            const auto x = (float) r.removeFromRight((int) arrowH).getX();
            const auto halfH = (float) r.getCentreY();
            juce::Path p;
            p.addTriangle(x, halfH - arrowH * 0.5f,
                          x, halfH + arrowH * 0.5f,
                          x + arrowH * 0.6f, halfH);
            g.setColour(c(textDim));
            g.fillPath(p);
        }

        // 勾选/箭头各自改过画笔颜色，画文字前必须显式恢复文字色，
        // 否则被勾选的行整行变成点缀色。
        r.removeFromRight(3);
        g.setColour(highlighted ? c(textBright) : c(text));
        g.drawFittedText(item.text, r, juce::Justification::centredLeft, 1);

        if (item.shortcutKeyDescription.isNotEmpty())
            g.drawText(item.shortcutKeyDescription, r, juce::Justification::centredRight, true);
    }

    // 滑块数值编辑框（JUCE 里它是个 Label）：13px，画成圆角小字段。
    juce::Label* createSliderTextBox(juce::Slider&) override
    {
        auto* t = new ValueField();
        t->setFont(withDefaultMetrics(juce::FontOptions(13.0f)));
        return t;
    }

    // 拇指半径 6，与下面的绘制保持一致（拇指正好走到轨道两端）。
    int getSliderThumbRadius(juce::Slider&) override { return 6; }

    // 细圆角轨道（4px）+ 青色已填充段 + 白色圆钮。
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
            g.setColour(c(accent));
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
        // isMouseOver 必须含子控件:文字 Label 盖满整框,不含的话鼠标扫过
        // 会在 label 与框体间来回亮暗。键盘焦点不算高亮——点击后焦点会一直
        // 留在框上,算进去描边就常亮不灭。
        const bool highlight = box.isMouseOver(true) || isButtonDown;

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
        g.setColour(on ? c(accent) : (over ? c(outline).brighter(0.08f) : c(outline)));
        g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
        g.setColour(c(on ? accent : outline).withAlpha(0.6f));
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

        // 四角全圆仅限"只剩标题栏"的分组（折叠态，或正文为零）。
        // 状态由编辑器通过组件属性传入（refreshCollapse 里维护），不靠卡片高度猜。
        const bool titleOnly = group.getProperties().getVarPointer("v4hTitleOnly") != nullptr;
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
            g.setColour(c(accent));
            g.fillRoundedRectangle(12.0f, hdr.getCentreY() - 6.0f, 3.0f, 12.0f, 1.5f);
            g.setFont(withDefaultMetrics(juce::FontOptions(12.5f, juce::Font::bold)));
            g.setColour(c(textBright));
            g.drawText(text, juce::Rectangle<float>(23.0f, hdr.getY(),
                static_cast<float>(width) - 70.0f, headerH), juce::Justification::centredLeft);
        }
    }

    // 普通按钮：静默 = 面板色 + 发丝描边；按下压暗；激活态 = 青色胶囊。
    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
        const juce::Colour&, bool over, bool down) override
    {
        using namespace theme;
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        if (button.getToggleState())
        {
            g.setColour(c(accent));
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
        // 本地化按钮文字（Копировать、Выбрать файл… 等）可能比按钮宽；
        // drawText 超宽直接裁掉，缩字号塞进去还能认。
        g.drawFittedText(button.getButtonText(), button.getLocalBounds(),
            juce::Justification::centred, 1);
    }
};
} // namespace v4h
