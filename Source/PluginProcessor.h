#pragma once

#include <viper/ViPER.h>

#include <JuceHeader.h>

class ViPER4HostAudioProcessor final : public juce::AudioProcessor
{
public:
    ViPER4HostAudioProcessor();
    ~ViPER4HostAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParametersState() { return parameters_; }
    juce::String getUiLanguage() const;
    void setUiLanguage(const juce::String& languageCode);

    static constexpr const char* pluginDisplayName = "ViPER4Host";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void buildEngineParams(viper::ViPERParams& params) const;

    juce::AudioProcessorValueTreeState parameters_;
    ViPER engine_;
    viper::ViPERParams lastAppliedParams_{};
    std::vector<float> interleaved_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViPER4HostAudioProcessor)
};
