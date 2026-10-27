#pragma once

#include <viper/ViPER.h>

#include <JuceHeader.h>
#include <mutex>

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

    // Live metering for the editor's curves and GR meters. Audio thread
    // writes, message thread polls; plain atomics are the standard
    // best-effort metering contract (a torn/stale frame is harmless).
    struct Meters
    {
        std::atomic<float> fetGrDb{0.0f};
        std::atomic<float> limiterGrDb{0.0f};
        std::atomic<float> mbcGrDb[5]{{0.0f}, {0.0f}, {0.0f}, {0.0f}, {0.0f}};
        std::atomic<float> deGainDb[10]{{0.0f}, {0.0f}, {0.0f}, {0.0f}, {0.0f},
            {0.0f}, {0.0f}, {0.0f}, {0.0f}, {0.0f}};
    };
    Meters meters_;
    // Mono output tap feeding the editor's spectrum analyzer.
    juce::AbstractFifo spectrumFifo_{16384};
    float spectrumBuffer_[16384]{};

    // Preset support: state swap that keeps the uiLanguage property alive.
    void applyStateTree(const juce::ValueTree& newState);

    // File-based effects. These parse on the calling (message) thread and hand
    // the result to the audio thread via an atomic slot; returns an empty
    // string on success, else a localised error message key suffix.
    juce::String loadConvolverFile(const juce::String& path);
    juce::String loadDdcFile(const juce::String& path);

    static constexpr const char* pluginDisplayName = "ViPER4Host";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void buildEngineParams(viper::ViPERParams& params) const;

    struct PendingKernel
    {
        std::vector<float> samples;
        uint32_t frames = 0, channels = 0;
    };
    struct PendingDdc
    {
        std::vector<viper::BiquadSection> sections44100, sections48000;
    };

    juce::AudioProcessorValueTreeState parameters_;
    ViPER engine_;
    viper::ViPERParams lastAppliedParams_{};
    std::vector<float> interleaved_;
    juce::AudioFormatManager formatManager_;
    // File payloads staged by the message thread, applied on the audio thread
    // (the engine mutates buffers the audio thread reads). C++17 has no
    // atomic<shared_ptr>; the audio thread try_locks and skips a block if the
    // slot is busy for the microsecond the handoff takes.
    std::mutex pendingMutex_;
    std::shared_ptr<PendingKernel> pendingKernel_;
    std::shared_ptr<PendingDdc> pendingDdc_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViPER4HostAudioProcessor)
};
