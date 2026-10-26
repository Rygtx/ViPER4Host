#include "PluginEditor.h"

namespace
{
constexpr int defaultWidth = 660;
constexpr int defaultHeight = 560;
constexpr int gap = 8;
constexpr int rowHeight = 30;
constexpr int titleHeight = 20;
constexpr int labelWidth = 130;
} // namespace

ViPER4HostAudioProcessorEditor::ViPER4HostAudioProcessorEditor(ViPER4HostAudioProcessor& processor)
    : AudioProcessorEditor(&processor), processor_(processor)
{
    // Sections in the same order as the parameter layout.
    addGroup("Master", { "limiterThreshold", "limiterVolume", "limiterPan" });
    addGroup("AGC", { "pgcEnable", "pgcStrength", "pgcMaxGain" });
    addGroup("ViPER Bass", { "bassEnable", "bassMode", "bassFreq", "bassGain" });
    addGroup("Bass Mono", { "bassMonoEnable", "bassMonoFreq", "bassMonoGain" });
    addGroup("Clarity", { "clarityEnable", "clarityMode", "clarityGain" });
    addGroup("Headphone Surround+", { "vheEnable", "vheQuality" });
    addGroup("Field Surround", { "fieldEnable", "fieldWidening", "fieldMidImage", "fieldDepth" });
    addGroup("Reverberation", { "reverbEnable", "reverbRoomSize", "reverbWidth", "reverbDamp", "reverbWet", "reverbDry" });
    addGroup("Cure", { "cureEnable", "curePreset" });
    addGroup("Tube", { "tubeEnable", "tubeModel", "tubeDrive", "tubeMix" });

    content_.setSize(defaultWidth, 0);
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);
    setResizable(true, false);
    setResizeLimits(320, 240, 1920, 2160);
    setSize(defaultWidth, defaultHeight);
}

ViPER4HostAudioProcessorEditor::~ViPER4HostAudioProcessorEditor() = default;

void ViPER4HostAudioProcessorEditor::addGroup(const juce::String& title, std::initializer_list<const char*> paramIds)
{
    auto pg = std::make_unique<ParamGroup>();
    pg->group.setText(title);

    auto& apvts = processor_.getParametersState();
    for (const char* id : paramIds)
    {
        auto* param = apvts.getParameter(id);
        jassert(param != nullptr);

        auto row = std::make_unique<ParamRow>();
        row->label.setText(param->getName(64), juce::dontSendNotification);
        row->label.setJustificationType(juce::Justification::centredRight);
        row->label.setFont({ 13.0f });

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

void ViPER4HostAudioProcessorEditor::resized()
{
    viewport_.setBounds(getLocalBounds().reduced(gap));

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
