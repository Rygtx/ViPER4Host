#pragma once

// 横向增益衰减量表（FET 压缩器 / 输出限幅器）。衰减越多，填充越向右延伸；
// 细长的峰值保持刻度线标出最近的最大值。量程右键循环 12/24/48 dB，
// 超出当前量程时填充和标签变红提醒。

#include "Theme.h"

#include <JuceHeader.h>

namespace v4h
{

class GrMeter : public juce::Component
{
public:
    GrMeter() = default; // 下面的禁拷贝宏会连带屏蔽隐式默认构造
    void setLevel(float reductionDb) // 0 = 无衰减，-rangeDb = 满量程
    {
        const float raw = -reductionDb;
        const bool nowOver = raw > rangeDb;
        const float nowCurrent = juce::jlimit(0.0f, rangeDb, raw);
        const float nowPeak = juce::jmax(peak * 0.97f, nowCurrent); // 峰值保持并缓慢回落
        // 数值没变就不重绘；峰值 > 0 时 nowPeak 每次都在回落，不受影响。
        if (nowCurrent == current && nowPeak == peak && nowOver == over)
            return;
        over = nowOver;
        current = nowCurrent;
        peak = nowPeak;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            rangeDb = rangeDb >= 48.0f ? 12.0f : rangeDb * 2.0f; // 12 -> 24 -> 48
            peak = juce::jmin(peak, rangeDb);
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(c(field));
        g.fillRoundedRectangle(r, 4.0f);

        const auto track = r.withTrimmedLeft(26.0f).withTrimmedRight(4.0f)
            .withY(r.getCentreY() - 4.0f).withHeight(8.0f);
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.setColour(c(textDim));
        g.drawText("GR",
            juce::Rectangle<float>(r.getX() + 4.0f, r.getY(), 20.0f, r.getHeight()),
            juce::Justification::centredLeft);

        // 1/4、2/4、3/4 量程刻度
        for (float db = rangeDb * 0.25f; db < rangeDb - 0.5f; db += rangeDb * 0.25f)
        {
            const float x = track.getX() + track.getWidth() * db / rangeDb;
            g.setColour(c(outline));
            g.fillRect(juce::Rectangle<float>(x, track.getY(), 1.0f, track.getHeight()));
        }

        // 超量程时填充变红；峰值线打到满量程后短暂保持红色再随峰值回落
        const bool fillOver = over;
        const bool peakOver = peak >= rangeDb - 0.01f;
        const auto fillColour = c(fillOver ? danger : gr);
        const float w = track.getWidth() * current / rangeDb;
        if (w > 0.5f)
        {
            g.setColour(fillColour);
            g.fillRoundedRectangle(track.getX(), track.getY(), w, track.getHeight(), 3.0f);
        }
        const float px = track.getX() + track.getWidth() * peak / rangeDb;
        if (peak > 0.5f)
        {
            g.setColour(c(peakOver ? danger : gr).brighter(0.3f));
            g.fillRect(juce::Rectangle<float>(px - 1.0f, track.getY() - 2.0f, 2.0f, track.getHeight() + 4.0f));
        }

        g.setColour(c(fillOver ? danger : textDim));
        g.drawText(juce::String(static_cast<int>(rangeDb)),
            juce::Rectangle<float>(track.getRight() - 18.0f, track.getBottom(), 18.0f, 10.0f),
            juce::Justification::centredRight);
    }

private:
    float rangeDb = 24.0f;
    float current = 0.0f, peak = 0.0f;
    bool over = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrMeter)
};

} // namespace v4h
