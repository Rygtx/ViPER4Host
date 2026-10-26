#include "PluginEditor.h"
#include "Localisation.h"

namespace
{
constexpr int defaultWidth = 660;
constexpr int defaultHeight = 600;
constexpr int headerHeight = 28;
constexpr int gap = 8;
constexpr int rowHeight = 30;
constexpr int titleHeight = 20;
constexpr int labelWidth = 130;

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
    // Sections in the same order as the parameter layout.
    addGroup("groupMaster", { "limiterThreshold", "limiterVolume", "limiterPan" });
    addGroup("groupAGC", { "pgcEnable", "pgcStrength", "pgcMaxGain", "pgcOutputThreshold" });
    addGroup("groupLufs", { "lufsEnable", "lufsTarget", "lufsMaxGain", "lufsSpeed" });
    addGroup("groupFet", { "fetEnable", "fetThreshold", "fetRatio", "fetKnee", "fetKneeAuto",
        "fetGain", "fetGainAuto", "fetAttack", "fetAttackAuto", "fetRelease", "fetReleaseAuto",
        "fetKneeMulti", "fetMaxAttack", "fetMaxRelease", "fetCrest", "fetAdapt", "fetNoClip" });

    {
        std::vector<juce::String> eqIds = { "eqEnable", "eqBandCount" };
        for (int i = 1; i <= 31; ++i)
            eqIds.push_back("eqBand" + juce::String(i));
        addGroup("groupEq", eqIds);
    }

    addGroup("groupDe", { "deEnable", "deBandCount" });
    for (int b = 1; b <= 10; ++b)
    {
        const auto pfx = "de" + juce::String(b);
        addGroup("groupDeB" + juce::String(b), { pfx + "Freq", pfx + "Q", pfx + "Gain",
            pfx + "Threshold", pfx + "Attack", pfx + "Release", pfx + "Type" });
    }

    addGroup("groupPsyBass", { "pBassEnable", "pBassCutoff", "pBassIntensity", "pBassHarmonicOrder", "pBassOriginalLevel" });
    addGroup("groupBass", { "bassEnable", "bassMode", "bassFreq", "bassGain", "bassAntiPop" });
    addGroup("groupBassMono", { "bassMonoEnable", "bassMonoMode", "bassMonoFreq", "bassMonoGain", "bassMonoAntiPop" });
    addGroup("groupSpecExt", { "specExtEnable", "specExtStrength", "specExtExciter" });
    addGroup("groupConv", { "convEnable", "convCrossChannel" });
    addGroup("groupDdc", { "ddcEnable" });
    addGroup("groupField", { "fieldEnable", "fieldWidening", "fieldMidImage", "fieldDepth" });
    addGroup("groupDiff", { "diffEnable", "diffDelay", "diffReverse", "diffMix", "diffLpCutoff" });
    addGroup("groupSi", { "siEnable", "siLowWidth", "siMidWidth", "siHighWidth", "siLowCrossover", "siHighCrossover" });
    addGroup("groupVHE", { "vheEnable", "vheQuality" });
    addGroup("groupReverb", { "reverbEnable", "reverbRoomSize", "reverbWidth", "reverbDamp", "reverbWet", "reverbDry" });
    addGroup("groupDs", { "dsEnable", "dsXLow", "dsXHigh", "dsYLow", "dsYHigh", "dsSideGainLow", "dsSideGainHigh", "dsStrength" });
    addGroup("groupClarity", { "clarityEnable", "clarityMode", "clarityGain" });
    addGroup("groupCure", { "cureEnable", "curePreset" });
    addGroup("groupTube", { "tubeEnable", "tubeModel", "tubeDrive", "tubeMix" });
    addGroup("groupAx", { "axEnable", "axMode" });
    addGroup("groupSpk", { "spkEnable" });
    addGroup("groupMbc", { "mbcEnable", "mbcBandCount", "mbcCross1", "mbcCross2", "mbcCross3", "mbcCross4" });
    for (int b = 1; b <= 5; ++b)
    {
        const auto pfx = "mbc" + juce::String(b);
        addGroup("groupMbcB" + juce::String(b), { pfx + "Enable", pfx + "Threshold", pfx + "Ratio",
            pfx + "Knee", pfx + "KneeAuto", pfx + "Gain", pfx + "GainAuto", pfx + "Attack",
            pfx + "AttackAuto", pfx + "Release", pfx + "ReleaseAuto", pfx + "KneeMulti",
            pfx + "MaxAttack", pfx + "MaxRelease", pfx + "Crest", pfx + "Adapt", pfx + "NoClip" });
    }

