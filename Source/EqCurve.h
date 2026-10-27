#pragma once

// Frequency-response curve editor shared by the graphic EQ (fixed ISO band
// frequencies, drag = gain) and the dynamic EQ (draggable freq + gain nodes,
// click = select band, wheel = fine gain, double-click = reset gain).
//
// Dynamic mode draws the summed RBJ biquad magnitude of every band; Graphic
// mode interpolates the band points with a monotone cubic (Fritsch-Carlson,
// no overshoot between untouched bands). The drawn curve is a UI aid — the
// parameters remain the single source of truth.

#include "Theme.h"

#include <JuceHeader.h>
#include <cmath>
#include <vector>

namespace v4h
{

class EqCurve : public juce::Component
{
public:
    enum class Mode { Graphic, Dynamic };

    EqCurve(juce::AudioProcessorValueTreeState& state, Mode m)
        : apvts(state), mode(m)
    {
        setMouseClickGrabsKeyboardFocus(false);
    }

    // Binds the first `count` bands: prefix "eqBand" (Graphic, eqBand1..N) or
    // "de" (Dynamic, de1Freq/Q/Gain/Type ...). Called again on band-count
    // changes; rebuilds the parameter attachments.
    void setBands(const juce::String& paramPrefix, int count)
    {
        prefix = paramPrefix;
        bandCount = juce::jlimit(1, 31, count);
        selected = juce::jlimit(0, bandCount - 1, selected);
        attachments.clear();
        const char* suffixes[] = { "Freq", "Q", "Gain" };
        const int perBand = mode == Mode::Dynamic ? 3 : 1;
        for (int b = 0; b < bandCount; ++b)
        {
            for (int s = 0; s < perBand; ++s)
            {
                attachments.push_back(std::make_unique<juce::ParameterAttachment>(
                    *apvts.getParameter(mode == Mode::Dynamic ? bandId(b, suffixes[s])
                                                              : bandId(b, nullptr)),
                    [this](float) { repaint(); }, nullptr));
                attachments.back()->sendInitialUpdate();
            }
        }
        repaint();
    }

    void setSelected(int band)
    {
        selected = juce::jlimit(0, bandCount - 1, band);
        repaint();
    }

    // Fired when the user picks a node in Dynamic mode; the editor rebinds its
    // per-band detail rows to the selected band.
    std::function<void(int)> onSelect;

private:
    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f, maxDb = 12.0f;
    static constexpr float padL = 34.0f, padR = 10.0f, padT = 8.0f, padB = 16.0f;
    // Drawing only — response shape vs sample rate is negligible on a log axis.
    static constexpr double fsDraw = 48000.0;

    juce::String bandId(int band, const char* suffix) const
    {
        return prefix + juce::String(band + 1) + (suffix != nullptr ? suffix : "");
    }
    juce::String gainId(int band) const
    {
        return bandId(band, mode == Mode::Graphic ? nullptr : "Gain");
    }

    juce::RangedAudioParameter* rp(const juce::String& id) const
    {
        return dynamic_cast<juce::RangedAudioParameter*>(apvts.getParameter(id));
    }
    float paramValue(const juce::String& id) const
    {
        auto* p = rp(id);
        return p != nullptr ? p->convertFrom0to1(p->getValue()) : 0.0f;
    }
    void setParam(const juce::String& id, float v)
    {
        if (auto* p = rp(id))
            p->setValueNotifyingHost(p->convertTo0to1(v));
    }
    void gesture(const juce::String& id, bool begin)
    {
        if (auto* p = rp(id))
            begin ? p->beginChangeGesture() : p->endChangeGesture();
    }

