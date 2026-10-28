#include "PluginEditor.h"
#include "Localisation.h"

namespace
{
constexpr int defaultWidth = 700;
constexpr int defaultHeight = 640;
constexpr int headerHeight = 50;
constexpr int tabBarHeight = 36;
constexpr int gap = 10;
constexpr int rowHeight = 30;
constexpr int titleHeight = 30; // 与 Theme.h 卡片标题栏高度一致
constexpr int labelWidth = 140;
constexpr int curveHeight = 230; // EQ / DynEQ 频响图
constexpr int stripHeight = 60;  // MBC 分频条
constexpr int siViewHeight = 200; // 立体声声像器场视图
constexpr int grMeterHeight = 30; // FET / 限幅器增益衰减量表

// 分类页签，按顺序。数量必须与头文件里的 numPages 一致。
const char* pageKeys[] = { "tabIn", "tabEq", "tabStage", "tabDynamics",
    "tabTone", "tabOutput" };
static_assert(sizeof(pageKeys) / sizeof(pageKeys[0]) == ViPER4HostAudioProcessorEditor::numPages);

// 每个分组属于哪个分类页签。页签大体按引擎信号链排列（ViPER::Process 的处理
// 顺序）：卷积器 -> VHE -> DDC -> 频谱延展 -> EQ -> 动态 EQ -> 声场环绕 ->
// 立体声声像器 -> 差分环绕 -> 播放增益 -> MBC -> FET -> 动态系统 -> 电子管
// -> 低音们 -> clarity -> cure -> analogX -> 混响 -> 扬声器修正 -> LUFS ->
// 限幅器。例外：VHE 只有两个控件，按功能归入"声场/环绕"页而非信号链位置。
// 页签内的分组也按这条链的顺序排列（下面的分组创建必须与之对应）。
int pageOf(const juce::String& gid)
{
    if (gid == "groupConv" || gid == "groupDdc")
        return 0; // 输入侧修正：卷积核、DDC 曲线
    if (gid == "groupSpecExt" || gid == "groupEq" || gid == "groupDe")
        return 1; // 频率响应：激励器、EQ、动态 EQ
    if (gid == "groupVHE" || gid == "groupField" || gid == "groupSi" || gid == "groupDiff")
        return 2; // 声场 / 环绕
    if (gid == "groupAGC" || gid.startsWith("groupMbc") || gid == "groupFet" || gid == "groupDs")
        return 3; // 动态类，含动态系统
    if (gid == "groupTube" || gid == "groupPsyBass" || gid == "groupBass"
        || gid == "groupBassMono" || gid == "groupClarity" || gid == "groupCure"
        || gid == "groupAx" || gid == "groupReverb")
        return 4; // 音染色：电子管、低音谐波、clarity、cure、analogX、混响
    return 5; // 输出：扬声器修正、LUFS、限幅器/主控
}

// 分频段参数（mbc3Threshold、de7Freq）按角色在所有频段间共用一个标签键：
// mbcN* -> mbcBand*，deN* -> deBand*。
juce::String labelKey(const juce::String& paramId)
{
    const bool isMbc = paramId.startsWith("mbc");
    const bool isDe = paramId.startsWith("de");
    const int prefixLen = isMbc ? 3 : 2;
    if ((isMbc || isDe) && paramId.length() > prefixLen
        && juce::CharacterFunctions::isDigit(paramId[prefixLen]))
    {
        int i = prefixLen;
        while (i < paramId.length() && juce::CharacterFunctions::isDigit(paramId[i]))
            ++i;
        return (isMbc ? "mbcBand" : "deBand") + paramId.substring(i);
    }
    return paramId;
}

// "eqBandN"（图形 EQ 频段滑杆）-> N；其他参数（eqEnable、eqBandCount）返回 0。
int eqBandNumberOf(const juce::String& id)
{
    if (!id.startsWith("eqBand"))
        return 0;
    const auto suffix = id.substring(6);
    return suffix.isNotEmpty() && suffix.containsOnly("0123456789") ? suffix.getIntValue() : 0;
}
} // namespace

