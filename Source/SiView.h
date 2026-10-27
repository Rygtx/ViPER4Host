#pragma once

// 立体声声像器的场视图：经典的三频段立体声显示，兼任编辑器。频率沿纵轴
// 向上；两条横线分别是低/中、中/高分频点（纵向拖动）。每个频段横条的
// 宽度就是它的声场宽度（横向拖动）：中心 = 1.0 不变，左缘 = 单声道 (0)，
// 右缘 = 翻倍 (2)。
//
// 取代该分组的五排滑块；区域标签的语言每次绘制时从 APVTS 状态读取，
// 所以 relabelAll 不需要为它写特例。

#include "Localisation.h"
#include "Theme.h"

#include <JuceHeader.h>
#include <cmath>

namespace v4h
{

class SiView : public juce::Component
{
public:
    explicit SiView(juce::AudioProcessorValueTreeState& state)
        : apvts(state)
    {
        setMouseClickGrabsKeyboardFocus(false);
    }

private:
    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
    static constexpr const char* widthIds[3] = { "siLowWidth", "siMidWidth", "siHighWidth" };
    static constexpr const char* zoneKeys[3] = { "siZoneLow", "siZoneMid", "siZoneHigh" };

    juce::RangedAudioParameter* rp(const juce::String& id) const
    {
        return dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    }
    float valueOf(const juce::String& id) const
    {
        auto* p = rp(id);
        return p != nullptr ? p->convertFrom0to1(p->getValue()) : 1.0f;
    }
    void setValue(const juce::String& id, float v)
    {
        if (auto* p = rp(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    float widthOf(int zone) const { return juce::jlimit(0.0f, 2.0f, valueOf(widthIds[zone])); }
    float lowCross() const { return valueOf("siLowCrossover"); }
    float highCross() const { return valueOf("siHighCrossover"); }

    juce::Rectangle<float> fieldRect() const
    {
        return getLocalBounds().toFloat().reduced(6.0f);
    }
    // 频率沿纵轴从底部 (20 Hz) 到顶部 (20 kHz)，与频谱图一致。
    float yOf(double f) const
    {
        const auto r = fieldRect();
        return r.getBottom() - (float) ((std::log10(f) - std::log10(minFreq))
            / (std::log10(maxFreq) - std::log10(minFreq)) * r.getHeight());
    }
    double freqOf(float y) const
    {
        const auto r = fieldRect();
        return minFreq * std::pow(10.0, (r.getBottom() - y) / r.getHeight()
            * (std::log10(maxFreq) - std::log10(minFreq)));
    }
    // y 落在哪个频段？0 = 低频，1 = 中频，2 = 高频。
    int zoneAt(float y) const
    {
        return y < yOf(highCross()) ? 2 : (y < yOf(lowCross()) ? 1 : 0);
    }

    // --- 鼠标 -------------------------------------------------------------

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto r = fieldRect();
        const float dLow = std::abs(e.position.y - yOf(lowCross()));
        const float dHigh = std::abs(e.position.y - yOf(highCross()));
        if (dLow < 8.0f && dLow <= dHigh)
            dragCross = 1; // 低/中分频线
        else if (dHigh < 8.0f)
            dragCross = 2; // 中/高分频线
        else
            dragZone = zoneAt(e.position.y);
        if (dragCross <= 0 && dragZone < 0)
            return;
        gesture(true);
        if (dragZone >= 0)
            setValue(widthIds[dragZone], widthFromX(e.position.x, r));
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const auto r = fieldRect();
        if (dragCross == 1)
            setValue("siLowCrossover", (float) juce::jlimit(80.0, 400.0, freqOf(e.position.y)));
        else if (dragCross == 2)
            setValue("siHighCrossover", (float) juce::jlimit(2000.0, 8000.0, freqOf(e.position.y)));
        else if (dragZone >= 0)
            setValue(widthIds[dragZone], widthFromX(e.position.x, r));
        else
            return;
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        gesture(false);
        dragCross = dragZone = -1;
    }

    static float widthFromX(float x, const juce::Rectangle<float>& r)
    {
        // 左缘 -> 0（单声道），中心 -> 1（不变），右缘 -> 2
        return juce::jlimit(0.0f, 2.0f, 2.0f * (x - r.getX()) / r.getWidth());
    }

    void gesture(bool begin)
    {
        for (const auto* id : { "siLowCrossover", "siHighCrossover" })
            if (dragCross > 0 || !begin)
                if (auto* p = rp(id))
                    begin ? p->beginChangeGesture() : p->endChangeGesture();
        if (dragZone >= 0)
            if (auto* p = rp(widthIds[dragZone]))
                begin ? p->beginChangeGesture() : p->endChangeGesture();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const auto r = fieldRect();
        const float dLow = std::abs(e.position.y - yOf(lowCross()));
        const float dHigh = std::abs(e.position.y - yOf(highCross()));
        const int cros = dLow < 8.0f && dLow <= dHigh ? 1 : (dHigh < 8.0f ? 2 : -1);
        const int zone = cros > 0 ? -1 : zoneAt(e.position.y);
        setMouseCursor(cros > 0 ? juce::MouseCursor::UpDownResizeCursor
                                : juce::MouseCursor::PointingHandCursor);
        if (zone != hoverZone || cros != hoverCross)
        {
            hoverZone = zone;
            hoverCross = cros;
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverZone = hoverCross = -1;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        // 只有宽度会重置；分频线没有有意义的公共默认值。
        if (std::abs(e.position.y - yOf(lowCross())) < 8.0f
            || std::abs(e.position.y - yOf(highCross())) < 8.0f)
            return;
        const int zone = zoneAt(e.position.y);
        if (zone >= 0)
            if (auto* p = rp(widthIds[zone]))
                p->setValueNotifyingHost(p->getDefaultValue()); // 回到 1.0
    }

    // --- 绘制 -------------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        using namespace theme;
        const auto lang = apvts.state.getProperty("uiLanguage", "en").toString();
        const auto r = fieldRect();

        g.setColour(c(field));
        g.fillRoundedRectangle(r, 5.0f);

        // 中线（单声道参考）
        g.setColour(c(outline).brighter(0.2f));
        g.fillRect(juce::Rectangle<float>(r.getCentreX(), r.getY(), 1.0f, r.getHeight()));

        // 频段：区域标签 + 从中心展开的宽度条
        {
            juce::Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(r.toNearestInt()); // 宽度 > 1 时会越出场区
            for (int zone = 0; zone < 3; ++zone)
            {
                const float yTop = zone == 2 ? r.getY() : yOf(zone == 1 ? highCross() : lowCross());
                const float yBottom = zone == 0 ? r.getBottom() : yOf(zone == 1 ? lowCross() : highCross());
                const auto zoneRect = juce::Rectangle<float>(r.getX(), yTop, r.getWidth(), yBottom - yTop);
                const float halfW = widthOf(zone) * r.getWidth() * 0.5f;

                g.setColour(c(accent).withAlpha(zone == hoverZone || zone == dragZone ? 0.28f : 0.14f));
                g.fillRoundedRectangle(r.getCentreX() - halfW, zoneRect.getCentreY() - 9.0f,
                    halfW * 2.0f, 18.0f, 4.0f);
                g.setColour(c(accent).withAlpha(zone == hoverZone || zone == dragZone ? 0.9f : 0.5f));
                g.drawRoundedRectangle(r.getCentreX() - halfW, zoneRect.getCentreY() - 9.0f,
                    halfW * 2.0f, 18.0f, 4.0f, 1.4f);
            }
        }

        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        for (int zone = 0; zone < 3; ++zone)
        {
            const float yTop = zone == 2 ? r.getY() : yOf(zone == 1 ? highCross() : lowCross());
            const float yBottom = zone == 0 ? r.getBottom() : yOf(zone == 1 ? lowCross() : highCross());
            g.setColour(c(textDim));
            g.drawText(v4h::localisation::tr(lang, zoneKeys[zone]),
                juce::Rectangle<float>(r.getX() + 6.0f, yTop, 60.0f, yBottom - yTop),
                juce::Justification::centredLeft);
        }

        // 分频线（纵向拖动）
        for (int line = 1; line <= 2; ++line)
        {
            const float y = yOf(line == 1 ? lowCross() : highCross());
            const bool active = line == dragCross || line == hoverCross;
            g.setColour(active ? c(accentBright) : c(textBright));
            g.fillRect(juce::Rectangle<float>(r.getX() + 3.0f, y - 1.0f, r.getWidth() - 6.0f, 2.0f));
            g.fillEllipse(r.getX() + 3.0f, y - 4.0f, 8.0f, 8.0f);
            g.fillEllipse(r.getRight() - 11.0f, y - 4.0f, 8.0f, 8.0f);
        }

        // 读数：悬停频段的宽度，或分频频率
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.setColour(c(textBright));
        juce::String readout;
        if (dragCross > 0 || hoverCross > 0)
        {
            const int line = dragCross > 0 ? dragCross : hoverCross;
            const float f = line == 1 ? lowCross() : highCross();
            readout = freqText(f);
        }
        else if (dragZone >= 0 || hoverZone >= 0)
        {
            const int zone = dragZone >= 0 ? dragZone : hoverZone;
            readout = v4h::localisation::tr(lang, zoneKeys[zone]) + "  "
                + juce::String(widthOf(zone), 2) + "x";
        }
        if (readout.isNotEmpty())
            g.drawText(readout,
                juce::Rectangle<float>(r.getX(), r.getY() + 2.0f, r.getWidth() - 8.0f, 14.0f),
                juce::Justification::centredRight);
    }

    juce::AudioProcessorValueTreeState& apvts;
    int dragCross = -1, dragZone = -1, hoverCross = -1, hoverZone = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SiView)
};

} // namespace v4h
