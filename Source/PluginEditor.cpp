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
} // namespace

ViPER4HostAudioProcessorEditor::ViPER4HostAudioProcessorEditor(ViPER4HostAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor)
{
    // Sections in the same order as the parameter layout.
    addGroup("groupMaster", { "limiterThreshold", "limiterVolume", "limiterPan" });
    addGroup("groupAGC", { "pgcEnable", "pgcStrength", "pgcMaxGain" });
    addGroup("groupBass", { "bassEnable", "bassMode", "bassFreq", "bassGain" });
    addGroup("groupBassMono", { "bassMonoEnable", "bassMonoFreq", "bassMonoGain" });
    addGroup("groupClarity", { "clarityEnable", "clarityMode", "clarityGain" });
    addGroup("groupVHE", { "vheEnable", "vheQuality" });
    addGroup("groupField", { "fieldEnable", "fieldWidening", "fieldMidImage", "fieldDepth" });
    addGroup("groupReverb", { "reverbEnable", "reverbRoomSize", "reverbWidth", "reverbDamp", "reverbWet", "reverbDry" });
    addGroup("groupCure", { "cureEnable", "curePreset" });
    addGroup("groupTube", { "tubeEnable", "tubeModel", "tubeDrive", "tubeMix" });

    titleLabel_.setFont({ 16.0f, juce::Font::bold });
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    languageLabel_.setJustificationType(juce::Justification::centredRight);
    for (const auto& option : v4h::localisation::availableLanguages())
    {
        auto button = std::make_unique<juce::ToggleButton>(option.displayName);
        button->setClickingTogglesState(true);
        button->onClick = [this, index = static_cast<int>(languageButtons_.size())] {
            selectLanguage(index);
        };
        languageButtons_.push_back(std::move(button));
    }

    content_.setSize(defaultWidth, 0);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(titleLabel_);
    addAndMakeVisible(languageLabel_);
    for (auto& button : languageButtons_)
        addAndMakeVisible(*button);
    addAndMakeVisible(viewport_);
    setResizable(true, false);
    setResizeLimits(320, 240, 1920, 2160);
    setSize(defaultWidth, defaultHeight);

    const auto language = v4h::localisation::normaliseLanguageCode(processor_.getUiLanguage());
    for (int i = 0; i < static_cast<int>(languageButtons_.size()); ++i)
        languageButtons_[static_cast<size_t>(i)]->setToggleState(
            v4h::localisation::availableLanguages()[static_cast<size_t>(i)].code == language,
            juce::dontSendNotification);
    relabelAll(language);
}

void ViPER4HostAudioProcessorEditor::selectLanguage(int index)
{
    const auto& languages = v4h::localisation::availableLanguages();
    const auto& code = languages[static_cast<size_t>(juce::jlimit(
        0, static_cast<int>(languages.size()) - 1, index))].code;
    for (int i = 0; i < static_cast<int>(languageButtons_.size()); ++i)
        languageButtons_[static_cast<size_t>(i)]->setToggleState(
            v4h::localisation::availableLanguages()[static_cast<size_t>(i)].code == code,
            juce::dontSendNotification);
    processor_.setUiLanguage(code);
    relabelAll(code);
}

ViPER4HostAudioProcessorEditor::~ViPER4HostAudioProcessorEditor() = default;

void ViPER4HostAudioProcessorEditor::addGroup(const juce::String& groupId, std::initializer_list<const char*> paramIds)
{
    auto pg = std::make_unique<ParamGroup>();
    pg->groupId = groupId;

    auto& apvts = processor_.getParametersState();
    for (const char* id : paramIds)
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
        else
        {
            row->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            row->slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, rowHeight);
            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
                row->slider.setRange(0.0, static_cast<double>(choiceParam->choices.size() - 1), 1.0);
            row->sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, id, row->slider);
            content_.addAndMakeVisible(row->slider);
        }

        content_.addAndMakeVisible(row->label);
        pg->rows.push_back(std::move(row));
    }

    content_.addAndMakeVisible(pg->group);
    groups_.push_back(std::move(pg));
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
            row->label.setText(tr(lang, row->paramId), juce::dontSendNotification);
            if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(
                    processor_.getParametersState().getParameter(row->paramId)))
            {
                const int numChoices = choiceParam->choices.size();
                row->slider.textFromValueFunction = [lang, id = row->paramId, numChoices](double value) {
                    const int index = juce::jlimit(0, numChoices - 1, static_cast<int>(value));
                    auto translated = choiceText(id, index, lang);
                    return translated.isNotEmpty() ? translated : juce::String(value);
                };
                row->slider.updateText();
            }
        }
    }
}

void ViPER4HostAudioProcessorEditor::resized()
{
    auto headerArea = getLocalBounds().removeFromTop(headerHeight).reduced(gap, 0);
    titleLabel_.setBounds(headerArea.removeFromLeft(getWidth() / 2));
    languageLabel_.setBounds(headerArea.removeFromRight(90));

    for (auto& button : languageButtons_)
        button->setBounds(headerArea.removeFromRight(84).withSizeKeepingCentre(80, 24));

    viewport_.setBounds(getLocalBounds().removeFromBottom(getHeight() - headerHeight).reduced(gap));

    // Two-column layout inside the viewport content.
    const int contentWidth = content_.getWidth();
    const int columnWidth = (contentWidth - gap * 3) / 2;

    int y = gap;
    int columnHeights[2] = { gap, gap };
    for (auto& pg : groups_)
    {
        const int height = titleHeight + static_cast<int>(pg->rows.size()) * rowHeight;
        const int column = columnHeights[0] <= columnHeights[1] ? 0 : 1;
        const int x = column == 0 ? gap : gap + columnWidth + gap;

        pg->group.setBounds(x, y, columnWidth, height);
        auto rowsArea = juce::Rectangle<int>(x + 8, y + titleHeight, columnWidth - 16, static_cast<int>(pg->rows.size()) * rowHeight);
        for (auto& row : pg->rows)
        {
            auto rowBounds = rowsArea.removeFromTop(rowHeight).reduced(4, 1);
            row->label.setBounds(rowBounds.removeFromLeft(labelWidth));
            if (row->buttonAttachment != nullptr)
                row->toggle.setBounds(rowBounds.removeFromLeft(labelWidth));
            else
                row->slider.setBounds(rowBounds);
        }

        columnHeights[column] = y + height + gap;
        if (column == 1)
            y = juce::jmax(columnHeights[0], columnHeights[1]);
    }

    content_.setSize(contentWidth, juce::jmax(columnHeights[0], columnHeights[1]) + gap);
}

void ViPER4HostAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}