    static const float* freqTable(int count)
    {
        static constexpr float f10[] = { 31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
        static constexpr float f15[] = { 25, 40, 63, 100, 160, 250, 400, 630, 1000, 1600,
            2500, 4000, 6300, 10000, 16000 };
        static constexpr float f25[] = { 20, 31.5f, 40, 50, 80, 100, 125, 160, 250, 315,
            400, 500, 800, 1000, 1250, 1600, 2500, 3150, 4000, 5000, 8000, 10000, 12500,
            16000, 20000 };
        static constexpr float f31[] = { 20, 25, 31.5f, 40, 50, 63, 80, 100, 125, 160, 200,
            250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
            6300, 8000, 10000, 12500, 16000, 20000 };
        switch (count)
        {
            case 10: return f10;
            case 15: return f15;
            case 25: return f25;
            default: return f31;
        }
    }

    struct Band // cached snapshot per paint / drag hit test
    {
        double f = 1000.0, g = 0.0, q = 1.0;
        int type = 0;
    };

    std::vector<Band> snapshot() const
    {
        std::vector<Band> bs((size_t) bandCount);
        for (int b = 0; b < bandCount; ++b)
        {
            auto& band = bs[(size_t) b];
            band.f = mode == Mode::Dynamic
                ? juce::jlimit(minFreq, maxFreq, paramValue(bandId(b, "Freq")))
                : (double) freqTable(bandCount)[b];
            band.g = juce::jlimit(-maxDb, maxDb, paramValue(gainId(b)));
            band.q = juce::jlimit(0.3, 16.0, (double) paramValue(bandId(b, "Q")));
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(
                    apvts.getParameter(bandId(b, "Type"))))
                band.type = p->getIndex();
        }
        return bs;
    }

    // --- geometry ----------------------------------------------------------

    juce::Rectangle<float> plot() const
    {
        return getLocalBounds().toFloat()
            .withTrimmedLeft(padL).withTrimmedRight(padR)
            .withTrimmedTop(padT).withTrimmedBottom(padB);
    }
    float xOf(double f) const
    {
        const auto p = plot();
        return p.getX() + (float) ((std::log10(f) - std::log10(minFreq))
            / (std::log10(maxFreq) - std::log10(minFreq)) * p.getWidth());
    }
    double freqOf(float x) const
    {
        const auto p = plot();
        return minFreq * std::pow(10.0, (x - p.getX()) / p.getWidth()
            * (std::log10(maxFreq) - std::log10(minFreq)));
    }
    float yOf(double db) const
    {
        const auto p = plot();
        return p.getCentreY() - (float) (db / maxDb) * (p.getHeight() * 0.5f);
    }
    float dbOf(float y) const
    {
        const auto p = plot();
        return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * maxDb;
    }

    // --- mouse -------------------------------------------------------------

