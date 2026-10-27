#pragma once

// 横向增益衰减量表（FET 压缩器 / 输出限幅器）。衰减越多，填充越向右延伸；
// 细长的峰值保持刻度线标出最近的最大值。量程 0..-24 dB。

#include "Theme.h"

#include <JuceHeader.h>

namespace v4h
{

class GrMeter : public juce::Component
{
public:
    GrMeter() = default; // 下面的禁拷贝宏会连带屏蔽隐式默认构造
    void setLevel(float reductionDb) // 0 = 无衰减，-24 = 满量程
    {
        current = juce::jlimit(0.0f, 24.0f, -reductionDb);
        peak = juce::jmax(peak * 0.97f, current); // 峰值保持并缓慢回落
        repaint();
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

        // -6 / -12 / -18 dB 刻度
        for (int db = 6; db < 24; db += 6)
        {
            const float x = track.getX() + track.getWidth() * db / 24.0f;
            g.setColour(c(outline));
            g.fillRect(juce::Rectangle<float>(x, track.getY(), 1.0f, track.getHeight()));
        }

        const float w = track.getWidth() * current / 24.0f;
        if (w > 0.5f)
        {
            g.setColour(c(gr));
            g.fillRoundedRectangle(track.getX(), track.getY(), w, track.getHeight(), 3.0f);
        }
        const float px = track.getX() + track.getWidth() * peak / 24.0f;
        if (peak > 0.5f)
        {
            g.setColour(c(gr).brighter(0.3f));
            g.fillRect(juce::Rectangle<float>(px - 1.0f, track.getY() - 2.0f, 2.0f, track.getHeight() + 4.0f));
        }

        g.setColour(c(textDim));
        g.drawText("24",
            juce::Rectangle<float>(track.getRight() - 18.0f, track.getBottom(), 18.0f, 10.0f),
            juce::Justification::centredRight);
    }

private:
    float current = 0.0f, peak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrMeter)
};

} // namespace v4h