ViPER4HostAudioProcessorEditor::ViPER4HostAudioProcessorEditor(ViPER4HostAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor)
{
    setLookAndFeel(&lookAndFeel_);

    // 频谱分析器初始化：长短两个 Hann 窗 + 静音初始频谱。
    const auto initHann = [](auto& h) {
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
                * (float) i / (float) (h.size() - 1));
    };
    initHann(hann_);
    initHann(hannLong_);
    spectrumDb_.fill(-96.0f);

    // 分区顺序跟随引擎信号链（见上面的 pageOf），所以每个页签里的
    // 分组都按处理顺序排列。
    addGroup("groupConv", { "convEnable", "convCrossChannel" });
    addGroup("groupDdc", { "ddcEnable" });
    addGroup("groupSpecExt", { "specExtEnable", "specExtStrength", "specExtExciter" });

    {
        auto& pg = *addGroup("groupEq", { "eqEnable", "eqBandCount" });
        pg.curve = std::make_unique<v4h::EqCurve>(
            processor_.getParametersState(), v4h::EqCurve::Mode::Graphic);
        pg.curve->setSpectrum(spectrumDb_.data());
        pg.group.addAndMakeVisible(*pg.curve);
    }

    {
        auto& pg = *addGroup("groupDe", { "deEnable", "deBandCount",
            "de1Freq", "de1Q", "de1Gain", "de1Threshold", "de1Attack", "de1Release", "de1Type" });
        pg.curve = std::make_unique<v4h::EqCurve>(
            processor_.getParametersState(), v4h::EqCurve::Mode::Dynamic);
        pg.curve->setSpectrum(spectrumDb_.data());
        pg.curve->setLiveGains(deGainDb_);
        // 点击图上的节点会把详情行切到那个频段。
        pg.curve->onSelect = [this](int band) { selectDeBand(band); };
        pg.group.addAndMakeVisible(*pg.curve);
        for (auto& row : pg.rows)
            if (!row->isEnable && row->paramId != "deBandCount")
                deBandRows_.push_back(row.get()); // 创建顺序 == 绑定顺序
    }

    addGroup("groupVHE", { "vheEnable", "vheQuality" });
    addGroup("groupField", { "fieldEnable", "fieldWidening", "fieldMidImage", "fieldDepth" });
    addGroup("groupSi", { "siEnable" });
    groups_.back()->siView = std::make_unique<v4h::SiView>(processor_.getParametersState());
    groups_.back()->group.addAndMakeVisible(*groups_.back()->siView);
    addGroup("groupDiff", { "diffEnable", "diffDelay", "diffReverse", "diffMix", "diffLpCutoff" });

    addGroup("groupAGC", { "pgcEnable", "pgcStrength", "pgcMaxGain", "pgcOutputThreshold" });
    {
        auto& pg = *addGroup("groupMbc", { "mbcEnable", "mbcBandCount" });
        pg.strip = std::make_unique<v4h::XoverStrip>(processor_.getParametersState());
        pg.strip->setBandGrDb(mbcGrDb_);
        // 点击某个区间选中该频段；详情行跟着切换。
        pg.strip->onSelect = [this](int band) { selectMbcBand(band); };
        pg.group.addAndMakeVisible(*pg.strip);
    }

    {
        // 选中 MBC 频段的详情面板（titleEnable=false，让 mbcNEnable 保持
        // 普通开关行，而不是充当本分组的折叠开关）。
        static const char* suffixes[] = { "Enable", "Threshold", "Ratio", "Knee", "KneeAuto",
            "Gain", "GainAuto", "Attack", "AttackAuto", "Release", "ReleaseAuto", "KneeMulti",
            "MaxAttack", "MaxRelease", "Crest", "Adapt", "NoClip" };
        std::vector<juce::String> ids;
        for (const char* s : suffixes)
            ids.push_back(juce::String("mbc1") + s);
        auto& pg = *addGroup("groupMbcSel", ids, false);
        for (auto& row : pg.rows)
            mbcBandRows_.push_back(row.get()); // 创建顺序 == 绑定顺序
    }

    addGroup("groupFet", { "fetEnable", "fetThreshold", "fetRatio", "fetKnee", "fetKneeAuto",
        "fetGain", "fetGainAuto", "fetAttack", "fetAttackAuto", "fetRelease", "fetReleaseAuto",
        "fetKneeMulti", "fetMaxAttack", "fetMaxRelease", "fetCrest", "fetAdapt", "fetNoClip" });
    groups_.back()->grMeter = std::make_unique<v4h::GrMeter>(); // FET 压缩器 GR
    groups_.back()->group.addAndMakeVisible(*groups_.back()->grMeter);
    addGroup("groupDs", { "dsEnable", "dsXLow", "dsXHigh", "dsYLow", "dsYHigh",
        "dsSideGainLow", "dsSideGainHigh", "dsStrength" });

    addGroup("groupTube", { "tubeEnable", "tubeModel", "tubeDrive", "tubeMix" });
    addGroup("groupPsyBass", { "pBassEnable", "pBassCutoff", "pBassIntensity",
        "pBassHarmonicOrder", "pBassOriginalLevel" });
    addGroup("groupBass", { "bassEnable", "bassMode", "bassFreq", "bassGain", "bassAntiPop" });
    addGroup("groupBassMono", { "bassMonoEnable", "bassMonoMode", "bassMonoFreq",
        "bassMonoGain", "bassMonoAntiPop" });
    addGroup("groupClarity", { "clarityEnable", "clarityMode", "clarityGain" });
    addGroup("groupCure", { "cureEnable", "curePreset" });
    addGroup("groupAx", { "axEnable", "axMode" });
    addGroup("groupReverb", { "reverbEnable", "reverbRoomSize", "reverbWidth", "reverbDamp",
        "reverbWet", "reverbDry" });

    addGroup("groupSpk", { "spkEnable" });
    addGroup("groupLufs", { "lufsEnable", "lufsTarget", "lufsMaxGain", "lufsSpeed" });
    addGroup("groupMaster", { "limiterThreshold", "limiterVolume", "limiterPan" });
    groups_.back()->grMeter = std::make_unique<v4h::GrMeter>(); // 输出限幅器 GR
    groups_.back()->group.addAndMakeVisible(*groups_.back()->grMeter);

    // 图形对象（curve/strip 等）是各 addGroup 返回后才挂上的，期间各自
    // 跑过的 refreshCollapse 可能误判"无正文"；这里全部齐备后再统一推导。
    refreshCollapse();

    // 频段数参数决定显示哪些频段行/分组。
    for (const char* id : { "eqBandCount", "deBandCount", "mbcBandCount" })
    {
        bandCountAttachments_.push_back(std::make_unique<juce::ParameterAttachment>(
            *processor_.getParametersState().getParameter(id),
            [this](float) { updateBandVisibility(); }, nullptr));
        bandCountAttachments_.back()->sendInitialUpdate();
    }

    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    languageLabel_.setJustificationType(juce::Justification::centredRight);
    for (const auto& option : v4h::localisation::availableLanguages())
        languageBox_.addItem(option.displayName, static_cast<int>(languageBox_.getNumItems()) + 1);
    languageBox_.onChange = [this] { selectLanguage(); };

    // 预设与 A/B 对比：槽位状态放在处理器里（随 DAW 工程一起保存），
    // 活动槽位取恢复出来的状态。
    for (int s = 0; s < 2; ++s)
    {
        abButtons_[s].setButtonText(s == 0 ? "A" : "B");
        abButtons_[s].setClickingTogglesState(true);
        abButtons_[s].setRadioGroupId(2);
        abButtons_[s].onClick = [this, s] {
            if (abButtons_[s].getToggleState())
                processor_.activateAbSlot(s);
        };
        addAndMakeVisible(abButtons_[s]);
    }
    abButtons_[processor_.activeAbSlot].setToggleState(true, juce::dontSendNotification);
    abCopy_.onClick = [this] {
        processor_.abSlots[processor_.activeAbSlot] = processor_.getParametersState().copyState();
    };
    addAndMakeVisible(abCopy_);
    presetBox_.onChange = [this] {
        const int id = presetBox_.getSelectedId();
        if (id == 9999)
            savePresetDialog();
        else if (id >= 100 && id - 100 < userPresetFiles_.size())
        {
            if (auto xml = juce::parseXML(userPresetFiles_[id - 100]))
                processor_.applyStateTree(juce::ValueTree::fromXml(*xml));
        }
    };
    addAndMakeVisible(presetBox_);
    // 右键预设栏弹出预设管理菜单（左键仍由 ComboBox 自己处理弹窗）。
    presetBox_.addMouseListener(this, true);
    refreshPresetList();

    for (int i = 0; i < numPages; ++i)
    {
        pageButtons_[i].setClickingTogglesState(true);
        pageButtons_[i].setRadioGroupId(1);
        pageButtons_[i].onClick = [this, i] {
            currentPage_ = i;
            resized();
        };
        addAndMakeVisible(pageButtons_[i]);
    }
    pageButtons_[0].setToggleState(true, juce::dontSendNotification);
    refreshTabDots(); // 页签先于各开关初值出现，这里补一次点亮

    content_.setSize(defaultWidth, 0);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, true);
    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(languageLabel_);
    addAndMakeVisible(languageBox_);
    addAndMakeVisible(viewport_);
    setResizable(true, false);
    setResizeLimits(320, 240, 1920, 2160);
    // 恢复记住的窗口尺寸（由定时器在用户停止拖拽边框约 1 秒后保存）。
    auto& uiState = processor_.getParametersState().state;
    setSize(
        juce::jlimit(320, 1920, (int) uiState.getProperty("uiWidth", defaultWidth)),
        juce::jlimit(240, 2160, (int) uiState.getProperty("uiHeight", defaultHeight)));
    savedSize_ = { getWidth(), getHeight() };

    const auto language = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    juce::StringArray codes;
    for (const auto& option : v4h::localisation::availableLanguages())
        codes.add(option.code);
    languageBox_.setSelectedId(codes.indexOf(language) + 1, juce::dontSendNotification);
    relabelAll(language);
    startTimerHz(60);
}

