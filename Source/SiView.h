#pragma once

// Stereo-imager field view: the classic three-band stereo display, doubling
// as the editor. Frequency runs up the vertical axis; the two horizontal
// lines are the low/mid and mid/high crossovers (drag vertically). Each
// band's horizontal spread is its width (drag horizontally): centre = 1.0
// untouched, left edge = mono (0), right edge = double (2).
//
// Replaces the group's five slider rows; the language for its zone labels is
// read from the APVTS state each paint, so relabelAll needs no special case.

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
    // Frequency runs bottom (20 Hz) to top (20 kHz), like a spectrum.
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
    // Which band does y fall in? 0 = low, 1 = mid, 2 = high.
    int zoneAt(float y) const
    {
        return y < yOf(highCross()) ? 2 : (y < yOf(lowCross()) ? 1 : 0);
    }

    // --- mouse -------------------------------------------------------------

    void mouseDown(const juce::MouseEvent& e) override
    {
        const auto r = fieldRect();
        const float dLow = std::abs(e.position.y - yOf(lowCross()));
        const float dHigh = std::abs(e.position.y - yOf(highCross()));
        if (dLow < 8.0f && dLow <= dHigh)
            dragCross = 1; // low/mid line
        else if (dHigh < 8.0f)
            dragCross = 2; // mid/high line
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
        // left edge -> 0 (mono), centre -> 1 (untouched), right edge -> 2
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
        // Only widths reset; crossover lines have no meaningful shared default.
        if (std::abs(e.position.y - yOf(lowCross())) < 8.0f
            || std::abs(e.position.y - yOf(highCross())) < 8.0f)
            return;
        const int zone = zoneAt(e.position.y);
        if (zone >= 0)
            if (auto* p = rp(widthIds[zone]))
                p->setValueNotifyingHost(p->getDefaultValue()); // back to 1.0
    }

    // --- drawing -----------------------------------------------------------

    void paint(juce::Graphics& g) override
    {
        using namespace theme;
        const auto lang = apvts.state.getProperty("uiLanguage", "en").toString();
        const auto r = fieldRect();

        g.setColour(c(field));
        g.fillRoundedRectangle(r, 5.0f);

        // centre line (mono reference)
        g.setColour(c(outline).brighter(0.2f));
        g.fillRect(r.getCentreX(), r.getY(), 1.0f, r.getHeight());

        // bands: zone label + width spread from the centre
        {
            juce::Graphics::ScopedSaveState saved(g);
            g.reduceClipRegion(r.toNearestInt()); // width > 1 overflows the field
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
                r.getX() + 6.0f, yTop, 60.0f, yBottom - yTop, juce::Justification::centredLeft);
        }

        // crossover lines (drag vertically)
        for (int line = 1; line <= 2; ++line)
        {
            const float y = yOf(line == 1 ? lowCross() : highCross());
            const bool active = line == dragCross || line == hoverCross;
            g.setColour(active ? c(accentBright) : c(textBright));
            g.fillRect(r.getX() + 3.0f, y - 1.0f, r.getWidth() - 6.0f, 2.0f);
            g.fillEllipse(r.getX() + 3.0f, y - 4.0f, 8.0f, 8.0f);
            g.fillEllipse(r.getRight() - 11.0f, y - 4.0f, 8.0f, 8.0f);
        }

        // readout: hovered band width or crossover frequency
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
            g.drawText(readout, r.getX(), r.getY() + 2.0f, r.getWidth() - 8.0f, 14.0f,
                juce::Justification::centredRight);
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

    juce::AudioProcessorValueTreeState& apvts;
    int dragCross = -1, dragZone = -1, hoverCross = -1, hoverZone = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SiView)
};

} // namespace v4h