    titleLabel_.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    languageLabel_.setJustificationType(juce::Justification::centredRight);
    for (const auto& option : v4h::localisation::availableLanguages())
        languageBox_.addItem(option.displayName, static_cast<int>(languageBox_.getNumItems()) + 1);
    languageBox_.onChange = [this] { selectLanguage(); };

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

ViPER4HostAudioProcessorEditor::~ViPER4HostAudioProcessorEditor() = default;

void ViPER4HostAudioProcessorEditor::addGroup(const juce::String& groupId, std::vector<juce::String> paramIds)
{
    auto pg = std::make_unique<ParamGroup>();
    pg->groupId = groupId;

    auto& apvts = processor_.getParametersState();
    for (const auto& id : paramIds)
    {
        auto* param = apvts.getParameter(id);
        jassert(param != nullptr);

        auto row = std::make_unique<ParamRow>();
        row->paramId = id;

        if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr)
        {
            row->buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, id, row->toggle);
            content_.addAndMakeVisible(row->toggle);
        }
        else if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
        {
            row->label.setJustificationType(juce::Justification::centredRight);
            fillCombo(row->combo, id, choiceParam->choices,
                v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage()));
            row->comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, id, row->combo);
            content_.addAndMakeVisible(row->combo);
        }
        else
        {
            row->slider.setSliderStyle(juce::Slider::LinearHorizontal);
            row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 84, rowHeight);
            row->label.setJustificationType(juce::Justification::centredRight);
            row->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, id, row->slider);
            content_.addAndMakeVisible(row->slider);
        }

        content_.addAndMakeVisible(row->label);
        pg->rows.push_back(std::move(row));
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

    content_.addAndMakeVisible(pg->group);
    groups_.push_back(std::move(pg));
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

    for (auto& pg : groups_)
    {
        pg->group.setText(tr(lang, pg->groupId));
        for (auto& row : pg->rows)
        {
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
    auto headerArea = getLocalBounds().removeFromTop(headerHeight).reduced(gap, 0);
    titleLabel_.setBounds(headerArea.removeFromLeft(getWidth() / 2));

    auto langArea = headerArea.removeFromRight(90 + gap + 120);
    languageLabel_.setBounds(langArea.removeFromLeft(90));
    languageBox_.setBounds(langArea.withSizeKeepingCentre(120, 24));

    viewport_.setBounds(getLocalBounds().removeFromBottom(getHeight() - headerHeight).reduced(gap));

    // Two-column layout inside the viewport content; each column stacks
    // independently so groups never leave holes when heights differ.
    // Content fills the viewport width (min 560, below that scroll sideways).
    const int contentWidth = juce::jmax(560, viewport_.getMaximumVisibleWidth());
    const int columnWidth = (contentWidth - gap * 3) / 2;

    int columnBottoms[2] = { gap, gap };
    for (auto& pg : groups_)
    {
        const int fileRowHeight = pg->hasFileRow ? rowHeight : 0;
        const int height = titleHeight + static_cast<int>(pg->rows.size()) * rowHeight + fileRowHeight;
        const int column = columnBottoms[0] <= columnBottoms[1] ? 0 : 1;
        const int x = column == 0 ? gap : gap + columnWidth + gap;

        pg->group.setBounds(x, columnBottoms[column], columnWidth, height);
        auto rowsArea = juce::Rectangle<int>(x + 8, columnBottoms[column] + titleHeight, columnWidth - 16, static_cast<int>(pg->rows.size()) * rowHeight + fileRowHeight);
        for (auto& row : pg->rows)
        {
            auto rowBounds = rowsArea.removeFromTop(rowHeight).reduced(4, 1);
            row->label.setBounds(rowBounds.removeFromLeft(labelWidth));
            if (row->buttonAttachment != nullptr)
                row->toggle.setBounds(rowBounds.removeFromLeft(labelWidth));
            else if (row->comboAttachment != nullptr)
                row->combo.setBounds(rowBounds.removeFromLeft(120));
            else
                row->slider.setBounds(rowBounds);
        }
        if (pg->hasFileRow)
        {
            // File row is a child of pg->group: bounds are group-relative,
            // unlike the param rows which live in content_ coordinates.
            auto fileBounds = juce::Rectangle<int>(8, titleHeight + static_cast<int>(pg->rows.size()) * rowHeight,
                columnWidth - 16, rowHeight).reduced(4, 1);
            pg->fileLabel.setBounds(fileBounds.removeFromLeft(labelWidth));
            pg->fileButton.setBounds(fileBounds.removeFromLeft(90).withSizeKeepingCentre(86, 24));
            pg->fileStatus.setBounds(fileBounds);
        }

        columnBottoms[column] += height + gap;
    }

    content_.setSize(contentWidth, juce::jmax(columnBottoms[0], columnBottoms[1]) + gap);
}

void ViPER4HostAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}