void ViPER4HostAudioProcessorEditor::selectLanguage()
{
    const auto& languages = v4h::localisation::availableLanguages();
    const auto& code = languages[static_cast<size_t>(languageBox_.getSelectedId() - 1)].code;
    processor_.setUiLanguage(code);
    relabelAll(code);
}

ViPER4HostAudioProcessorEditor::~ViPER4HostAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

// --- 电平 / 频谱 -----------------------------------------------------------

void ViPER4HostAudioProcessorEditor::timerCallback()
{
    // 窗口没上屏（编辑器被宿主隐藏等）时不跑 FFT 和重绘，省后台 CPU。
    if (!isShowing())
        return;

    pullMetersAndSpectrum();

    // 窗口尺寸稳定约 1 秒后持久化。
    const std::array<int, 2> size = { getWidth(), getHeight() };
    if (size != savedSize_)
    {
        savedSize_ = size;
        resizeIdleTicks_ = 0;
    }
    else if (resizeIdleTicks_ >= 0 && ++resizeIdleTicks_ >= 60)
    {
        auto& uiState = processor_.getParametersState().state;
        uiState.setProperty("uiWidth", size[0], nullptr);
        uiState.setProperty("uiHeight", size[1], nullptr);
        resizeIdleTicks_ = -1;
    }
}

