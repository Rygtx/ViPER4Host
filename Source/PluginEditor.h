#pragma once

#include "PluginProcessor.h"
#include "Theme.h"

#include <JuceHeader.h>
#include <vector>

class ViPER4HostAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    // Category tab count; keep in sync with pageKeys in the .cpp.
    static constexpr int numPages = 6;

    explicit ViPER4HostAudioProcessorEditor(ViPER4HostAudioProcessor&);
    ~ViPER4HostAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct ParamRow
    {
        juce::String paramId;
        // Group's "*Enable" switch: lives in the group title bar instead of a
        // labelled row (the switch itself is the enable control).
        bool isEnable = false;
        // Band-count rule (EQ/DEQ/MBC); actual visibility is applied in
        // resized() as bandVisible && !group collapsed.
        bool bandVisible = true;
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
        // First "*Enable" param of the group, if any: the switch doubles as
        // the fold control (off = title bar only) and refreshCollapse() runs
        // whenever it changes.
        juce::String enableParamId;
        std::unique_ptr<juce::ParameterAttachment> enableAttachment;
        // Band-count dependent groups (EQ/DynEQ/MBC bands) can be fully hidden.
        bool visible = true;
        // Category tab this group lives on, plus its fold state. A folded
        // group shows only its title bar; the enable switch sits in that bar.
        int page = 0;
        bool collapsed = false;
    };

    // Re-derives every group's collapsed state from its enable switch (band
    // sub-groups also follow their parent feature switch), then relayouts.
    void refreshCollapse();
    void addGroup(const juce::String& groupId, std::vector<juce::String> paramIds);
    // Hides rows/groups beyond the active band counts (eqBandCount, deBandCount,
    // mbcBandCount), then relayouts.
    void updateBandVisibility();
    void fillCombo(juce::ComboBox& combo, const juce::String& paramId,
        const juce::StringArray& choices, const juce::String& languageCode);
    void relabelAll(const juce::String& languageCode);
    void selectLanguage();
    void pickFile(ParamGroup& group);
    void refreshFileStatus(ParamGroup& group);

    ViPER4HostAudioProcessor& processor_;
    v4h::ViperLookAndFeel lookAndFeel_;
    std::vector<std::unique_ptr<ParamGroup>> groups_;
    // Follow eqBandCount / deBandCount / mbcBandCount to show/hide band rows.
    std::vector<std::unique_ptr<juce::ParameterAttachment>> bandCountAttachments_;
    juce::Label titleLabel_;
    juce::Label languageLabel_;
    int currentPage_ = 0;
    juce::TextButton pageButtons_[numPages];
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
