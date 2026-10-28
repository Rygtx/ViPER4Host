#pragma once

// 图形 EQ（固定 ISO 频点，拖动 = 增益）和动态 EQ（可拖动的频率+增益节点，
// 单击 = 选中频段，滚轮 = 微调增益，双击 = 重置增益）共用的频响曲线编辑器。
//
// 动态模式画出所有频段 RBJ 双二阶响应之和；图形模式复刻引擎自己的带通
// 分节算法（MinPhaseIIRCoeffs），并做了归一化，让平直设置真的画在 0 dB。
// 编辑器通过裸指针喂入实时数据：输出频谱（画在曲线下方）和各频段当前
// 生效的增益（动态模式的"幽灵点"，展示动态在起作用）。方向键微调选中
// 频段（Shift = 粗调）；动态模式下 Alt+拖动 / Ctrl+滚轮调 Q。

#include "Theme.h"

#include <JuceHeader.h>
#include <array>
#include <cmath>
#include <complex>
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
        setWantsKeyboardFocus(true);
    }

    // 来自编辑器的实时数据（定时器接管前可能为空）。
    static constexpr int kSpectrumPoints = 128;
    void setSpectrum(const float* db) { spectrumDb = db; }
    void setLiveGains(const float* db) { liveGainDb = db; }
    void setSampleRate(double fs)
    {
        if (fs > 0.0 && fs != fsDraw)
        {
            fsDraw = fs;
            repaint();
        }
    }

    // 与引擎 MinPhaseIIRCoeffs 一致的各段数频点表（编辑器标签也用它）。
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

    // 绑定前 `count` 个频段：前缀 "eqBand"（图形 EQ，eqBand1..N）或
    // "de"（动态 EQ，de1Freq/Q/Gain/Type ……）。频段数变化时会再次调用；
    // 重建参数附着。
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

    // 动态模式下用户选中节点时触发；编辑器把分频段详情行重新绑定到
    // 选中的频段。
    std::function<void(int)> onSelect;

    // 每次绘制 / 拖动命中测试时缓存的快照。公开是因为下面贴近引擎的
    // 频响计算要接收它的 vector。
    struct Band
    {
        double f = 1000.0, g = 0.0, q = 1.0;
        int type = 0;
    };

    // --- 贴近引擎的图形 EQ 频响 -------------------------------------------
    // 复刻 ViPERDSP：每个频段是一个固定带通双二阶（系数来自
    // MinPhaseIIRCoeffs::UpdateCoeffs），输出是 level_k * H_k 之和。
    // 显示对平直设置做归一化，让全 0 dB 真的画在 0 dB——展示的是用户
    // 的改动，而不是引擎里固化的 0.636 补偿常数。

    static double bandPoleRadius(double centerFreq, double octaves, double fs)
    {
        const double x = std::pow(2.0, octaves / 2.0);
        const double w0 = 2.0 * juce::MathConstants<double>::pi * centerFreq / fs;
        const double wy = 2.0 * juce::MathConstants<double>::pi * centerFreq * x / fs;
        const double cw = std::cos(w0), cy = std::cos(wy), sy = std::sin(wy);
        const double a = cw * cy, b = cw * cw / 2.0, c = sy * sy;
        const double d = b - a + 0.5 - c;
        const double e = c + (b + cy * cy - a - 0.5);
        const double f = cw * cw * 0.125 - cw * cy * 0.25 + 0.125 - c * 0.25;
        const double xr = (f - e * e / (d * 4.0)) / d;
        const double yr = e / (d + d);
        if (xr >= 0.0)
            return 0.0;
        const double z = std::sqrt(-xr);
        const double ra = -yr - z, rb = z - yr;
        return ra > rb ? ra : rb;
    }

    // 每频段常数：系数只依赖频段参数和采样率，与 x 无关，逐像素循环外
    // 算好（每帧一次，而不是每像素每频段一次 pow/三角函数）。
    struct GraphicCoeffs { double c1, c2, c3, gain; };
    struct SectionCoeffs { double b0, b1, b2, a0, a1, a2; };

    std::array<GraphicCoeffs, 31> graphicCoeffs(const std::vector<Band>& bs) const
    {
        const double octaves = bandCount == 10 ? 1.0
            : bandCount == 15                  ? 2.0 / 3.0
                                               : 1.0 / 3.0;
        std::array<GraphicCoeffs, 31> cs{};
        for (int b = 0; b < bandCount; ++b)
        {
            const double r = bandPoleRadius(bs[(size_t) b].f, octaves, fsDraw);
            const double w0 = 2.0 * juce::MathConstants<double>::pi * bs[(size_t) b].f / fsDraw;
            cs[(size_t) b] = { 2.0 * r, 0.5 - r, 2.0 * (r + 0.5) * std::cos(w0),
                std::pow(10.0, bs[(size_t) b].g / 20.0) };
        }
        return cs;
    }

    double graphicResponseDb(const GraphicCoeffs* cs, double f) const
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fsDraw;
        const std::complex<double> zm1(std::cos(w), -std::sin(w));
        const std::complex<double> zm2 = zm1 * zm1;
        std::complex<double> sum{}, flat{};
        for (int b = 0; b < bandCount; ++b)
        {
            const auto& c = cs[(size_t) b];
            const std::complex<double> h = c.c2 * (1.0 - zm2) / (1.0 - c.c3 * zm1 + c.c1 * zm2);
            sum += c.gain * h;
            flat += h;
        }
        // 原实现的两侧 0.636 补偿常数在比值里抵消，直接省去。
        return 20.0 * std::log10(std::abs(sum) / std::max(std::abs(flat), 1e-9));
    }

