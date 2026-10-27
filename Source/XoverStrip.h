#pragma once

// Multiband-compressor crossover strip: a log-frequency bar whose draggable
// handles are the crossover points; the regions between them are the bands,
// and clicking a region selects that band (the editor rebinds its detail rows
// to it). Same interaction family as EqCurve, but no gain axis — the vertical
// dimension only carries the handles.

#include "Theme.h"

#include <JuceHeader.h>
#include <cmath>
#include <vector>

namespace v4h
{

class XoverStrip : public juce::Component
{
public:
    explicit XoverStrip(juce::AudioProcessorValueTreeState& state)
        : apvts(state)
    {
        setMouseClickGrabsKeyboardFocus(false);
    }

    // `bands` regions need bands-1 crossover params (mbcCross1..N).
    void setBands(int bands)
    {
        bandCount = juce::jlimit(1, 5, bands);
        selected = juce::jlimit(0, bandCount - 1, selected);
        attachments.clear();
        for (int c = 1; c < bandCount; ++c)
        {
            attachments.push_back(std::make_unique<juce::ParameterAttachment>(
                *apvts.getParameter("mbcCross" + juce::String(c)),
                [this](float) { repaint(); }, nullptr));
            attachments.back()->sendInitialUpdate();
        }
        repaint();
    }

    void setSelected(int band)
    {
        selected = juce::jlimit(0, bandCount - 1, band);
        repaint();
    }

    std::function<void(int)> onSelect;

private:
    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f;
    static constexpr int barH = 34; // interactive bar height inside the strip

    juce::RangedAudioParameter* rp(const juce::String& id) const
    {
        return dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    }
    float crossValue(int c) const
    {
        auto* p = rp("mbcCross" + juce::String(c));
        return p != nullptr ? p->convertFrom0to1(p->getValue()) : 1000.0f;
    }
    void setCross(int c, float f)
    {
        if (auto* p = rp("mbcCross" + juce::String(c)))
            p->setValueNotifyingHost(p->convertTo0to1(f));
    }

    juce::Rectangle<float> bar() const
    {
        auto b = getLocalBounds().toFloat();
        return b.withY((b.getHeight() - barH) * 0.5f).withHeight((float) barH)
            .withTrimmedLeft(6.0f).withTrimmedRight(6.0f);
    }
    float xOf(double f) const
    {
        const auto b = bar();
        return b.getX() + (float) ((std::log10(f) - std::log10(minFreq))
            / (std::log10(maxFreq) - std::log10(minFreq)) * b.getWidth());
    }
    double freqOf(float x) const
    {
        const auto b = bar();
        return minFreq * std::pow(10.0, (x - b.getX()) / b.getWidth()
            * (std::log10(maxFreq) - std::log10(minFreq)));
    }
    // Frequency of a handle clamped between its neighbours.
    float clampedFreq(int c, float f) const
    {
        const float lo = c > 1 ? crossValue(c - 1) : minFreq;
        const float hi = c < bandCount - 1 ? crossValue(c + 1) : maxFreq;
        return juce::jlimit(lo, hi, f);
    }