    int pick(juce::Point<float> pos) const
    {
        int best = -1;
        // Graphic: grab anywhere in the band's x column; Dynamic: nearest node.
        float bestD = mode == Mode::Graphic ? 24.0f * 24.0f : 28.0f * 28.0f;
        const auto bs = snapshot();
        for (int b = 0; b < bandCount; ++b)
        {
            const float dx = pos.x - xOf(bs[(size_t) b].f);
            const float dy = pos.y - yOf(bs[(size_t) b].g);
            const float d = mode == Mode::Graphic ? dx * dx : dx * dx + dy * dy;
            if (d < bestD)
            {
                bestD = d;
                best = b;
            }
        }
        return best;
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragBand = pick(e.position);
        if (dragBand < 0)
            return;
        if (mode == Mode::Dynamic && selected != dragBand)
        {
            selected = dragBand;
            if (onSelect)
                onSelect(selected);
        }
        hover = dragBand;
        gesture(gainId(dragBand), true);
        if (mode == Mode::Dynamic)
            gesture(bandId(dragBand, "Freq"), true);
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragBand < 0)
            return;
        setParam(gainId(dragBand), juce::jlimit(-maxDb, maxDb, dbOf(e.position.y)));
        if (mode == Mode::Dynamic)
            setParam(bandId(dragBand, "Freq"),
                juce::jlimit(minFreq, maxFreq, (float) freqOf(e.position.x)));
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (dragBand >= 0)
        {
            gesture(gainId(dragBand), false);
            if (mode == Mode::Dynamic)
                gesture(bandId(dragBand, "Freq"), false);
        }
        dragBand = -1;
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        const int h = dragBand >= 0 ? dragBand : pick(e.position);
        if (h != hover)
        {
            hover = h;
            setMouseCursor(h >= 0 ? juce::MouseCursor::PointingHandCursor
                                  : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        if (hover != dragBand)
        {
            hover = -1;
            repaint();
        }
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        const int b = pick(e.position);
        if (b < 0)
            return;
        if (auto* p = rp(gainId(b)))
            p->setValueNotifyingHost(p->getDefaultValue()); // back to 0 dB
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int b = dragBand >= 0 ? dragBand : pick(e.position);
        if (b < 0 || wheel.deltaY == 0)
            return;
        const auto id = gainId(b);
        const float g = juce::jlimit(-maxDb, maxDb,
            paramValue(id) + (wheel.deltaY > 0 ? 0.5f : -0.5f));
        gesture(id, true);
        setParam(id, g);
        gesture(id, false);
    }

    // --- drawing -----------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        using namespace theme;
        const auto bounds = getLocalBounds().toFloat();
        g.setColour(c(field));
        g.fillRoundedRectangle(bounds, 5.0f);
        if (bandCount == 0)
            return;
        const auto p = plot();
        const auto bs = snapshot();

        // grid: log-frequency ticks + dB lines
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const float fTicks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        const char* fLabels[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        for (int i = 0; i < 8; ++i)
        {
            const float x = xOf(fTicks[i]);
            g.setColour(c(outline));
            g.fillRect(x, p.getY(), 1.0f, p.getHeight());
            g.setColour(c(textDim));
            g.drawText(fLabels[i], juce::Rectangle<float>(x - 20.0f, p.getBottom() + 1.0f,
                40.0f, 12.0f), juce::Justification::centred);
        }
        for (int db = -12; db <= 12; db += 6)
        {
            const float y = yOf(db);
            g.setColour(db == 0 ? c(outline).brighter(0.25f) : c(outline));
            g.fillRect(p.getX(), y, p.getWidth(), 1.0f);
            if (db != 0)
            {
                g.setColour(c(textDim));
                g.drawText(juce::String(db > 0 ? "+" : "") + juce::String(db),
                    juce::Rectangle<float>(0.0f, y - 6.0f, padL - 4.0f, 12.0f),
                    juce::Justification::centredRight);
            }
        }

        // response curve + soft fill under it
        auto path = curvePath(bs);
        juce::Path fill = path;
        fill.lineTo(p.getRight(), p.getCentreY());
        fill.lineTo(p.getX(), p.getCentreY());
        fill.closeSubPath();
        g.setColour(c(accent).withAlpha(0.10f));
        g.fillPath(fill);
        g.setColour(c(accent));
        g.strokePath(path, juce::PathStrokeType(2.0f));

        // band handles
        for (int b = 0; b < bandCount; ++b)
        {
            const float x = xOf(bs[(size_t) b].f), y = yOf(bs[(size_t) b].g);
            const bool active = b == dragBand || b == hover
                || (mode == Mode::Dynamic && b == selected);
            g.setColour(active ? c(accentBright) : c(textBright));
            g.fillEllipse(x - 5.5f, y - 5.5f, 11.0f, 11.0f);
            g.setColour(c(accent));
            g.drawEllipse(x - 5.5f, y - 5.5f, 11.0f, 11.0f, 1.6f);
        }

        // readout for the active band
        const int rb = dragBand >= 0 ? dragBand
            : hover >= 0             ? hover
                                     : (mode == Mode::Dynamic ? selected : -1);
        if (rb >= 0)
        {
            const auto& band = bs[(size_t) rb];
            auto readout = freqText((float) band.f) + "  " + (band.g >= 0 ? "+" : "")
                + juce::String(band.g, 1) + " dB";
            if (mode == Mode::Dynamic)
                readout = juce::String(rb + 1) + ": " + readout;
            g.setColour(c(textBright));
            g.drawText(readout, juce::Rectangle<float>(p.getX(), p.getY() + 2.0f,
                p.getWidth() - 4.0f, 14.0f), juce::Justification::centredRight);
        }
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

    // One RBJ biquad section's magnitude in dB at frequency f.
    static double sectionDb(const Band& band, double f)
    {
        const double A = std::pow(10.0, band.g / 40.0);
        const double q = band.q;
        const double w0 = 2.0 * juce::MathConstants<double>::pi
            * juce::jlimit(minFreq, maxFreq, (float) band.f) / fsDraw;
        const double cw = std::cos(w0), sw = std::sin(w0);
        double b0, b1, b2, a0, a1, a2;
        if (band.type == 0) // peak
        {
            const double al = sw / (2.0 * q);
            b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A;
            a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A;
        }
        else
        {
            const double sa = std::sqrt(A);
            const double al = sw / 2.0 * std::sqrt(2.0); // shelf slope S = 1
            if (band.type == 1) // low shelf
            {
                b0 = A * ((A + 1) - (A - 1) * cw + 2 * sa * al);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - 2 * sa * al);
                a0 = (A + 1) + (A - 1) * cw + sa * al;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - sa * al;
            }
            else // high shelf
            {
                b0 = A * ((A + 1) + (A - 1) * cw + 2 * sa * al);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - 2 * sa * al);
                a0 = (A + 1) - (A - 1) * cw + sa * al;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - sa * al;
            }
        }
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fsDraw;
        const double c1 = std::cos(w), c2 = std::cos(2.0 * w);
        const double num = b0 * b0 + b1 * b1 + b2 * b2
            + 2.0 * (b0 * b1 + b1 * b2) * c1 + 2.0 * b0 * b2 * c2;
        const double den = a0 * a0 + a1 * a1 + a2 * a2
            + 2.0 * (a0 * a1 + a1 * a2) * c1 + 2.0 * a0 * a2 * c2;
        return 10.0 * std::log10(std::max(num, 1e-12) / std::max(den, 1e-12));
    }

