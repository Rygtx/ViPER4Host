#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <JuceHeader.h>
#include <vector>

class ViPER4HostAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit ViPER4HostAudioProcessorEditor(ViPER4HostAudioProcessor&);
    ~ViPER4HostAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct ParamRow
    {
        juce::String paramId;
        juce::Label label;
        juce::Slider slider;       // for float params
        juce::ComboBox combo;      // for choice params
        juce::ToggleButton toggle; // for bool params
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    struct ParamGroup
    {
        juce::String groupId;
        juce::GroupComponent group;
        std::vector<std::unique_ptr<ParamRow>> rows;
        // File-loading row (convolver / DDC groups only).
        juce::Label fileLabel;
        juce::TextButton fileButton;
        juce::Label fileStatus;
        juce::String fileKind; // APVTS state property holding the path
        bool hasFileRow = false;
        // First "*Enable" param of the group, if any: dims and disables the
        // other rows while the effect is off (the enable control itself stays
        // interactive).
        juce::String enableParamId;
        std::unique_ptr<juce::ParameterAttachment> enableAttachment;

        void setControlsEnabled(bool enabled);
    };

    void addGroup(const juce::String& groupId, std::vector<juce::String> paramIds);
    void fillCombo(juce::ComboBox& combo, const juce::String& paramId,
        const juce::StringArray& choices, const juce::String& languageCode);
    void relabelAll(const juce::String& languageCode);
    void selectLanguage();
    void pickFile(ParamGroup& group);
    void refreshFileStatus(ParamGroup& group);

    ViPER4HostAudioProcessor& processor_;
    v4h::ViperLookAndFeel lookAndFeel_;
    std::vector<std::unique_ptr<ParamGroup>> groups_;
    juce::Label titleLabel_;
    juce::Label languageLabel_;
    // ponytail: EQ APO deletes the editor on a non-message thread; a ComboBox
    // popup open at that instant crashes in DropShadower. Accepted — same risk
    // class as fileChooser_ below. If it ever bites, swap for an in-editor
    // dropdown (child components only, no popup window).
    juce::ComboBox languageBox_;
    juce::Viewport viewport_;
    juce::Component content_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViPER4HostAudioProcessorEditor)
};
