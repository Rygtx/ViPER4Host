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
constexpr int curveHeight = 210; // EQ / DynEQ response graph
constexpr int stripHeight = 60;  // MBC crossover strip
constexpr int siViewHeight = 190; // stereo imager field view

// Category tabs, in order. Count must match numPages in the header.
const char* pageKeys[] = { "tabOutput", "tabDynamics", "tabEq", "tabBass", "tabStage", "tabFx" };
static_assert(sizeof(pageKeys) / sizeof(pageKeys[0]) == ViPER4HostAudioProcessorEditor::numPages);

// Which category tab a group lives on. 0 output, 1 dynamics, 2 eq, 3 bass,
// 4 surround, 5 everything else.
int pageOf(const juce::String& gid)
{
    if (gid == "groupMaster" || gid == "groupAGC" || gid == "groupLufs")
        return 0;
    if (gid == "groupFet" || gid.startsWith("groupMbc"))
        return 1;
    if (gid == "groupEq" || gid.startsWith("groupDe") || gid == "groupCure")
        return 2;
    if (gid == "groupPsyBass" || gid == "groupBass" || gid == "groupBassMono" || gid == "groupSpecExt")
        return 3;
    if (gid == "groupField" || gid == "groupDiff" || gid == "groupSi" || gid == "groupDs")
        return 4;
    return 5;
}

// Band params (mbc3Threshold, de7Freq) share one label key per role across
// all bands: mbcN* -> mbcBand*, deN* -> deBand*.
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

    // Sections in the same order as the parameter layout.
    addGroup("groupMaster", { "limiterThreshold", "limiterVolume", "limiterPan" });
    addGroup("groupAGC", { "pgcEnable", "pgcStrength", "pgcMaxGain", "pgcOutputThreshold" });
    addGroup("groupLufs", { "lufsEnable", "lufsTarget", "lufsMaxGain", "lufsSpeed" });
    addGroup("groupFet", { "fetEnable", "fetThreshold", "fetRatio", "fetKnee", "fetKneeAuto",
        "fetGain", "fetGainAuto", "fetAttack", "fetAttackAuto", "fetRelease", "fetReleaseAuto",
        "fetKneeMulti", "fetMaxAttack", "fetMaxRelease", "fetCrest", "fetAdapt", "fetNoClip" });

    {
        auto& pg = *addGroup("groupEq", { "eqEnable", "eqBandCount" });
        pg.curve = std::make_unique<v4h::EqCurve>(
            processor_.getParametersState(), v4h::EqCurve::Mode::Graphic);
        pg.group.addAndMakeVisible(*pg.curve);
    }

    {
        auto& pg = *addGroup("groupDe", { "deEnable", "deBandCount",
            "de1Freq", "de1Q", "de1Gain", "de1Threshold", "de1Attack", "de1Release", "de1Type" });
        pg.curve = std::make_unique<v4h::EqCurve>(
            processor_.getParametersState(), v4h::EqCurve::Mode::Dynamic);
        // Clicking a node on the graph switches the detail rows to that band.
        pg.curve->onSelect = [this](int band) { selectDeBand(band); };
        pg.group.addAndMakeVisible(*pg.curve);
        for (auto& row : pg.rows)
            if (!row->isEnable && row->paramId != "deBandCount")
                deBandRows_.push_back(row.get()); // creation order == bind order
    }

    addGroup("groupPsyBass", { "pBassEnable", "pBassCutoff", "pBassIntensity", "pBassHarmonicOrder", "pBassOriginalLevel" });
    addGroup("groupBass", { "bassEnable", "bassMode", "bassFreq", "bassGain", "bassAntiPop" });
    addGroup("groupBassMono", { "bassMonoEnable", "bassMonoMode", "bassMonoFreq", "bassMonoGain", "bassMonoAntiPop" });
    addGroup("groupSpecExt", { "specExtEnable", "specExtStrength", "specExtExciter" });
    addGroup("groupConv", { "convEnable", "convCrossChannel" });
    addGroup("groupDdc", { "ddcEnable" });
    addGroup("groupField", { "fieldEnable", "fieldWidening", "fieldMidImage", "fieldDepth" });
    addGroup("groupDiff", { "diffEnable", "diffDelay", "diffReverse", "diffMix", "diffLpCutoff" });
    addGroup("groupSi", { "siEnable" });
    groups_.back()->siView = std::make_unique<v4h::SiView>(processor_.getParametersState());
    groups_.back()->group.addAndMakeVisible(*groups_.back()->siView);
    addGroup("groupVHE", { "vheEnable", "vheQuality" });
    addGroup("groupReverb", { "reverbEnable", "reverbRoomSize", "reverbWidth", "reverbDamp", "reverbWet", "reverbDry" });
    addGroup("groupDs", { "dsEnable", "dsXLow", "dsXHigh", "dsYLow", "dsYHigh", "dsSideGainLow", "dsSideGainHigh", "dsStrength" });
    addGroup("groupClarity", { "clarityEnable", "clarityMode", "clarityGain" });
    addGroup("groupCure", { "cureEnable", "curePreset" });
    addGroup("groupTube", { "tubeEnable", "tubeModel", "tubeDrive", "tubeMix" });
    addGroup("groupAx", { "axEnable", "axMode" });
    addGroup("groupSpk", { "spkEnable" });
    {
        auto& pg = *addGroup("groupMbc", { "mbcEnable", "mbcBandCount" });
        pg.strip = std::make_unique<v4h::XoverStrip>(processor_.getParametersState());
        // Clicking a region selects that band; the detail rows follow.
        pg.strip->onSelect = [this](int band) { selectMbcBand(band); };
        pg.group.addAndMakeVisible(*pg.strip);
    }

    {
        // One detail panel for the selected MBC band (titleEnable=false so
        // mbcNEnable is a plain toggle row, not this group's fold switch).
        static const char* suffixes[] = { "Enable", "Threshold", "Ratio", "Knee", "KneeAuto",
            "Gain", "GainAuto", "Attack", "AttackAuto", "Release", "ReleaseAuto", "KneeMulti",
            "MaxAttack", "MaxRelease", "Crest", "Adapt", "NoClip" };
        std::vector<juce::String> ids;
        for (const char* s : suffixes)
            ids.push_back(juce::String("mbc1") + s);
        auto& pg = *addGroup("groupMbcSel", ids, false);
        for (auto& row : pg.rows)
            mbcBandRows_.push_back(row.get()); // creation order == bind order
    }

    // Band-count params drive which band rows/groups are shown.
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
    setSize(defaultWidth, defaultHeight);

    const auto language = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    juce::StringArray codes;
    for (const auto& option : v4h::localisation::availableLanguages())
        codes.add(option.code);
    languageBox_.setSelectedId(codes.indexOf(language) + 1, juce::dontSendNotification);
    relabelAll(language);
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