    juce::Path curvePath(const std::vector<Band>& bs) const
    {
        juce::Path path;
        const auto p = plot();
        if (mode == Mode::Dynamic)
        {
            const int steps = juce::jmax(2, juce::roundToInt(p.getWidth()));
            path.startNewSubPath(p.getX(), yOf(sumDb(bs, freqOf(p.getX()))));
            for (int s = 1; s <= steps; ++s)
            {
                const float x = p.getX() + p.getWidth() * s / steps;
                path.lineTo(x, yOf(sumDb(bs, freqOf(x))));
            }
            return path;
        }

        // Graphic: monotone cubic through the band points.
        const int n = bandCount;
        if (n < 2)
        {
            path.startNewSubPath(p.getX(), yOf(bs[0].g));
            path.lineTo(p.getRight(), yOf(bs[0].g));
            return path;
        }
        std::vector<juce::Point<float>> pts((size_t) n);
        for (int b = 0; b < n; ++b)
            pts[(size_t) b] = { xOf(bs[(size_t) b].f), yOf(bs[(size_t) b].g) };

        std::vector<float> m((size_t)(n - 1)), t((size_t) n, 0.0f);
        for (int i = 0; i < n - 1; ++i)
            m[(size_t) i] = (pts[(size_t) i + 1].y - pts[(size_t) i].y)
                / (pts[(size_t) i + 1].x - pts[(size_t) i].x);
        t[0] = m[0];
        t[(size_t) n - 1] = m[(size_t) n - 2];
        for (int i = 1; i < n - 1; ++i)
        {
            if (m[(size_t) i - 1] * m[(size_t) i] <= 0)
                t[(size_t) i] = 0;
            else
            {
                const float w1 = 2 * m[(size_t) i - 1] + m[(size_t) i];
                const float w2 = m[(size_t) i - 1] + 2 * m[(size_t) i];
                t[(size_t) i] = (w1 + w2) / (w1 / m[(size_t) i - 1] + w2 / m[(size_t) i]);
            }
        }

        path.startNewSubPath(pts[0]);
        int seg = 0;
        const int steps = juce::jmax(2, juce::roundToInt(p.getWidth()));
        for (int s = 1; s <= steps; ++s)
        {
            const float x = p.getX() + p.getWidth() * s / steps;
            while (seg < n - 2 && x > pts[(size_t) seg + 1].x)
                ++seg;
            const float h = pts[(size_t) seg + 1].x - pts[(size_t) seg].x;
            const float u = (x - pts[(size_t) seg].x) / h;
            const float u2 = u * u, u3 = u2 * u;
            const float y = (2 * u3 - 3 * u2 + 1) * pts[(size_t) seg].y
                + (u3 - 2 * u2 + u) * h * t[(size_t) seg]
                + (-2 * u3 + 3 * u2) * pts[(size_t) seg + 1].y
                + (u3 - u2) * h * t[(size_t) seg + 1];
            path.lineTo(x, y);
        }
        return path;
    }

    static double sumDb(const std::vector<Band>& bs, double f)
    {
        double total = 0.0;
        for (const auto& b : bs)
            total += sectionDb(b, f);
        return total;
    }

    juce::AudioProcessorValueTreeState& apvts;
    Mode mode;
    juce::String prefix;
    int bandCount = 0, selected = 0, hover = -1, dragBand = -1;
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqCurve)
};

} // namespace v4h