void ViPER4HostAudioProcessorEditor::pullMetersAndSpectrum()
{
    auto& m = processor_.meters_;
    const auto isOn = [this](const char* id) {
        auto* p = processor_.getParametersState().getParameter(id);
        return p != nullptr && p->getValue() > 0.5f;
    };
    const float fetGr = isOn("fetEnable") ? m.fetGrDb.load() : 0.0f;
    const float limGr = m.limiterGrDb.load();
    const bool mbcOn = isOn("mbcEnable");
    for (int b = 0; b < 5; ++b)
        mbcGrDb_[b] = mbcOn ? m.mbcGrDb[(size_t) b].load() : 0.0f;
    for (int b = 0; b < 10; ++b)
        deGainDb_[b] = isOn("deEnable") ? m.deGainDb[(size_t) b].load() : 0.0f;

    // 频谱：多分辨率 FFT。低频带用 8192 点长窗（5.9 Hz 分辨率，20 Hz 附近
    // 才能对齐调节点），高频带用 2048 点短窗（瞬态不糊）。带内取 bin 最大
    // 值；没有 bin 覆盖的窄带按 log 频率在相邻 bin 间插值；最后跨带 1-2-1
    // 平滑，去掉低频阶梯和高频毛刺。
    auto& fifo = processor_.spectrumFifo_;
    constexpr int fftLong = 8192, fftShort = 2048;
    constexpr double crossHz = 400.0; // 低于此频率用长窗
    // 滑动窗：每帧把 FIFO 里新到的样本全部取走追加到窗尾（攒够 8192 才
    // 跑一次的话，48 kHz 下频谱只有约 5 fps，看着卡）。
    if (processor_.getSampleRate() > 0.0)
    {
        const auto append = [&](const float* src, int size) {
            size = juce::jmin(size, fftLong);
            if (size <= 0)
                return;
            const int overflow = juce::jmax(0, windowFill_ + size - fftLong);
            if (overflow > 0)
            {
                std::copy(window_.begin() + overflow, window_.begin() + windowFill_,
                    window_.begin());
                windowFill_ -= overflow;
            }
            std::copy_n(src, size, window_.begin() + windowFill_);
            windowFill_ += size;
        };
        while (fifo.getNumReady() > 0)
        {
            int start1, size1, start2, size2;
            fifo.prepareToRead(fifo.getNumReady(), start1, size1, start2, size2);
            if (size1 + size2 <= 0)
                break;
            if (size1 > 0)
                append(processor_.spectrumBuffer_ + start1, size1);
            if (size2 > 0)
                append(processor_.spectrumBuffer_ + start2, size2);
            fifo.finishedRead(size1 + size2);
        }
    }
    if (windowFill_ >= fftLong && processor_.getSampleRate() > 0.0)
    {
        const double fs = processor_.getSampleRate();

        // 把两个窗各自 FFT 出的可用 bin 收成 (log2 f, dB) 点列。
        struct SpecPt { double logf; float db; };
        constexpr int kMaxPts = 1280; // 短窗最多 1023 个 bin + 长窗约 70 个
        std::array<SpecPt, kMaxPts> pts{};
        int nPts = 0;
        const auto collect = [&](const float* spec, int fftN, double fFrom, double fTo) {
            const int nBins = fftN / 2;
            const double binHz = fs / fftN;
            const int b0 = juce::jlimit(1, nBins - 1, (int) std::ceil(14.0 / binHz));
            const int b1 = juce::jlimit(b0, nBins - 1, (int) (fTo / binHz));
            for (int b = b0; b <= b1; ++b)
            {
                if (nPts >= kMaxPts || b * binHz < fFrom)
                    continue;
                const float mag = std::hypot(spec[(size_t) (2 * b)], spec[(size_t) (2 * b + 1)]);
                pts[(size_t) nPts++] = { std::log2((double) b * binHz),
                    juce::jlimit(-120.0f, 12.0f,
                        20.0f * std::log10(mag / (float) (fftN / 4) + 1e-6f)) };
            }
        };
        // 长窗在 fftSamples_ 上原地变换（JUCE 实数变换要 2N 个浮点）；
        // 短窗要的时域样本先拷走。
        std::copy(window_.begin(), window_.end(), fftSamples_.begin());
        for (int i = 0; i < fftShort; ++i)
            fftShortBuf_[(size_t) i] = window_[(size_t) (fftLong - fftShort + i)];
        for (int i = 0; i < fftLong; ++i)
            fftSamples_[(size_t) i] *= hannLong_[(size_t) i];
        fftLong_.performRealOnlyForwardTransform(fftSamples_.data());
        collect(fftSamples_.data(), fftLong, 0.0, crossHz);
        for (int i = 0; i < fftShort; ++i)
            fftShortBuf_[(size_t) i] *= hann_[(size_t) i];
        fft_.performRealOnlyForwardTransform(fftShortBuf_.data());
        collect(fftShortBuf_.data(), fftShort, crossHz, fs * 0.5);

        // 128 个对数频带：带内有 bin 取最大值；没有则按 log 频率在相邻 bin
        // 间线性插值（低频 bin 稀疏，画成 0 会在左侧留一堵 -96 的悬崖）。
        std::array<float, v4h::EqCurve::kSpectrumPoints> fresh{};
        int p = 0;
        for (int i = 0; i < v4h::EqCurve::kSpectrumPoints; ++i)
        {
            const double lLo = std::log2(20.0 * std::pow(1000.0, i / 128.0));
            const double lHi = std::log2(20.0 * std::pow(1000.0, (i + 1.0) / 128.0));
            while (p < nPts && pts[(size_t) p].logf < lLo)
                ++p;
            int q = p;
            float mag = -120.0f;
            bool any = false;
            while (q < nPts && pts[(size_t) q].logf <= lHi)
            {
                mag = juce::jmax(mag, pts[(size_t) q].db);
                ++q;
                any = true;
            }
            if (any)
            {
                fresh[(size_t) i] = mag;
            }
            else if (p > 0 && q < nPts) // 带内无 bin：相邻 bin 的 dB 插值
            {
                const double mid = 0.5 * (lLo + lHi);
                const double t = (mid - pts[(size_t) (p - 1)].logf)
                    / (pts[(size_t) q].logf - pts[(size_t) (p - 1)].logf);
                fresh[(size_t) i] = (float) (juce::jlimit(0.0, 1.0, t)
                    * (pts[(size_t) q].db - pts[(size_t) (p - 1)].db) + pts[(size_t) (p - 1)].db);
            }
            else // 边缘带没有 bin：平铺最近的值，别掉到 -96
                fresh[(size_t) i] = p > 0 ? pts[(size_t) (p - 1)].db : pts[(size_t) q].db;
            if (!any)
                p = q;
        }
        // 跨带 1-2-1 平滑两遍（合成为 [1,4,6,4,1]/16 二项式核，接近
        // FabFilter 的顺滑度）+ 快起慢落的时域包络（60 fps 下每帧
        // 1.25 dB = 每秒 75 dB，与 30 fps 时代的观感一致）。
        std::array<float, v4h::EqCurve::kSpectrumPoints> smooth{};
        for (int pass = 0; pass < 2; ++pass)
        {
            const auto& src = pass == 0 ? fresh : smooth;
            std::array<float, v4h::EqCurve::kSpectrumPoints> dst{};
            for (int i = 0; i < v4h::EqCurve::kSpectrumPoints; ++i)
            {
                const float lo = src[(size_t) juce::jmax(0, i - 1)];
                const float hi = src[(size_t) juce::jmin(v4h::EqCurve::kSpectrumPoints - 1, i + 1)];
                dst[(size_t) i] = 0.25f * lo + 0.5f * src[(size_t) i] + 0.25f * hi;
            }
            smooth = dst;
        }
        for (int i = 0; i < v4h::EqCurve::kSpectrumPoints; ++i)
            spectrumDb_[(size_t) i] = juce::jmax(smooth[(size_t) i], spectrumDb_[(size_t) i] - 1.25f);
    }

    // 把实时数据推给可见的曲线图，并按当前帧率刷新它们。
    const double fs = processor_.getSampleRate();
    for (auto& pg : groups_)
    {
        if (pg->page != currentPage_ || pg->collapsed)
            continue;
        if (pg->grMeter != nullptr)
            pg->grMeter->setLevel(pg->groupId == "groupFet" ? fetGr : limGr);
        if (pg->curve != nullptr)
        {
            pg->curve->setSampleRate(fs);
            pg->curve->repaint();
        }
        if (pg->strip != nullptr)
            pg->strip->repaint();
    }
}

// --- 预设与 A/B ------------------------------------------------------------

juce::File ViPER4HostAudioProcessorEditor::presetsDir() const
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("ViPER4Host")
        .getChildFile("Presets");
    dir.createDirectory();
    return dir;
}

void ViPER4HostAudioProcessorEditor::refreshPresetList()
{
    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    presetBox_.clear(juce::dontSendNotification);
    userPresetFiles_ = presetsDir().findChildFiles(juce::File::findFiles, false, "*.xml");
    std::sort(userPresetFiles_.begin(), userPresetFiles_.end(),
        [](const juce::File& a, const juce::File& b) {
            return a.getFileName().compareNatural(b.getFileName()) < 0;
        });
    for (int i = 0; i < userPresetFiles_.size(); ++i)
        presetBox_.addItem(userPresetFiles_[i].getFileNameWithoutExtension(), 100 + i);
    if (userPresetFiles_.size() > 0)
        presetBox_.addSeparator();
    presetBox_.addItem(v4h::localisation::tr(lang, "presetSave"), 9999);
    presetBox_.setTextWhenNothingSelected(v4h::localisation::tr(lang, "presetLabel"));
}

void ViPER4HostAudioProcessorEditor::savePresetDialog()
{
    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    auto* window = new juce::AlertWindow(v4h::localisation::tr(lang, "presetSave"), {},
        juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", "preset");
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, window](int result) {
            if (result == 1)
            {
                // Windows 文件名的非法字符：替换，不拒绝。
                auto name = window->getTextEditorContents("name").trim();
                for (const char* p = "/\\:*?\"<>|"; *p != '\0'; ++p)
                    name = name.replaceCharacter(static_cast<juce::juce_wchar>(*p), '_');
                if (name.isNotEmpty())
                {
                    auto xml = processor_.getParametersState().copyState().createXml();
                    xml->setAttribute("presetName", name);
                    if (xml->writeTo(presetsDir().getChildFile(name + ".xml")))
                        refreshPresetList();
                    else
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            v4h::localisation::tr(processor_.getUiLanguage(), "presetSave"),
                            v4h::localisation::tr(processor_.getUiLanguage(), "presetSaveFailed"));
                }
            }
        }),
        true);
}