ViPER4HostAudioProcessorEditor::ParamGroup* ViPER4HostAudioProcessorEditor::addGroup(
    const juce::String& groupId, std::vector<juce::String> paramIds, bool titleEnable)
{
    auto pg = std::make_unique<ParamGroup>();
    pg->groupId = groupId;

    const auto lang = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    for (const auto& id : paramIds)
    {
        auto row = std::make_unique<ParamRow>();
        // The group's "*Enable" switch lives in the group title bar...
        bindRow(*row, id, lang, id.endsWith("Enable") && titleEnable
            ? (juce::Component&) pg->group : content_, titleEnable);
        pg->rows.push_back(std::move(row));
    }

    // The group's enable switch (if it has one) doubles as the fold control.
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
    // Fold state comes from the enable switches (refreshCollapse); groups
    // without one (groupMaster) default to open.

    content_.addAndMakeVisible(pg->group);
    pg->group.toBack();
    groups_.push_back(std::move(pg));
    return groups_.back().get();
}

// Row construction shared by addGroup and the select*Band rebinding helpers.
// Attachments are rebuilt from scratch, so rebinding is just bindRow with a
// new id. titleEnable=false keeps "*Enable" params as plain toggle rows.
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

// A folded group is just its title bar; the enable switch in that bar is the
// only control. The MBC detail panel additionally follows mbcEnable.
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

    // The EQ / MBC graphics follow the band counts.
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
        selectDeBand(deN - 1); // detail rows follow the shrunken band count
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
            if (row->isEnable) // switch in the title bar, no label
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
}

void ViPER4HostAudioProcessorEditor::resized()
{
    auto body = getLocalBounds();
    auto headerArea = body.removeFromTop(headerHeight).reduced(gap, 0);
    titleLabel_.setBounds(headerArea.removeFromLeft(getWidth() / 2));

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

    // Single column: each group spans the full content width. Content fills
    // the viewport width (min 560, below that scroll sideways). Only groups
    // on the current page are laid out.
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
            // Rows are content_ children, so hide them explicitly. The enable
            // switch is a group child — group visibility already covers it,
            // and touching it here would leave it hidden when we come back.
            for (auto& row : pg->rows)
                if (!row->isEnable)
                    applyVis(*row, false);
            continue;
        }

        int rowCount = 0;
        for (auto& row : pg->rows)
        {
            if (row->isEnable) // switch lives in the title bar, not the grid
                continue;
            const bool on = row->bandVisible && !pg->collapsed;
            applyVis(*row, on);
            if (on)
                ++rowCount;
        }

        const bool collapsed = pg->collapsed;
        // Graphic strip (curve / crossover / field view) between title and rows.
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
                // File row is a child of pg->group: bounds are group-relative,
                // unlike the param rows which live in content_ coordinates.
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
