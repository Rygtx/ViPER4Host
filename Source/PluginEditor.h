#pragma once

#include "PluginProcessor.h"

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
        juce::Slider slider;       // for float / choice params
        juce::ToggleButton toggle; // for bool params
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    struct ParamGroup
    {
        juce::String groupId;
        juce::GroupComponent group;
        std::vector<std::unique_ptr<ParamRow>> rows;
    };

    void addGroup(const juce::String& groupId, std::initializer_list<const char*> paramIds);
    void relabelAll(const juce::String& languageCode);

    ViPER4HostAudioProcessor& processor_;
    std::vector<std::unique_ptr<ParamGroup>> groups_;
    juce::Label titleLabel_;
    juce::Label languageLabel_;
    juce::ComboBox languageBox_;
    juce::Viewport viewport_;
    juce::Component content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViPER4HostAudioProcessorEditor)
};
