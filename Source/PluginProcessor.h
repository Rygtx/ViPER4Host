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

    // 供编辑器的曲线和 GR 表用的实时电平数据。音频线程写，消息线程轮询；
    // 普通原子变量就是标准的"尽力而为"电平约定（某帧读到撕裂/过期数据
    // 也无妨）。
    struct Meters
    {
        std::atomic<float> fetGrDb{0.0f};
        std::atomic<float> limiterGrDb{0.0f};
        std::atomic<float> mbcGrDb[5]{{0.0f}, {0.0f}, {0.0f}, {0.0f}, {0.0f}};
        std::atomic<float> deGainDb[10]{{0.0f}, {0.0f}, {0.0f}, {0.0f}, {0.0f},
            {0.0f}, {0.0f}, {0.0f}, {0.0f}, {0.0f}};
    };
    Meters meters_;
    // 喂给编辑器频谱分析器的单声道输出抽头。
    juce::AbstractFifo spectrumFifo_{16384};
    float spectrumBuffer_[16384]{};

    // 预设支持：换状态树时保住 uiLanguage 属性。
    void applyStateTree(const juce::ValueTree& newState);

    // A/B 对比：槽位快照和切换逻辑放在处理器里，随 getStateInformation
    // 一起保存——关窗口、重开工程后 A/B 都还在。仅消息线程使用。
    juce::ValueTree abSlots[2];
    int activeAbSlot = 0;
    void activateAbSlot(int slot);

    // 文件类效果。在调用方（消息）线程解析，通过一个原子槽位把结果交给
    // 音频线程；成功返回空字符串，否则返回本地化错误消息的键名后缀。
    juce::String loadConvolverFile(const juce::String& path);
    juce::String loadDdcFile(const juce::String& path);

    static constexpr const char* pluginDisplayName = "ViPER4Host";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void buildEngineParams(viper::ViPERParams& params) const;

    // 频段族参数的原始值指针，构造时抓一次。标量参数按字面量 id 查找
    // 本来就不构造 juce::String；但每块动态拼 id（"eqBand" + N 等）会
    // 在音频线程上堆分配约 190 次——实时线程的堆分配可能和宿主抢堆锁。
    // 指针随处理器存活（replaceState 只换状态树，不重建参数适配器）。
    struct DeBandPtrs { std::atomic<float> *freq, *q, *gain, *threshold, *attack, *release, *type; };
    struct MbcBandPtrs { std::atomic<float> *enable, *threshold, *ratio, *knee, *kneeAuto,
        *gain, *gainAuto, *attack, *attackAuto, *release, *releaseAuto, *kneeMulti,
        *maxAttack, *maxRelease, *crest, *adapt, *noClip; };
    std::atomic<float>* eqBandPtrs_[31]{};
    DeBandPtrs deBandPtrs_[10]{};
    MbcBandPtrs mbcBandPtrs_[5]{};
    std::atomic<float>* mbcCrossPtrs_[4]{};

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
    // 由消息线程暂存的文件数据，在音频线程上应用（引擎会改写音频线程
    // 要读的缓冲区）。C++17 没有 atomic<shared_ptr>；音频线程用 try_lock，
    // 槽位忙时跳过一个块——交接只花微秒级时间。
    std::mutex pendingMutex_;
    std::shared_ptr<PendingKernel> pendingKernel_;
    std::shared_ptr<PendingDdc> pendingDdc_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViPER4HostAudioProcessor)
};
