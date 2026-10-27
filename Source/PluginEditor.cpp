#include "PluginEditor.h"
#include "Localisation.h"

namespace
{
constexpr int defaultWidth = 660;
constexpr int defaultHeight = 600;
constexpr int headerHeight = 40;
constexpr int tabBarHeight = 28;
constexpr int gap = 8;
constexpr int rowHeight = 30;
constexpr int titleHeight = 24;
constexpr int labelWidth = 130;
constexpr int curveHeight = 210; // EQ / DynEQ 频响图
constexpr int stripHeight = 60;  // MBC 分频条
constexpr int siViewHeight = 190; // 立体声声像器场视图
constexpr int grMeterHeight = 26; // FET / 限幅器增益衰减量表

// 分类页签，按顺序。数量必须与头文件里的 numPages 一致。
const char* pageKeys[] = { "tabIn", "tabVhe", "tabEq", "tabStage", "tabDynamics",
    "tabTone", "tabOutput" };
static_assert(sizeof(pageKeys) / sizeof(pageKeys[0]) == ViPER4HostAudioProcessorEditor::numPages);

// 每个分组属于哪个分类页签。页签按引擎信号链排列（ViPER::Process 的处理
// 顺序）：卷积器 -> VHE -> DDC -> 频谱延展 -> EQ -> 动态 EQ -> 声场环绕 ->
// 立体声声像器 -> 差分环绕 -> 播放增益 -> MBC -> FET -> 动态系统 -> 电子管
// -> 低音们 -> clarity -> cure -> analogX -> 混响 -> 扬声器修正 -> LUFS ->
// 限幅器。每个页签是信号链上一段连续区间，页签内的分组也按这条链的顺序
// 排列（下面的分组创建必须与之对应）。
int pageOf(const juce::String& gid)
{
    if (gid == "groupConv" || gid == "groupDdc")
        return 0; // 输入侧修正：卷积核、DDC 曲线
    if (gid == "groupVHE")
        return 1; // 耳机环绕虚拟化
    if (gid == "groupSpecExt" || gid == "groupEq" || gid == "groupDe")
        return 2; // 频率响应：激励器、EQ、动态 EQ
    if (gid == "groupField" || gid == "groupSi" || gid == "groupDiff")
        return 3; // 声场 / 环绕
    if (gid == "groupAGC" || gid.startsWith("groupMbc") || gid == "groupFet" || gid == "groupDs")
        return 4; // 动态类，含动态系统
    if (gid == "groupTube" || gid == "groupPsyBass" || gid == "groupBass"
        || gid == "groupBassMono" || gid == "groupClarity" || gid == "groupCure"
        || gid == "groupAx" || gid == "groupReverb")
        return 5; // 音染色：电子管、低音谐波、clarity、cure、analogX、混响
    return 6; // 输出：扬声器修正、LUFS、限幅器/主控
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
} // namespace

ViPER4HostAudioProcessorEditor::ViPER4HostAudioProcessorEditor(ViPER4HostAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor)
{
    setLookAndFeel(&lookAndFeel_);

    // 频谱分析器初始化：Hann 窗 + 静音初始频谱。
    for (size_t i = 0; i < hann_.size(); ++i)
        hann_[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi
            * (float) i / (float) (hann_.size() - 1));
    spectrumDb_.fill(-96.0f);

    // 分区顺序跟随引擎信号链（见上面的 pageOf），所以每个页签里的
    // 分组都按处理顺序排列。
    addGroup("groupConv", { "convEnable", "convCrossChannel" });
    addGroup("groupDdc", { "ddcEnable" });
    addGroup("groupVHE", { "vheEnable", "vheQuality" });
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

    // 预设与 A/B 对比：槽位 0 从实时状态开始，槽位 1 是它的副本。
    abSlots_[0] = abSlots_[1] = processor_.getParametersState().copyState();
    for (int s = 0; s < 2; ++s)
    {
        abButtons_[s].setButtonText(s == 0 ? "A" : "B");
        abButtons_[s].setClickingTogglesState(true);
        abButtons_[s].setRadioGroupId(2);
        abButtons_[s].onClick = [this, s] {
            if (abButtons_[s].getToggleState())
                activateAbSlot(s);
        };
        addAndMakeVisible(abButtons_[s]);
    }
    abButtons_[0].setToggleState(true, juce::dontSendNotification);
    abCopy_.onClick = [this] { abSlots_[activeAbSlot_] = processor_.getParametersState().copyState(); };
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
    startTimerHz(30);
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
    pullMetersAndSpectrum();

    // 窗口尺寸稳定约 1 秒后持久化。
    const std::array<int, 2> size = { getWidth(), getHeight() };
    if (size != savedSize_)
    {
        savedSize_ = size;
        resizeIdleTicks_ = 0;
    }
    else if (resizeIdleTicks_ >= 0 && ++resizeIdleTicks_ >= 30)
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

    // 频谱：取最新的 2048 个单声道样本，Hann 窗 -> FFT -> 对数频点。
    auto& fifo = processor_.spectrumFifo_;
    constexpr int fftSize = 2048;
    if (fifo.getNumReady() >= fftSize && processor_.getSampleRate() > 0.0)
    {
        int start1, size1, start2, size2;
        const int drop = fifo.getNumReady() - fftSize;
        fifo.prepareToRead(drop, start1, size1, start2, size2);
        fifo.finishedRead(drop);
        fifo.prepareToRead(fftSize, start1, size1, start2, size2);
        const auto copy = [&](int start, int size, int offset) {
            for (int i = 0; i < size; ++i)
                fftSamples_[(size_t) (offset + i)]
                    = processor_.spectrumBuffer_[(size_t) (start + i)];
        };
        copy(start1, size1, 0);
        copy(start2, size2, size1);
        fifo.finishedRead(fftSize);

        for (int i = 0; i < fftSize; ++i)
            fftSamples_[(size_t) i] *= hann_[(size_t) i];
        fft_.performRealOnlyForwardTransform(fftSamples_.data());

        const int nBins = fftSize / 2;
        const double binHz = processor_.getSampleRate() / fftSize;
        std::array<float, v4h::EqCurve::kSpectrumPoints> fresh{};
        int bin = 1; // 跳过 DC
        for (int i = 0; i < v4h::EqCurve::kSpectrumPoints; ++i)
        {
            const double fHi = 20.0 * std::pow(1000.0, (i + 1.0) / v4h::EqCurve::kSpectrumPoints);
            const int last = juce::jlimit(bin, nBins - 1, (int) (fHi / binHz));
            float mag = 0.0f;
            for (; bin <= last && bin < nBins; ++bin)
                mag = juce::jmax(mag, std::hypot(fftSamples_[(size_t) (2 * bin)],
                    fftSamples_[(size_t) (2 * bin + 1)]));
            // Hann 窗相干增益约 0.5：/N/4 使满幅正弦对应 0 dB。
            fresh[(size_t) i] = juce::jlimit(-96.0f, 6.0f,
                20.0f * std::log10(mag / (float) (fftSize / 4) + 1e-6f));
        }
        // 快速起音，每帧约 2.5 dB 衰减。
        for (int i = 0; i < v4h::EqCurve::kSpectrumPoints; ++i)
            spectrumDb_[(size_t) i] = juce::jmax(fresh[(size_t) i], spectrumDb_[(size_t) i] - 2.5f);
    }

    // 把实时数据推给可见的曲线图，并按当前帧率刷新它们。
    const double fs = processor_.getSampleRate();
    for (auto& pg : groups_)
    {
        if (!pg->visible || pg->page != currentPage_ || pg->collapsed)
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

void ViPER4HostAudioProcessorEditor::activateAbSlot(int slot)
{
    if (slot == activeAbSlot_)
        return;
    abSlots_[activeAbSlot_] = processor_.getParametersState().copyState();
    activeAbSlot_ = slot;
    if (abSlots_[slot].isValid())
        processor_.applyStateTree(abSlots_[slot]);
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
            pg->enableAttachment->sendInitialUpdate();
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
    }
    resized();
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
    const int mbcN = bandCount("mbcBandCount") + 1;

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
    refreshPresetList(); // 重建下拉框，带上已翻译的预设名
}

void ViPER4HostAudioProcessorEditor::resized()
{
    auto body = getLocalBounds();
    auto headerArea = body.removeFromTop(headerHeight).reduced(gap, 0);
    titleLabel_.setBounds(headerArea.removeFromLeft(110));

    // 预设 + A/B 排在标题和语言选择器之间
    presetBox_.setBounds(headerArea.removeFromLeft(160).withSizeKeepingCentre(156, 26));
    auto abArea = headerArea.removeFromLeft(112);
    abButtons_[0].setBounds(abArea.removeFromLeft(34).withSizeKeepingCentre(30, 24));
    abArea.removeFromLeft(4);
    abButtons_[1].setBounds(abArea.removeFromLeft(34).withSizeKeepingCentre(30, 24));
    abArea.removeFromLeft(4);
    abCopy_.setBounds(abArea.withSizeKeepingCentre(36, 24));

    auto langArea = headerArea.removeFromRight(90 + gap + 120);
    languageLabel_.setBounds(langArea.removeFromLeft(90));
    languageBox_.setBounds(langArea.withSizeKeepingCentre(120, 26));

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

    int y = gap;
    for (auto& pg : groups_)
    {
        const bool show = pg->visible && pg->page == currentPage_;
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
            const bool on = !pg->collapsed;
            applyVis(*row, on);
            if (on)
                ++rowCount;
        }

        const bool collapsed = pg->collapsed;
        // 图形条（曲线 / 分频条 / 场视图）放在标题和参数行之间。
        int graphicH = 0;
        if (pg->curve != nullptr)
        {
            graphicH = !collapsed ? curveHeight : 0;
            pg->curve->setVisible(graphicH != 0);
            if (graphicH != 0)
                pg->curve->setBounds(8, titleHeight, columnWidth - 16, curveHeight);
        }
        else if (pg->strip != nullptr)
        {
            graphicH = !collapsed ? stripHeight : 0;
            pg->strip->setVisible(graphicH != 0);
            if (graphicH != 0)
                pg->strip->setBounds(8, titleHeight, columnWidth - 16, stripHeight);
        }
        else if (pg->siView != nullptr)
        {
            graphicH = !collapsed ? siViewHeight : 0;
            pg->siView->setVisible(graphicH != 0);
            if (graphicH != 0)
                pg->siView->setBounds(8, titleHeight, columnWidth - 16, siViewHeight);
        }
        else if (pg->grMeter != nullptr)
        {
            graphicH = !collapsed ? grMeterHeight : 0;
            pg->grMeter->setVisible(graphicH != 0);
            if (graphicH != 0)
                pg->grMeter->setBounds(8, titleHeight, columnWidth - 16, grMeterHeight);
        }
        const int fileRowHeight = pg->hasFileRow && !collapsed ? rowHeight : 0;
        const int height = titleHeight + (collapsed ? 0 : graphicH + rowCount * rowHeight + fileRowHeight);

        pg->group.setBounds(gap, y, columnWidth, height);
        for (auto& row : pg->rows)
            if (row->isEnable)
                row->toggle.setBounds(juce::Rectangle<int>(columnWidth - 48, 2, 40, 20));

        if (!collapsed)
        {
            auto rowsArea = juce::Rectangle<int>(gap + 8, y + titleHeight + graphicH, columnWidth - 16, rowCount * rowHeight + fileRowHeight);
            for (auto& row : pg->rows)
            {
                if (!row->label.isVisible())
                    continue;
                auto rowBounds = rowsArea.removeFromTop(rowHeight).reduced(4, 1);
                row->label.setBounds(rowBounds.removeFromLeft(labelWidth));
                if (row->buttonAttachment != nullptr)
                    row->toggle.setBounds(rowBounds.removeFromRight(44).withSizeKeepingCentre(40, 20));
                else if (row->comboAttachment != nullptr)
                    row->combo.setBounds(rowBounds.removeFromLeft(120));
                else
                    row->slider.setBounds(rowBounds);
            }
            if (pg->hasFileRow)
            {
                // 文件行是 pg->group 的子控件：坐标相对分组，
                // 而参数行在 content_ 坐标系里。
                auto fileBounds = juce::Rectangle<int>(8, titleHeight + rowCount * rowHeight,
                    columnWidth - 16, rowHeight).reduced(4, 1);
                pg->fileLabel.setBounds(fileBounds.removeFromLeft(labelWidth));
                pg->fileButton.setBounds(fileBounds.removeFromLeft(90).withSizeKeepingCentre(86, 24));
                pg->fileStatus.setBounds(fileBounds);
            }
        }

        y += height + gap;
    }

    content_.setSize(contentWidth, y + gap);
}

void ViPER4HostAudioProcessorEditor::paint(juce::Graphics& g)
{
    using namespace v4h::theme;
    g.fillAll(c(background));

    auto strip = getLocalBounds().removeFromTop(headerHeight).toFloat();
    g.setColour(c(header));
    g.fillRect(strip);
    g.setColour(c(accent));
    g.fillRect(strip.removeFromBottom(2.0f));
}