private:
    static constexpr float minFreq = 20.0f, maxFreq = 20000.0f, maxDb = 12.0f;
    static constexpr float padL = 34.0f, padR = 10.0f, padT = 8.0f, padB = 16.0f;
    // 已知时使用引擎的真实采样率（setSampleRate）；否则回退 48k。
    double fsDraw = 48000.0;

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

    // --- 几何 --------------------------------------------------------------

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

    // --- 鼠标 --------------------------------------------------------------

    int pick(juce::Point<float> pos) const
    {
        int best = -1;
        // 图形模式：抓取落在该频段 x 列上的任意点；动态模式：最近的节点。
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
        // 两种模式都保留选中态：键盘微调也依赖它。
        if (selected != dragBand)
        {
            selected = dragBand;
            if (mode == Mode::Dynamic && onSelect)
                onSelect(selected);
        }
        hover = dragBand;
        qAtDragStart = paramValue(bandId(dragBand, "Q"));
        yAtDragStart = e.position.y;
        gesture(gainId(dragBand), true);
        if (mode == Mode::Dynamic)
            gesture(bandId(dragBand, "Freq"), true);
        grabKeyboardFocus();
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (dragBand < 0)
            return;
        // Alt + 纵向拖动改的是 Q 值而不是增益。
        if (mode == Mode::Dynamic && e.mods.isAltDown())
        {
            setParam(bandId(dragBand, "Q"),
                (float) juce::jlimit(0.3, 16.0,
                    qAtDragStart * std::exp(-(e.position.y - yAtDragStart) / 150.0)));
            return;
        }
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
            p->setValueNotifyingHost(p->getDefaultValue()); // 回到 0 dB
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const int b = dragBand >= 0 ? dragBand : pick(e.position);
        if (b < 0 || wheel.deltaY == 0)
            return;
        // Ctrl/Alt + 滚轮调 Q；普通滚轮调增益。
        if (mode == Mode::Dynamic && (e.mods.isCtrlDown() || e.mods.isAltDown()))
        {
            const auto qid = bandId(b, "Q");
            const float q = (float) juce::jlimit(0.3, 16.0,
                paramValue(qid) * (wheel.deltaY > 0 ? 1.122 : 1.0 / 1.122));
            gesture(qid, true);
            setParam(qid, q);
            gesture(qid, false);
            return;
        }
        const auto id = gainId(b);
        const float g = juce::jlimit(-maxDb, maxDb,
            paramValue(id) + (wheel.deltaY > 0 ? 0.5f : -0.5f));
        gesture(id, true);
        setParam(id, g);
        gesture(id, false);
    }

    // 方向键微调选中频段。图形模式：左/右在频段间移动，上/下调增益。
    // 动态模式：左/右改频率，alt+左/右调 Q。Shift 把步长乘 4。
    bool keyPressed(const juce::KeyPress& k) override
    {
        if (bandCount == 0)
            return false;
        const int code = k.getKeyCode();
        const auto mods = k.getModifiers();
        const bool left = code == juce::KeyPress::leftKey;
        const bool right = code == juce::KeyPress::rightKey;
        const bool up = code == juce::KeyPress::upKey;
        const bool down = code == juce::KeyPress::downKey;
        if (!left && !right && !up && !down)
            return false;

        const float coarse = mods.isShiftDown() ? 4.0f : 1.0f;
        const auto nudge = [&](const juce::String& id, float v) {
            gesture(id, true);
            setParam(id, v);
            gesture(id, false);
        };

        if (mode == Mode::Graphic)
        {
            if (left || right)
            {
                selected = juce::jlimit(0, bandCount - 1, selected + (right ? 1 : -1));
                repaint();
                return true;
            }
            nudge(gainId(selected), juce::jlimit(-maxDb, maxDb,
                paramValue(gainId(selected)) + (up ? 0.25f : -0.25f) * coarse));
            repaint();
            return true;
        }

        if (up || down)
        {
            nudge(gainId(selected), juce::jlimit(-maxDb, maxDb,
                paramValue(gainId(selected)) + (up ? 0.25f : -0.25f) * coarse));
        }
        else if (mods.isAltDown())
        {
            nudge(bandId(selected, "Q"), (float) juce::jlimit(0.3, 16.0,
                paramValue(bandId(selected, "Q"))
                    * (right ? 1.122 * coarse : 1.0 / (1.122 * coarse))));
        }
        else
        {
            const double ratio = std::pow(2.0, (right ? 1.0 : -1.0)
                * (mods.isShiftDown() ? 1.0 : 1.0 / 12.0));
            nudge(bandId(selected, "Freq"), juce::jlimit(minFreq, maxFreq,
                (float) (paramValue(bandId(selected, "Freq")) * ratio)));
        }
        repaint();
        return true;
    }

    // --- 绘制 --------------------------------------------------------------

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

        // 网格：对数频率刻度 + dB 横线
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        const float fTicks[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        const char* fLabels[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
        for (int i = 0; i < 8; ++i)
        {
            const float x = xOf(fTicks[i]);
            g.setColour(c(outline));
            g.fillRect(juce::Rectangle<float>(x, p.getY(), 1.0f, p.getHeight()));
            g.setColour(c(textDim));
            g.drawText(fLabels[i], juce::Rectangle<float>(x - 20.0f, p.getBottom() + 1.0f,
                40.0f, 12.0f), juce::Justification::centred);
        }
        for (int db = -12; db <= 12; db += 6)
        {
            const float y = yOf(db);
            g.setColour(db == 0 ? c(outline).brighter(0.25f) : c(outline));
            g.fillRect(juce::Rectangle<float>(p.getX(), y, p.getWidth(), 1.0f));
            if (db != 0)
            {
                g.setColour(c(textDim));
                g.drawText(juce::String(db > 0 ? "+" : "") + juce::String(db),
                    juce::Rectangle<float>(0.0f, y - 6.0f, padL - 4.0f, 12.0f),
                    juce::Justification::centredRight);
            }
        }

        // 输出频谱垫底（编辑器的定时器喂入）
        if (spectrumDb != nullptr)
        {
            juce::Path spec;
            const auto ySpec = [&](float db) {
                return p.getBottom() - juce::jlimit(0.0f, 96.0f, db + 96.0f) / 96.0f * p.getHeight();
            };
            spec.startNewSubPath(p.getX(), ySpec(-96.0f));
            for (int i = 0; i < kSpectrumPoints; ++i)
            {
                const float f = minFreq * std::pow(1000.0f, i / (float) (kSpectrumPoints - 1));
                spec.lineTo(xOf(f), ySpec(spectrumDb[i]));
            }
            spec.lineTo(p.getRight(), ySpec(-96.0f));
            g.setColour(c(textBright).withAlpha(0.07f));
            g.fillPath(spec);
            g.setColour(c(textBright).withAlpha(0.25f));
            g.strokePath(spec, juce::PathStrokeType(1.0f));
        }

        // 频响曲线 + 曲线下方的柔和填充
        auto path = curvePath(bs);
        juce::Path fill = path;
        fill.lineTo(p.getRight(), p.getCentreY());
        fill.lineTo(p.getX(), p.getCentreY());
        fill.closeSubPath();
        g.setColour(c(accent).withAlpha(0.10f));
        g.fillPath(fill);
        g.setColour(c(accent));
        g.strokePath(path, juce::PathStrokeType(2.0f));

        // 频段手柄
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

        // 动态模式的幽灵点：各频段此刻被动态推到的位置
        if (mode == Mode::Dynamic && liveGainDb != nullptr)
        {
            for (int b = 0; b < bandCount; ++b)
            {
                const float live = liveGainDb[b];
                if (std::abs(live - bs[(size_t) b].g) < 0.3f)
                    continue;
                const float x = xOf(bs[(size_t) b].f), y = yOf(live);
                g.setColour(c(accent).withAlpha(0.7f));
                g.drawLine(x, yOf(bs[(size_t) b].g), x, y, 1.5f);
                g.fillEllipse(x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            }
        }

        // 活动频段的读数
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

    // 单个 RBJ 双二阶分节的系数（只依赖频段参数和采样率）。
    SectionCoeffs sectionCoeffs(const Band& band) const
    {
        const double A = std::pow(10.0, band.g / 40.0);
        const double q = band.q;
        const double w0 = 2.0 * juce::MathConstants<double>::pi
            * juce::jlimit(minFreq, maxFreq, (float) band.f) / fsDraw;
        const double cw = std::cos(w0), sw = std::sin(w0);
        double b0, b1, b2, a0, a1, a2;
        if (band.type == 0) // 峰值
        {
            const double al = sw / (2.0 * q);
            b0 = 1 + al * A; b1 = -2 * cw; b2 = 1 - al * A;
            a0 = 1 + al / A; a1 = -2 * cw; a2 = 1 - al / A;
        }
        else
        {
            const double sa = std::sqrt(A);
            const double al = sw / 2.0 * std::sqrt(2.0); // 架构斜率 S = 1
            if (band.type == 1) // 低架
            {
                b0 = A * ((A + 1) - (A - 1) * cw + 2 * sa * al);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - 2 * sa * al);
                a0 = (A + 1) + (A - 1) * cw + sa * al;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - sa * al;
            }
            else // 高架
            {
                b0 = A * ((A + 1) + (A - 1) * cw + 2 * sa * al);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - 2 * sa * al);
                a0 = (A + 1) - (A - 1) * cw + sa * al;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - sa * al;
            }
        }
        return { b0, b1, b2, a0, a1, a2 };
    }

    // 单个分节在频率 f 处的幅度（dB）。
    static double sectionDb(const SectionCoeffs& c, double f, double fs)
    {
        const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
        const double c1 = std::cos(w), c2 = std::cos(2.0 * w);
        const double num = c.b0 * c.b0 + c.b1 * c.b1 + c.b2 * c.b2
            + 2.0 * (c.b0 * c.b1 + c.b1 * c.b2) * c1 + 2.0 * c.b0 * c.b2 * c2;
        const double den = c.a0 * c.a0 + c.a1 * c.a1 + c.a2 * c.a2
            + 2.0 * (c.a0 * c.a1 + c.a1 * c.a2) * c1 + 2.0 * c.a0 * c.a2 * c2;
        return 10.0 * std::log10(std::max(num, 1e-12) / std::max(den, 1e-12));
    }

    juce::Path curvePath(const std::vector<Band>& bs) const
    {
        juce::Path path;
        const auto p = plot();
        const int steps = juce::jmax(2, juce::roundToInt(p.getWidth()));
        if (mode == Mode::Dynamic)
        {
            // 容量取类不变量上限 31（bandCount 由 setBands 钳到 1..31），
            // 动态模式实际只用 10，多出的空位不参与循环。
            std::array<SectionCoeffs, 31> cs{};
            for (int b = 0; b < bandCount; ++b)
                cs[(size_t) b] = sectionCoeffs(bs[(size_t) b]);
            path.startNewSubPath(p.getX(), yOf(sumDb(cs.data(), freqOf(p.getX()))));
            for (int s = 1; s <= steps; ++s)
            {
                const float x = p.getX() + p.getWidth() * s / steps;
                path.lineTo(x, yOf(sumDb(cs.data(), freqOf(x))));
            }
            return path;
        }

        // 图形模式：贴近引擎的带通响应求和。
        const auto coeffs = graphicCoeffs(bs);
        path.startNewSubPath(p.getX(), yOf(graphicResponseDb(coeffs.data(), freqOf(p.getX()))));
        for (int s = 1; s <= steps; ++s)
        {
            const float x = p.getX() + p.getWidth() * s / steps;
            path.lineTo(x, yOf(graphicResponseDb(coeffs.data(), freqOf(x))));
        }
        return path;
    }

    double sumDb(const SectionCoeffs* cs, double f) const
    {
        double total = 0.0;
        for (int b = 0; b < bandCount; ++b)
            total += sectionDb(cs[(size_t) b], f, fsDraw);
        return total;
    }

    juce::AudioProcessorValueTreeState& apvts;
    Mode mode;
    juce::String prefix;
    int bandCount = 0, selected = 0, hover = -1, dragBand = -1;
    float qAtDragStart = 1.0f, yAtDragStart = 0.0f;
    const float* spectrumDb = nullptr; // 编辑器持有，kSpectrumPoints 个元素
    const float* liveGainDb = nullptr; // 编辑器持有，每个动态 EQ 频段一个
    std::vector<std::unique_ptr<juce::ParameterAttachment>> attachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EqCurve)
};

} // namespace v4h