void ViPER4HostAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && presetBox_.isParentOf(e.eventComponent))
        showPresetMenu();
}

void ViPER4HostAudioProcessorEditor::showPresetMenu()
{
    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    const int sel = presetBox_.getSelectedId();
    const int loaded = (sel >= 100 && sel - 100 < userPresetFiles_.size()) ? sel - 100 : -1;

    // 逐个列出预设：当前载入的打勾；每个带"重命名/删除"子菜单，
    // 不必先载入就能管理（载入会改当前参数，管理不该有这个副作用）。
    juce::PopupMenu menu;
    for (int i = 0; i < userPresetFiles_.size(); ++i)
    {
        juce::PopupMenu sub;
        sub.addItem(10000 + i * 2, v4h::localisation::tr(lang, "presetRename"));
        sub.addItem(10000 + i * 2 + 1, v4h::localisation::tr(lang, "presetDelete"));
        menu.addSubMenu(userPresetFiles_[i].getFileNameWithoutExtension(), sub,
            true, nullptr, i == loaded);
    }
    if (userPresetFiles_.size() > 0)
        menu.addSeparator();
    menu.addItem(3, v4h::localisation::tr(lang, "presetOpenFolder"));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBox_),
        [this](int choice)
        {
            if (choice == 3)
            {
                presetsDir().startAsProcess();
            }
            else if (choice >= 10000 && (choice - 10000) / 2 < userPresetFiles_.size())
            {
                if ((choice - 10000) % 2 == 0)
                    renamePresetDialog((choice - 10000) / 2);
                else
                    deletePresetWithConfirm((choice - 10000) / 2);
            }
        });
}

void ViPER4HostAudioProcessorEditor::deletePresetWithConfirm(int fileIndex)
{
    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    const auto file = userPresetFiles_[fileIndex];
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
        v4h::localisation::tr(lang, "presetDelete"),
        v4h::localisation::tr(lang, "presetDeleteConfirm") + "\n" + file.getFileName(),
        {}, {}, this,
        juce::ModalCallbackFunction::create([this, file](int ok)
        {
            if (!ok)
                return;
            file.deleteFile();
            // 删除的正好是当前载入的预设时，顺带清掉下拉框的选择。
            const int sel = presetBox_.getSelectedId();
            if (sel >= 100 && sel - 100 < userPresetFiles_.size()
                && userPresetFiles_[sel - 100] == file)
                presetBox_.setSelectedId(0, juce::dontSendNotification);
            refreshPresetList();
        }));
}

void ViPER4HostAudioProcessorEditor::renamePresetDialog(int fileIndex)
{
    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    const auto file = userPresetFiles_[fileIndex];
    auto* window = new juce::AlertWindow(v4h::localisation::tr(lang, "presetRename"), {},
        juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", file.getFileNameWithoutExtension());
    window->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, window, file, lang](int result)
        {
            if (result != 1)
                return;
            // 与保存预设一致：Windows 文件名非法字符替换，不拒绝。
            auto name = window->getTextEditorContents("name").trim();
            for (const char* p = "/\\:*?\"<>|"; *p != '\0'; ++p)
                name = name.replaceCharacter(static_cast<juce::juce_wchar>(*p), '_');
            if (name.isEmpty())
                return;
            const auto target = presetsDir().getChildFile(name + ".xml");
            if (! file.moveFileTo(target))
            {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    v4h::localisation::tr(lang, "presetRename"),
                    v4h::localisation::tr(lang, "presetRenameFailed"));
                return;
            }
            // 改名的正好是当前载入的预设时，把选择跟到新名字上。
            const int sel = presetBox_.getSelectedId();
            const bool wasSelected = sel >= 100 && sel - 100 < userPresetFiles_.size()
                && userPresetFiles_[sel - 100] == file;
            refreshPresetList();
            if (wasSelected)
                for (int i = 0; i < userPresetFiles_.size(); ++i)
                    if (userPresetFiles_[i] == target)
                    {
                        presetBox_.setSelectedId(100 + i, juce::dontSendNotification);
                        break;
                    }
        }),
        true);
}

ViPER4HostAudioProcessorEditor::ParamGroup* ViPER4HostAudioProcessorEditor::addGroup(
    const juce::String& groupId, std::vector<juce::String> paramIds, bool titleEnable)
{
    auto pg = std::make_unique<ParamGroup>();
    pg->groupId = groupId;

    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    for (const auto& id : paramIds)
    {
        auto row = std::make_unique<ParamRow>();
        // 分组的 "*Enable" 开关放在分组标题栏里……
        bindRow(*row, id, lang, id.endsWith("Enable") && titleEnable
            ? (juce::Component&) pg->group : content_, titleEnable);
        pg->rows.push_back(std::move(row));
    }

    // 分组的启用开关（如果有）兼任折叠控件。
    if (titleEnable)
    {
        const auto enableIt = std::find_if(paramIds.begin(), paramIds.end(),
            [](const juce::String& id) { return id.endsWith("Enable"); });
        if (enableIt != paramIds.end())
        {
            pg->enableParamId = *enableIt;
            pg->enableAttachment = std::make_unique<juce::ParameterAttachment>(
                *processor_.getParametersState().getParameter(pg->enableParamId),
                [this](float) { refreshCollapse(); },
                nullptr);
        }
    }

    if (groupId == "groupConv" || groupId == "groupDdc")
    {
        pg->hasFileRow = true;
        pg->fileKind = groupId == "groupConv" ? "convolverPath" : "ddcPath";
        pg->fileButton.onClick = [this, &group = *pg] { pickFile(group); };
        pg->group.addAndMakeVisible(pg->fileLabel);
        pg->group.addAndMakeVisible(pg->fileButton);
        pg->group.addAndMakeVisible(pg->fileStatus);
    }

    pg->page = pageOf(groupId);
    // 折叠状态来自启用开关（refreshCollapse）；没有开关的分组
    // （groupMaster）默认展开。

    content_.addAndMakeVisible(pg->group);
    pg->group.toBack();
    groups_.push_back(std::move(pg));
    // 先入列再读初值：refreshCollapse 遍历 groups_，入列前调用会漏掉
    // 本分组自己，导致最后一个带开关的分组启动时折叠状态错误。
    if (groups_.back()->enableAttachment != nullptr)
        groups_.back()->enableAttachment->sendInitialUpdate();
    return groups_.back().get();
}

