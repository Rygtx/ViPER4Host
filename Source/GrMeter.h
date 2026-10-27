#pragma once

// Horizontal gain-reduction meter (FET compressor / output limiter). Fills
// left-to-right as more reduction is applied; slim peak-hold tick marks the
// recent maximum. 0..-24 dB range.

#include "Theme.h"

#include <JuceHeader.h>

namespace v4h
{

class GrMeter : public juce::Component
{
public:
    GrMeter() = default; // the non-copyable macro below suppresses the implicit one
    void setLevel(float reductionDb) // 0 = none, -24 = full scale
    {
        current = juce::jlimit(0.0f, 24.0f, -reductionDb);
        peak = juce::jmax(peak * 0.97f, current); // hold-and-fall
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
        g.drawText("GR", r.getX() + 4.0f, r.getY(), 20.0f, r.getHeight(),
            juce::Justification::centredLeft);

        // -6 / -12 / -18 dB ticks
        for (int db = 6; db < 24; db += 6)
        {
            const float x = track.getX() + track.getWidth() * db / 24.0f;
            g.setColour(c(outline));
            g.fillRect(x, track.getY(), 1.0f, track.getHeight());
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
            g.fillRect(px - 1.0f, track.getY() - 2.0f, 2.0f, track.getHeight() + 4.0f);
        }

        g.setColour(c(textDim));
        g.drawText("24", track.getRight() - 18.0f, track.getBottom(), 18.0f, 10.0f,
            juce::Justification::centredRight);
    }

private:
    float current = 0.0f, peak = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GrMeter)
};

} // namespace v4h