    int pickHandle(juce::Point<float> pos) const
    {
        const auto b = bar();
        if (pos.y < b.getY() - 6.0f || pos.y > b.getBottom() + 6.0f)
            return -1;
        int best = -1;
        float bestD = 12.0f * 12.0f;
        for (int c = 1; c < bandCount; ++c)
        {
            const float d = std::abs(pos.x - xOf(crossValue(c)));
            if (d * d < bestD)
            {
                bestD = d * d;
                best = c;
            }
        }
        return best;
    }
    int pickBand(juce::Point<float> pos) const
    {
        for (int b = 0; b < bandCount; ++b)
        {
            const float lo = b == 0 ? 0.0f : xOf(crossValue(b));
            const float hi = b == bandCount - 1 ? (float) getWidth() : xOf(crossValue(b + 1));
            if (pos.x >= lo && pos.x < hi)
                return b;
        }
        return -1;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragCross = pickHandle(e.position);
        if (dragCross > 0)
        {
            if (auto* p = rp("mbcCross" + juce::String(dragCross)))
                p->beginChangeGesture();
        }
        else
        {
            const int band = pickBand(e.position);
            if (band >= 0 && band != selected)
            {
                selected = band;
                if (onSelect)
                    onSelect(selected);
                repaint();
            }
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragCross <= 0)
            return;
        setCross(dragCross,
            (float) juce::jlimit(minFreq, maxFreq, clampedFreq(dragCross, (float) freqOf(e.position.x))));
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragCross > 0)
            if (auto* p = rp("mbcCross" + juce::String(dragCross)))
                p->endChangeGesture();
        dragCross = -1;
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        hoverCross = dragCross > 0 ? dragCross : pickHandle(e.position);
        hoverBand = hoverCross > 0 ? -1 : pickBand(e.position);
        setMouseCursor(hoverCross > 0 ? juce::MouseCursor::LeftRightResizeCursor
                                      : juce::MouseCursor::PointingHandCursor);
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoverBand = hoverCross = -1;
        repaint();
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const int c = pickHandle(e.position);
        if (c <= 0)
            return;
        if (auto* p = rp("mbcCross" + juce::String(c)))
            p->setValueNotifyingHost(p->getDefaultValue());
    }

    static juce::String freqText(float f)
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

    void paint(juce::Graphics& g) override
    {
        using namespace theme;
        const auto b = bar();
        g.setColour(c(field));
        g.fillRoundedRectangle(b, 5.0f);

        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const float fTicks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        const char* fLabels[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        for (int i = 0; i < 8; ++i)
        {
            const float x = xOf(fTicks[i]);
            g.setColour(c(outline));
            g.fillRect(x, b.getY(), 1.0f, b.getHeight());
            g.setColour(c(textDim));
            g.drawText(fLabels[i], juce::Rectangle<float>(x - 20.0f, b.getBottom(), 40.0f, 13.0f),
                juce::Justification::centred);
        }

        // bands: number centred in each region; selected region gets a wash
        for (int band = 0; band < bandCount; ++band)
        {
            const float lo = band == 0 ? b.getX() : xOf(crossValue(band));
            const float hi = band == bandCount - 1 ? b.getRight() : xOf(crossValue(band + 1));
            if (band == selected)
            {
                g.setColour(c(accent).withAlpha(0.12f));
                g.fillRect(lo, b.getY(), hi - lo, b.getHeight());
            }
            g.setColour(band == selected ? c(accentBright) : c(textDim));
            g.drawText(juce::String(band + 1), juce::Rectangle<float>(lo, b.getY(), hi - lo, b.getHeight()),
                juce::Justification::centred);
        }

        // crossover handles
        for (int cros = 1; cros < bandCount; ++cros)
        {
            const float x = xOf(crossValue(cros));
            const bool active = cros == dragCross || cros == hoverCross;
            g.setColour(active ? c(accentBright) : c(textBright));
            g.fillRect(x - 1.0f, b.getY() + 3.0f, 2.0f, b.getHeight() - 6.0f);
            g.fillEllipse(x - 4.5f, b.getCentreY() - 4.5f, 9.0f, 9.0f);
            g.setColour(c(accent));
            g.drawEllipse(x - 4.5f, b.getCentreY() - 4.5f, 9.0f, 9.0f, 1.4f);
        }

        // readout for the hovered / dragged handle
        const int rb = dragCross > 0 ? dragCross : hoverCross;
        if (rb > 0)
        {
            g.setColour(c(textBright));
            g.drawText(freqText(crossValue(rb)), b.getX(), 0.0f, b.getWidth(), (float) b.getY() + 2.0f,
                juce::Justification::centredRight);
        }
    }

    juce::AudioProcessorValueTreeState& apvts;
    int bandCount = 0, selected = 0, hoverBand = -1, hoverCross = -1, dragCross = -1;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(XoverStrip)
};

} // namespace v4h