// addGroup 和 select*Band 重绑定助手共用的行构建逻辑。附着每次从零
// 重建，所以重绑定就是用新 id 调一次 bindRow。titleEnable=false 时
// "*Enable" 参数保持普通开关行。
void ViPER4HostAudioProcessorEditor::bindRow(ParamRow& row, const juce::String& id,
    const juce::String& languageCode, juce::Component& parent, bool titleEnable)
{
    auto& apvts = processor_.getParametersState();
    auto* param = apvts.getParameter(id);
    jassert(param != nullptr);
    row.paramId = id;
    row.isEnable = titleEnable && id.endsWith("Enable");
    row.sliderAttachment.reset();
    row.comboAttachment.reset();
    row.buttonAttachment.reset();

    if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr)
    {
        row.buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, id, row.toggle);
        parent.addAndMakeVisible(row.toggle);
    }
    else if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
    {
        row.label.setJustificationType(juce::Justification::centredLeft);
        fillCombo(row.combo, id, choiceParam->choices, languageCode);
        row.comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, id, row.combo);
    }
    else
    {
        row.slider.setSliderStyle(juce::Slider::LinearHorizontal);
        row.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 84, 22);
        row.label.setJustificationType(juce::Justification::centredLeft);
        row.sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, id, row.slider);
    }

    if (!row.isEnable)
    {
        parent.addAndMakeVisible(row.label);
        if (row.comboAttachment != nullptr)
            parent.addAndMakeVisible(row.combo);
        else if (row.sliderAttachment != nullptr)
            parent.addAndMakeVisible(row.slider);
    }
}

void ViPER4HostAudioProcessorEditor::selectDeBand(int band)
{
    selectedDeBand_ = juce::jlimit(0, (int) deBandRows_.size() - 1, band);
    static const char* suffixes[] = { "Freq", "Q", "Gain", "Threshold", "Attack", "Release", "Type" };
    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    const auto pfx = "de" + juce::String(selectedDeBand_ + 1);
    for (size_t i = 0; i < deBandRows_.size() && i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
        bindRow(*deBandRows_[i], pfx + suffixes[i], lang, content_);
}

void ViPER4HostAudioProcessorEditor::selectMbcBand(int band)
{
    selectedMbcBand_ = juce::jlimit(0, (int) mbcBandRows_.size() - 1, band);
    static const char* suffixes[] = { "Enable", "Threshold", "Ratio", "Knee", "KneeAuto",
        "Gain", "GainAuto", "Attack", "AttackAuto", "Release", "ReleaseAuto", "KneeMulti",
        "MaxAttack", "MaxRelease", "Crest", "Adapt", "NoClip" };
    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    const auto pfx = "mbc" + juce::String(selectedMbcBand_ + 1);
    for (size_t i = 0; i < mbcBandRows_.size() && i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
        bindRow(*mbcBandRows_[i], pfx + suffixes[i], lang, content_, false);
}

// 折叠的分组只剩标题栏；标题栏里的启用开关是唯一控件。MBC 详情面板
// 还要额外跟随 mbcEnable。
void ViPER4HostAudioProcessorEditor::refreshCollapse()
{
    auto& apvts = processor_.getParametersState();
    const auto isOn = [&apvts](const juce::String& id) {
        auto* param = apvts.getParameter(id);
        return param != nullptr && param->getValue() > 0.5f;
    };
    for (auto& pg : groups_)
    {
        bool expanded = true;
        if (pg->enableParamId.isNotEmpty())
            expanded = isOn(pg->enableParamId);
        if (pg->groupId == "groupMbcSel")
            expanded = expanded && isOn("mbcEnable");
        pg->collapsed = !expanded;
        // 标题栏样式同步到组件属性，Theme.h 据此决定圆角画法。
        // 折叠态、或正文为零的分组（如扬声器优化，展开也没有内容）
        // 都按"只剩标题栏"画：四角全圆，不画与正文的分隔线。
        const bool hasBody = pg->hasFileRow || pg->curve != nullptr || pg->strip != nullptr
            || pg->siView != nullptr || pg->grMeter != nullptr
            || std::any_of(pg->rows.begin(), pg->rows.end(),
                [](const auto& r) { return !r->isEnable; });
        if (pg->collapsed || !hasBody)
            pg->group.getProperties().set("v4hTitleOnly", 1);
        else
            pg->group.getProperties().remove("v4hTitleOnly");
    }
    refreshTabDots();
    resized();
}

// 页签上的青点：该页至少一个分组的启用开关是开的（有东西在生效）。
// 跟着 refreshCollapse 一起刷，覆盖开关切换、预设载入、A/B 切换。
void ViPER4HostAudioProcessorEditor::refreshTabDots()
{
    auto& apvts = processor_.getParametersState();
    bool pageActive[numPages] = {};
    for (auto& pg : groups_)
    {
        if (pg->enableParamId.isEmpty())
            continue;
        auto* param = apvts.getParameter(pg->enableParamId);
        if (param != nullptr && param->getValue() > 0.5f)
            pageActive[pg->page] = true;
    }
    for (int i = 0; i < numPages; ++i)
        pageButtons_[i].setActiveDot(pageActive[i]);
}

void ViPER4HostAudioProcessorEditor::updateBandVisibility()
{
    auto& apvts = processor_.getParametersState();
    const auto bandCount = [&apvts](const char* id) -> int {
        auto* param = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(id));
        jassert(param != nullptr);
        return param != nullptr ? param->getIndex() : 0;
    };
    constexpr int eqCounts[] = { 10, 15, 25, 31 };
    const int eqN = eqCounts[bandCount("eqBandCount")];
    const int deN = bandCount("deBandCount") + 1;
    // 引擎只支持 3/5 段，界面档位与之对应（0 = 3 段，1 = 5 段）。
    const int mbcN = bandCount("mbcBandCount") == 0 ? 3 : 5;

    // EQ / MBC 的图形跟随频段数变化。
    for (auto& pg : groups_)
    {
        const auto& gid = pg->groupId;
        if (gid == "groupEq")
        {
            if (pg->curve != nullptr)
                pg->curve->setBands("eqBand", eqN);
        }
        else if (gid == "groupDe")
        {
            if (pg->curve != nullptr)
                pg->curve->setBands("de", deN);
        }
        else if (gid == "groupMbc")
        {
            if (pg->strip != nullptr)
                pg->strip->setBands(mbcN);
        }
    }
    if (selectedDeBand_ >= deN)
        selectDeBand(deN - 1); // 详情行跟随变小的频段数
    if (selectedMbcBand_ >= mbcN)
        selectMbcBand(mbcN - 1);
    relabelEqBands(); // 段数变化后 EQ 滑杆标签跟随引擎频点
    resized();
}

void ViPER4HostAudioProcessorEditor::fillCombo(
    juce::ComboBox& combo, const juce::String& paramId,
    const juce::StringArray& choices, const juce::String& languageCode)
{
    const int selected = combo.getSelectedItemIndex();
    combo.clear(juce::dontSendNotification);
    for (int i = 0; i < choices.size(); ++i)
    {
        auto text = v4h::localisation::choiceText(paramId, i, languageCode);
        combo.addItem(text.isNotEmpty() ? text : choices[static_cast<size_t>(i)], i + 1);
    }
    combo.setSelectedItemIndex(selected, juce::dontSendNotification);
}

void ViPER4HostAudioProcessorEditor::pickFile(ParamGroup& group)
{
    const bool isConv = group.fileKind == "convolverPath";
    const auto language = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    fileChooser_ = std::make_unique<juce::FileChooser>(
        v4h::localisation::tr(language, "chooseFile"), juce::File::getCurrentWorkingDirectory(),
        isConv ? "*.wav;*.aiff;*.flac" : "*.ddc;*.txt");
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, isConv, &group](const juce::FileChooser& fc) {
            const auto result = fc.getResult();
            if (!result.existsAsFile())
                return;
            const auto error = isConv ? processor_.loadConvolverFile(result.getFullPathName())
                                      : processor_.loadDdcFile(result.getFullPathName());
            if (error.isEmpty())
                refreshFileStatus(group);
            else
                group.fileStatus.setText(v4h::localisation::tr(processor_.getUiLanguage(), error),
                    juce::dontSendNotification);
        });
}

