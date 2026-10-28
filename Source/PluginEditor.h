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

namespace v4h
{
// 视口内的参数控件:滚轮默认冒泡给父级(页面滚动),按住 Ctrl/Alt 时才由
// 控件处理(调参)。否则悬停在滑块/下拉框上时页面滚不动、还会误调参数。
// 与 EqCurve 的 Ctrl/Alt 调 Q 保持同一修饰键约定。
class WheelSlider : public juce::Slider
{
public:
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isCtrlDown() || e.mods.isAltDown())
            juce::Slider::mouseWheelMove(e, wheel);
        else
            Component::mouseWheelMove(e, wheel);
    }
};

class WheelCombo : public juce::ComboBox
{
public:
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (e.mods.isCtrlDown() || e.mods.isAltDown())
            juce::ComboBox::mouseWheelMove(e, wheel);
        else
            Component::mouseWheelMove(e, wheel);
    }
};
} // namespace v4h

class ViPER4HostAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    // 分类页签数量；与 .cpp 里的 pageKeys 保持同步。
    static constexpr int numPages = 6;

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
        v4h::WheelSlider slider;   // 浮点参数用(滚轮默认透传,见 v4h::WheelSlider)
        v4h::WheelCombo combo;     // 选项参数用
        juce::ToggleButton toggle; // 布尔参数用
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
    };

    struct ParamGroup
    {
        juce::String groupId;
        juce::GroupComponent group;
        // 参数名列宽：relabelAll 按本分组内最长标签实测（13px 字号），
        // 固定宽度会在短标签语言（中文）下留出大片空白。
        int labelWidth = 140;
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
        // 该分组所在的分类页签，以及折叠状态。折叠的分组只显示标题栏；
        // 启用开关就放在那条标题栏里。
        int page = 0;
        bool collapsed = false;
    };

    // 根据各分组的启用开关重新推导折叠状态（频段子分组还跟随父功能
    // 开关），然后重新布局。
    void refreshCollapse();
    // 页签青点：每页只要有分组的启用开关是开的就点亮。
    void refreshTabDots();
    // 创建分组及其参数行；返回指针以便调用者往上面挂额外的东西
    // （曲线编辑器、文件行）。参数表里第一个 "*Enable" 是分组开关，
    // 进标题栏兼任折叠控件；后续的 "*Enable" 保持普通行。
    ParamGroup* addGroup(const juce::String& groupId, std::vector<juce::String> paramIds);
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
    // EQ 频段滑杆标签按当前段数取引擎真实频点。
    void relabelEqBands();
    void selectLanguage();
    void pickFile(ParamGroup& group);
    void refreshFileStatus(ParamGroup& group);
    // 60 Hz 电平刷新：轮询处理器电平、重算频谱 FFT，
    // 并重绘可见的曲线图 / GR 表。
    void pullMetersAndSpectrum();
    // 预设与 A/B 对比。
    juce::File presetsDir() const;
    void refreshPresetList();
    void savePresetDialog();
    // 左键统一入口：每个预设一个子菜单（载入/重命名/删除）+ 另存 + 打开文件夹。
    void openPresetMenu();
    void updatePresetButtonText();
    // 均按文件而非索引操作：菜单点击与文件系统变化之间没有时序耦合。
    void loadPreset(const juce::File& file);
    void renamePresetDialog(const juce::File& file);
    void deletePresetWithConfirm(const juce::File& file);

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
    v4h::TabButton pageButtons_[numPages];
    // ponytail: EQ APO 会在非消息线程删除编辑器；那一瞬间如果 ComboBox
    // 弹出菜单还开着，DropShadower 里会崩。已接受——与下面的 fileChooser_
    // 同一风险级别。真要出事就换成编辑器内下拉（纯子控件，不开弹窗）。
    // 同样用 WheelCombo:顶栏虽不滚动,但悬停滚轮误切预设/语言是同一个毛病。
    v4h::WheelCombo languageBox_;
    juce::Viewport viewport_;
    juce::Component content_;
    std::unique_ptr<juce::FileChooser> fileChooser_;

    // 预设与 A/B 对比。A/B 槽位状态放在处理器里（随 DAW 工程一起保存）；
    // 编辑始终落在实时状态上（下次切换时也会进入当前活动槽位）。
    // 预设入口是一个下拉框样式的按钮 + 统一弹层菜单（ComboBox 的弹层
    // 不支持子菜单，且对"重复选中同一项"不触发回调）。
    v4h::PresetButton presetButton_;
    // 当前载入的预设文件（无效 = 未载入）；是菜单打勾/保存预填的依据。
    juce::File currentPresetFile_;
    juce::TextButton abButtons_[2];
    juce::TextButton abCopy_;
    juce::Array<juce::File> userPresetFiles_;
    // 频谱垫底用的多分辨率 FFT 管线（单声道输出抽头 -> 对数频点）：
    // 低频带用 8192 点长窗（5.9 Hz 分辨率，20 Hz 附近才能对齐调节点），
    // 高频带用 2048 点短窗（瞬态不糊）。频带无 bin 覆盖时按 log 频率插值。
    juce::dsp::FFT fft_{11};   // 2^11 = 2048，高频带
    juce::dsp::FFT fftLong_{13}; // 2^13 = 8192，低频带
    // JUCE 的实数变换对 N 点输入写出 2N 个浮点数。
    std::array<float, 16384> fftSamples_{}; // 长窗时域输入，原地变到频域
    std::array<float, 4096> fftShortBuf_{}; // 短窗时域输入
    std::array<float, 2048> hann_{};
    std::array<float, 8192> hannLong_{};
    // 频谱滑动窗：编辑器每帧把 FIFO 新到的样本追加到窗尾，窗口始终是
    // 最新 8192 个样本，这样长窗 FFT 每帧都能跑（频谱才有 30 fps）。
    std::array<float, 8192> window_{};
    int windowFill_ = 0;
    std::array<float, v4h::EqCurve::kSpectrumPoints> spectrumDb_{};
    // 曲线图绘制时读取的电平快照。
    float mbcGrDb_[5]{};
    float deGainDb_[10]{};
    // 空闲检测：上一帧的电平 / 频谱是否还在动（见 pullMetersAndSpectrum）。
    float fetGrPrev_ = 0.0f, limGrPrev_ = 0.0f;
    bool spectrumDecaying_ = false;
    int resizeIdleTicks_ = -1; // -1 = 尺寸已保存；窗口缩放后开始计数
    std::array<int, 2> savedSize_{660, 600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViPER4HostAudioProcessorEditor)
};
