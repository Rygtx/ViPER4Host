#pragma once

#include "EqCurve.h"
#include "GrMeter.h"
#include "PluginProcessor.h"
#include "SiView.h"
#include "Theme.h"
#include "XoverStrip.h"

#include <JuceHeader.h>
#include <array>
#include <vector>

class ViPER4HostAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    // 分类页签数量；与 .cpp 里的 pageKeys 保持同步。
    static constexpr int numPages = 7;

    explicit ViPER4HostAudioProcessorEditor(ViPER4HostAudioProcessor&);
    ~ViPER4HostAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    struct ParamRow
    {
        juce::String paramId;
        // 分组的 "*Enable" 开关：放在分组标题栏里而不是带标签的行上
        // （开关本身就是启用控件）。
        bool isEnable = false;
        juce::Label label;
        juce::Slider slider;       // 浮点参数用
        juce::ComboBox combo;      // 选项参数用
        juce::ToggleButton toggle; // 布尔参数用
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    struct ParamGroup
    {
        juce::String groupId;
        juce::GroupComponent group;
        std::vector<std::unique_ptr<ParamRow>> rows;
        // 文件加载行（卷积器 / DDC 分组才有）。
        juce::Label fileLabel;
        juce::TextButton fileButton;
        juce::Label fileStatus;
        juce::String fileKind; // 存路径的 APVTS 状态属性
        bool hasFileRow = false;
        // 曲线编辑器（图形 EQ / 动态 EQ 分组）：作为分组子控件，
        // 布局在标题栏和参数行之间。
        std::unique_ptr<v4h::EqCurve> curve;
        // MBC 分频条和立体声声像器视图占同一个槽位。
        std::unique_ptr<v4h::XoverStrip> strip;
        std::unique_ptr<v4h::SiView> siView;
        // 横向 GR 量表（FET 压缩器 / 输出限幅器分组）。
        std::unique_ptr<v4h::GrMeter> grMeter;
        // 分组第一个 "*Enable" 参数（如有）：开关兼任折叠控件
        // （关 = 只剩标题栏），它变化时 refreshCollapse() 都会运行。
        juce::String enableParamId;
        std::unique_ptr<juce::ParameterAttachment> enableAttachment;
        // 依赖频段数的分组（EQ/DynEQ/MBC 频段）可以整体隐藏。
        bool visible = true;
        // 该分组所在的分类页签，以及折叠状态。折叠的分组只显示标题栏；
        // 启用开关就放在那条标题栏里。
        int page = 0;
        bool collapsed = false;
    };

    // 根据各分组的启用开关重新推导折叠状态（频段子分组还跟随父功能
    // 开关），然后重新布局。
    void refreshCollapse();
    // 创建分组及其参数行；返回指针以便调用者往上面挂额外的东西
    // （曲线编辑器、文件行）。titleEnable=false 时 "*Enable" 参数仍作为
    // 普通行（MBC 的选中频段面板标题栏没有开关）。
    ParamGroup* addGroup(const juce::String& groupId, std::vector<juce::String> paramIds,
        bool titleEnable = true);
    // 把一行的控件和附着（重新）绑定到一个参数。addGroup 和
    // selectDeBand/selectMbcBand 都用它——后者把详情行重新指向
    // 条/曲线选中的频段。
    void bindRow(ParamRow& row, const juce::String& id, const juce::String& languageCode,
        juce::Component& parent, bool titleEnable = true);
    // 把动态 EQ / MBC 的详情行指向频段下标 `band`（从 0 起）。
    void selectDeBand(int band);
    void selectMbcBand(int band);
    // 隐藏超出当前频段数（eqBandCount、deBandCount、mbcBandCount）的
    // 行/分组，然后重新布局。
    void updateBandVisibility();
    void fillCombo(juce::ComboBox& combo, const juce::String& paramId,
        const juce::StringArray& choices, const juce::String& languageCode);
    void relabelAll(const juce::String& languageCode);
    void selectLanguage();
    void pickFile(ParamGroup& group);
    void refreshFileStatus(ParamGroup& group);
    // 30 Hz 电平刷新：轮询处理器电平、重算频谱 FFT，
    // 并重绘可见的曲线图 / GR 表。
    void pullMetersAndSpectrum();
    // 预设与 A/B 对比。
    juce::File presetsDir() const;
    void refreshPresetList();
    void savePresetDialog();
    void activateAbSlot(int slot);

    ViPER4HostAudioProcessor& processor_;
    v4h::ViperLookAndFeel lookAndFeel_;
    std::vector<std::unique_ptr<ParamGroup>> groups_;
    // DynEQ / MBC 详情行，按绑定顺序排列——每个后缀一行。
    std::vector<ParamRow*> deBandRows_, mbcBandRows_;
    int selectedDeBand_ = 0, selectedMbcBand_ = 0;
    // 跟随 eqBandCount / deBandCount / mbcBandCount 显示或隐藏频段行。
    std::vector<std::unique_ptr<juce::ParameterAttachment>> bandCountAttachments_;
    juce::Label titleLabel_;
    juce::Label languageLabel_;
    int currentPage_ = 0;
    juce::TextButton pageButtons_[numPages];
    // ponytail: EQ APO 会在非消息线程删除编辑器；那一瞬间如果 ComboBox
    // 弹出菜单还开着，DropShadower 里会崩。已接受——与下面的 fileChooser_
    // 同一风险级别。真要出事就换成编辑器内下拉（纯子控件，不开弹窗）。
    juce::ComboBox languageBox_;
    juce::Viewport viewport_;
    juce::Component content_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    // 预设与 A/B 对比。切换时快照槽位状态；编辑始终落在实时状态上
    // （下次切换时也会进入当前活动槽位）。
    juce::ComboBox presetBox_;
    juce::TextButton abButtons_[2];
    juce::TextButton abCopy_;
    juce::ValueTree abSlots_[2];
    int activeAbSlot_ = 0;
    juce::Array<juce::File> userPresetFiles_;
    // 频谱垫底用的 FFT 管线（单声道输出抽头 -> 对数频点）。
    juce::dsp::FFT fft_{11}; // 2^11 = 2048
    // JUCE 的实数变换对 N 点输入写出 2N 个浮点数。
    std::array<float, 4096> fftSamples_{};
    std::array<float, 2048> hann_{};
    std::array<float, v4h::EqCurve::kSpectrumPoints> spectrumDb_{};
    // 曲线图绘制时读取的电平快照。
    float mbcGrDb_[5]{};
    float deGainDb_[10]{};
    int resizeIdleTicks_ = -1; // -1 = 尺寸已保存；窗口缩放后开始计数
    std::array<int, 2> savedSize_{660, 600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViPER4HostAudioProcessorEditor)
};