void ViPER4HostAudioProcessorEditor::refreshFileStatus(ParamGroup& group)
{
    const auto path = processor_.getParametersState().state.getProperty(group.fileKind, {}).toString();
    group.fileStatus.setText(path.isEmpty()
            ? v4h::localisation::tr(processor_.getUiLanguage(), "noFile")
            : juce::File(path).getFileName(),
        juce::dontSendNotification);
}

void ViPER4HostAudioProcessorEditor::relabelAll(const juce::String& languageCode)
{
    using namespace v4h::localisation;
    const auto lang = normaliseLanguageCode(languageCode);

    titleLabel_.setText(tr(lang, "pluginTitle"), juce::dontSendNotification);
    languageLabel_.setText(tr(lang, "uiLanguage"), juce::dontSendNotification);
    for (int i = 0; i < numPages; ++i)
        pageButtons_[i].setButtonText(tr(lang, pageKeys[i]));

    for (auto& pg : groups_)
    {
        pg->group.setText(tr(lang, pg->groupId));
        for (auto& row : pg->rows)
        {
            if (row->isEnable) // 开关在标题栏里，没有标签
                continue;
            row->label.setText(tr(lang, labelKey(row->paramId)), juce::dontSendNotification);
            if (row->comboAttachment != nullptr)
                if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(
                        processor_.getParametersState().getParameter(row->paramId)))
                    fillCombo(row->combo, row->paramId, choiceParam->choices, lang);
        }
        if (pg->hasFileRow)
        {
            pg->fileLabel.setText(tr(lang, pg->groupId == "groupConv" ? "convFile" : "ddcFile"),
                juce::dontSendNotification);
            pg->fileButton.setButtonText(tr(lang, "chooseFile"));
            refreshFileStatus(*pg);
        }
    }

    abCopy_.setButtonText(tr(lang, "abCopy"));
    relabelEqBands(); // EQ 频段标签是频率文本，不走本地化键
    refreshPresetList(); // 重建下拉框，带上已翻译的预设名
}

// EQ 频段滑杆的标签按当前段数取引擎真实频点（本地化文件里的固定 ISO 名
// 只在 31 段模式完全一致）。频率文本语言无关，直接用通用格式化。
void ViPER4HostAudioProcessorEditor::relabelEqBands()
{
    auto* countParam = dynamic_cast<juce::AudioParameterChoice*>(
        processor_.getParametersState().getParameter("eqBandCount"));
    if (countParam == nullptr)
        return;
    constexpr int counts[] = { 10, 15, 25, 31 };
    const float* table = v4h::EqCurve::freqTable(counts[countParam->getIndex()]);
    for (auto& pg : groups_)
    {
        if (pg->groupId != "groupEq")
            continue;
        for (auto& row : pg->rows)
        {
            const int band = eqBandNumberOf(row->paramId);
            if (band > 0)
                row->label.setText(v4h::freqText(table[band - 1]),
                    juce::dontSendNotification);
        }
    }
}

void ViPER4HostAudioProcessorEditor::resized()
{
    auto body = getLocalBounds();
    auto headerArea = body.removeFromTop(headerHeight).reduced(gap + 4, 0);
    titleLabel_.setBounds(headerArea.removeFromLeft(120));

    // 预设 + A/B 排在标题和语言选择器之间
    presetBox_.setBounds(headerArea.removeFromLeft(180).withSizeKeepingCentre(176, 28));
    auto abArea = headerArea.removeFromLeft(3 * 34 + 2 * 6 + 8);
    abButtons_[0].setBounds(abArea.removeFromLeft(34).withSizeKeepingCentre(30, 26));
    abArea.removeFromLeft(6);
    abButtons_[1].setBounds(abArea.removeFromLeft(34).withSizeKeepingCentre(30, 26));
    abArea.removeFromLeft(6);
    abCopy_.setBounds(abArea.withSizeKeepingCentre(42, 26));

    auto langArea = headerArea.removeFromRight(96 + gap + 130);
    languageLabel_.setBounds(langArea.removeFromLeft(96));
    languageBox_.setBounds(langArea.withSizeKeepingCentre(126, 28));

    auto tabArea = body.removeFromTop(tabBarHeight).reduced(gap, 0);
    const int tabWidth = (tabArea.getWidth() - gap * (numPages - 1)) / numPages;
    for (int i = 0; i < numPages; ++i)
    {
        pageButtons_[i].setBounds(tabArea.removeFromLeft(tabWidth));
        tabArea.removeFromLeft(gap);
    }

    viewport_.setBounds(body.reduced(gap));

    // 单列布局：每个分组横跨整个内容宽度。内容填满视口宽度（最小 560，
    // 更窄就横向滚动）。只布局当前页上的分组。
    const int contentWidth = juce::jmax(560, viewport_.getMaximumVisibleWidth());
    const int columnWidth = contentWidth - gap * 2;

    const auto applyVis = [](ParamRow& row, bool on) {
        row.label.setVisible(on);
        row.slider.setVisible(on);
        row.combo.setVisible(on);
        row.toggle.setVisible(on);
    };

    // 图形 EQ 超出当前段数的频段滑杆不进网格。
    int eqN = 31;
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(
            processor_.getParametersState().getParameter("eqBandCount")))
    {
        constexpr int eqCounts[] = { 10, 15, 25, 31 };
        eqN = eqCounts[p->getIndex()];
    }

    int y = gap;
    for (auto& pg : groups_)
    {
        const bool show = pg->page == currentPage_;
        pg->group.setVisible(show);
        if (!show)
        {
            // 参数行是 content_ 的子控件，要显式隐藏。启用开关是分组的
            // 子控件——分组可见性已经覆盖它，这里再动它的话，回来时
            // 它会一直是隐藏的。
            for (auto& row : pg->rows)
                if (!row->isEnable)
                    applyVis(*row, false);
            continue;
        }

        int rowCount = 0;
        for (auto& row : pg->rows)
        {
            if (row->isEnable) // 开关在标题栏里，不在网格里
                continue;
            bool on = !pg->collapsed;
            if (on && pg->groupId == "groupEq")
            {
                const int band = eqBandNumberOf(row->paramId);
                on = band == 0 || band <= eqN;
            }
            applyVis(*row, on);
            if (on)
                ++rowCount;
        }

        const bool collapsed = pg->collapsed;
        // 图形条（曲线 / 分频条 / 场视图）放在标题和参数行之间，
        // 与标题栏隔一个 gap，图的上边不压标题。
        int graphicH = 0;
        if (pg->curve != nullptr)
        {
            graphicH = !collapsed ? curveHeight : 0;
            pg->curve->setVisible(graphicH != 0);
            if (graphicH != 0)
                pg->curve->setBounds(8, titleHeight + gap, columnWidth - 16, curveHeight);
        }
        else if (pg->strip != nullptr)
        {
            graphicH = !collapsed ? stripHeight : 0;
            pg->strip->setVisible(graphicH != 0);
            if (graphicH != 0)
                pg->strip->setBounds(8, titleHeight + gap, columnWidth - 16, stripHeight);
        }
        else if (pg->siView != nullptr)
        {
            graphicH = !collapsed ? siViewHeight : 0;
            pg->siView->setVisible(graphicH != 0);
            if (graphicH != 0)
                pg->siView->setBounds(8, titleHeight + gap, columnWidth - 16, siViewHeight);
        }
        else if (pg->grMeter != nullptr)
        {
            graphicH = !collapsed ? grMeterHeight : 0;
            pg->grMeter->setVisible(graphicH != 0);
            if (graphicH != 0)
                pg->grMeter->setBounds(8, titleHeight + gap, columnWidth - 16, grMeterHeight);
        }
        const int fileRowHeight = pg->hasFileRow && !collapsed ? rowHeight : 0;
        const int height = titleHeight
            + (collapsed ? 0 : gap + graphicH + rowCount * rowHeight + fileRowHeight);

        pg->group.setBounds(gap, y, columnWidth, height);
        for (auto& row : pg->rows)
            if (row->isEnable)
                row->toggle.setBounds(juce::Rectangle<int>(columnWidth - 46, 6, 38, 18));

        if (!collapsed)
        {
            auto rowsArea = juce::Rectangle<int>(gap + 8, y + titleHeight + gap + graphicH, columnWidth - 16, rowCount * rowHeight + fileRowHeight);
            for (auto& row : pg->rows)
            {
                if (!row->label.isVisible())
                    continue;
                auto rowBounds = rowsArea.removeFromTop(rowHeight).reduced(4, 1);
                row->label.setBounds(rowBounds.removeFromLeft(labelWidth));
                if (row->buttonAttachment != nullptr)
                    row->toggle.setBounds(rowBounds.removeFromRight(46).withSizeKeepingCentre(38, 18));
                else if (row->comboAttachment != nullptr)
                    row->combo.setBounds(rowBounds.removeFromLeft(130));
                else
                    row->slider.setBounds(rowBounds);
            }
            if (pg->hasFileRow)
            {
                // 文件行是 pg->group 的子控件：坐标相对分组，
                // 而参数行在 content_ 坐标系里。
                // shortcut: 未叠加 graphicH 偏移——conv/ddc 目前没有图形条，
                // 若将来给它们加曲线/量表，这里的 Y 要加上 graphicH。
                auto fileBounds = juce::Rectangle<int>(8, titleHeight + rowCount * rowHeight,
                    columnWidth - 16, rowHeight).reduced(4, 1);
                pg->fileLabel.setBounds(fileBounds.removeFromLeft(labelWidth));
                pg->fileButton.setBounds(fileBounds.removeFromLeft(94).withSizeKeepingCentre(90, 26));
                pg->fileStatus.setBounds(fileBounds);
            }
        }

        y += height + gap;
    }

    content_.setSize(contentWidth, y + gap);

    // 垂直滚动条出现/消失会改变可视宽度，导致本次布局用的宽度已过期；
    // 重跑一次布局让内容宽度跟上（滚动条状态稳定后第二轮即收敛）。
    if (juce::jmax(560, viewport_.getMaximumVisibleWidth()) != contentWidth)
        resized();
}

void ViPER4HostAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace v4h::theme;
    g.setColour(c(background));
    g.fillAll();

    auto strip = getLocalBounds().removeFromTop(headerHeight).toFloat();
    g.setColour(c(header));
    g.fillRect(strip);
    // 顶栏底部：深色发丝线叠一条青色高光。
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRect(0.0f, strip.getBottom() - 1.0f, static_cast<float>(getWidth()), 1.0f);
    g.setColour(c(accent).withAlpha(0.55f));
    g.fillRect(0.0f, strip.getBottom() - 1.0f, static_cast<float>(getWidth()), 1.0f);
}
