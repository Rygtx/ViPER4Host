#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Localisation.h"

#include <cmath>

ViPER4HostAudioProcessor::ViPER4HostAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Input", juce::AudioChannelSet::stereo(), true))
    , parameters_(*this, nullptr, "PARAMS", createParameterLayout())
{
    formatManager_.registerBasicFormats();
    // 频段族参数指针缓存（见头文件说明）；APVTS 已在初始化列表里建好。
    for (int i = 0; i < 31; ++i)
        eqBandPtrs_[i] = parameters_.getRawParameterValue("eqBand" + juce::String(i + 1));
    for (int b = 0; b < 10; ++b)
    {
        const auto pfx = "de" + juce::String(b + 1);
        auto& d = deBandPtrs_[b];
        d.freq = parameters_.getRawParameterValue(pfx + "Freq");
        d.q = parameters_.getRawParameterValue(pfx + "Q");
        d.gain = parameters_.getRawParameterValue(pfx + "Gain");
        d.threshold = parameters_.getRawParameterValue(pfx + "Threshold");
        d.attack = parameters_.getRawParameterValue(pfx + "Attack");
        d.release = parameters_.getRawParameterValue(pfx + "Release");
        d.type = parameters_.getRawParameterValue(pfx + "Type");
    }
    for (int c = 0; c < 4; ++c)
        mbcCrossPtrs_[c] = parameters_.getRawParameterValue("mbcCross" + juce::String(c + 1));
    bypassPtr_ = parameters_.getRawParameterValue("bypass");
    for (int b = 0; b < 5; ++b)
    {
        const auto pfx = "mbc" + juce::String(b + 1);
        auto& m = mbcBandPtrs_[b];
        m.enable = parameters_.getRawParameterValue(pfx + "Enable");
        m.threshold = parameters_.getRawParameterValue(pfx + "Threshold");
        m.ratio = parameters_.getRawParameterValue(pfx + "Ratio");
        m.knee = parameters_.getRawParameterValue(pfx + "Knee");
        m.kneeAuto = parameters_.getRawParameterValue(pfx + "KneeAuto");
        m.gain = parameters_.getRawParameterValue(pfx + "Gain");
        m.gainAuto = parameters_.getRawParameterValue(pfx + "GainAuto");
        m.attack = parameters_.getRawParameterValue(pfx + "Attack");
        m.attackAuto = parameters_.getRawParameterValue(pfx + "AttackAuto");
        m.release = parameters_.getRawParameterValue(pfx + "Release");
        m.releaseAuto = parameters_.getRawParameterValue(pfx + "ReleaseAuto");
        m.kneeMulti = parameters_.getRawParameterValue(pfx + "KneeMulti");
        m.maxAttack = parameters_.getRawParameterValue(pfx + "MaxAttack");
        m.maxRelease = parameters_.getRawParameterValue(pfx + "MaxRelease");
        m.crest = parameters_.getRawParameterValue(pfx + "Crest");
        m.adapt = parameters_.getRawParameterValue(pfx + "Adapt");
        m.noClip = parameters_.getRawParameterValue(pfx + "NoClip");
    }
    parameters_.state.setProperty("uiLanguage", v4h::localisation::systemLanguage(), nullptr);
    // A/B 两个槽位初始都等于当前状态。
    abSlots[0] = abSlots[1] = parameters_.copyState();
    // 全局 LocalisedStrings 表会泄漏进宿主自己的 JUCE 文案，
    // 所以只在独立版里安装。
    if (wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        v4h::localisation::applyJuceTranslations(getUiLanguage());

#if JUCE_DEBUG
    // 本文件负责的解析逻辑自检；布局写错也能正常编译，只是听起来不对，
    // 所以在启动时钉死（仅调试构建）。
    {
        std::vector<viper::BiquadSection> lo, hi;
        // 布局：每节 5 个浮点数，前半 44.1 kHz，后半 48 kHz。
        const bool ok = parseDdcText(
            "0 1 2 3 4 5 6 7 8 9 20 21 22 23 24 25 26 27 28 29", lo, hi);
        jassert(ok && lo.size() == 2 && hi.size() == 2
            && lo[0][0] == 0.0f && lo[1][0] == 5.0f
            && hi[0][0] == 20.0f && hi[1][0] == 25.0f);
        jassert(!parseDdcText("1 2 3 4 5", lo, hi)); // 数据太短
    }

    // 状态保存/恢复自检：A/B 槽位必须随状态一起走完一轮。
    {
        activeAbSlot = 1;
        juce::MemoryBlock block;
        getStateInformation(block);
        activeAbSlot = 0;
        setStateInformation(block.getData(), static_cast<int> (block.getSize()));
        jassert(activeAbSlot == 1 && abSlots[0].isValid() && abSlots[1].isValid());
        activeAbSlot = 0;
    }
#endif
}

juce::String ViPER4HostAudioProcessor::getUiLanguage() const
{
    return parameters_.state.getProperty("uiLanguage", "en").toString();
}

void ViPER4HostAudioProcessor::setUiLanguage(const juce::String& languageCode)
{
    parameters_.state.setProperty("uiLanguage", languageCode, nullptr);
    if (wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        v4h::localisation::applyJuceTranslations(languageCode);
}

ViPER4HostAudioProcessor::~ViPER4HostAudioProcessor() = default;

namespace
{
// FET/MBC 压缩器的阈值和拐点存放在自然对数域：raw = dB * ln(10)/20。
// 比率以带符号的斜率 -1/R 存储。
constexpr float kDbToLog = 0.1151293f;
constexpr float dbToRaw(float db) { return db * kDbToLog; }
constexpr float ratioToRaw(float ratio) { return -1.0f / ratio; }
static_assert(dbToRaw(0.0f) == 0.0f && dbToRaw(-6.0f) < -0.69f && dbToRaw(-6.0f) > -0.70f);
static_assert(ratioToRaw(2.0f) == -0.5f);

// 每个子结构体都有 operator==；逐一比较所有块，防止未接线的参数
// 悄悄跳过重新应用。放在宿主侧，让 ViPERDSP 子模块保持在它的
// 上游提交不动。
bool paramsDiffer(const viper::ViPERParams& a, const viper::ViPERParams& b)
{
    return !(a.master_limiter == b.master_limiter
        && a.playback_gain_control == b.playback_gain_control
        && a.lufs == b.lufs
        && a.fet_compressor == b.fet_compressor
        && a.bass == b.bass
        && a.bass_mono == b.bass_mono
        && a.psychoacoustic_bass == b.psychoacoustic_bass
        && a.spectrum_extension == b.spectrum_extension
        && a.equalizer == b.equalizer
        && a.convolver == b.convolver
        && a.ddc == b.ddc
        && a.field_surround == b.field_surround
        && a.diff_surround == b.diff_surround
        && a.stereo_imager == b.stereo_imager
        && a.headphone_surround == b.headphone_surround
        && a.reverb == b.reverb
        && a.dynamic_system == b.dynamic_system
        && a.clarity == b.clarity
        && a.cure == b.cure
        && a.tube_simulator == b.tube_simulator
        && a.analog_x == b.analog_x
        && a.speaker_correction == b.speaker_correction
        && a.multiband_compressor == b.multiband_compressor
        && a.dynamic_eq == b.dynamic_eq);
}

// ponytail: 假定是 ViPER4Windows 的 .ddc 文本布局——空白分隔的双二阶
// 分节，每节 5 个浮点数，前半 44.1 kHz，后半 48 kHz。真文件不一致时
// 再加一个识别文件头的解析器。
bool parseDdcText(const juce::String& text,
    std::vector<viper::BiquadSection>& sections44100,
    std::vector<viper::BiquadSection>& sections48000)
{
    juce::StringArray tokens;
    tokens.addTokens(text, " \r\n\t,", "");
    std::vector<float> values;
    values.reserve(static_cast<size_t>(tokens.size()));
    for (const auto& t : tokens)
        if (t.trim().isNotEmpty())
            values.push_back(t.getFloatValue());

    if (values.size() < 10 || values.size() % 10 != 0)
        return false;

    const size_t half = values.size() / 2;
    const size_t sections = half / 5;
    sections44100.clear();
    sections48000.clear();
    for (size_t s = 0; s < sections; ++s)
    {
        viper::BiquadSection lo{}, hi{};
        for (int k = 0; k < 5; ++k)
        {
            lo[static_cast<size_t>(k)] = values[s * 5 + static_cast<size_t>(k)];
            hi[static_cast<size_t>(k)] = values[half + s * 5 + static_cast<size_t>(k)];
        }
        sections44100.push_back(lo);
        sections48000.push_back(hi);
    }
    return true;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout
ViPER4HostAudioProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using BoolParameter = juce::AudioParameterBool;
    using ChoiceParameter = juce::AudioParameterChoice;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // 默认值对照 ViPER4Windows（F:\Project\ViPER4Windows，
    // ViPER4Windows/lib/models/viper_state.dart）换算成宿主单位。
    // 例外：FET/MBC 的 Adapt 保持 4 s——V4W 引擎副本里存的是原始对数
    // 系数，换算不成秒。

    auto addBool = [&layout](const char* id, const char* name, bool def = false) {
        layout.add(std::make_unique<BoolParameter>(id, name, def));
    };
    auto addFloat = [&layout](const char* id, const char* name, float min, float max, float def,
                          const char* suffix = "") {
        // ponytail: 按数值范围定小数位（范围宽的都是 Hz/整数）；哪个参数
        // 需要特殊格式，再单独传 stringFromValue。
        const int decimals = max <= 10.0f ? 2 : 0;
        const juce::String label = suffix;
        layout.add(std::make_unique<Parameter>(
            id, name, juce::NormalisableRange<float>(min, max), def,
            juce::AudioParameterFloatAttributes()
                .withLabel(label)
                // JUCE 8 的 getText() 不会追加标签后缀，所以在这里补上。
                .withStringFromValueFunction([decimals, label](float v, int) {
                    // String(v, 0) 不是"0 位小数"——它会退回 %g（6 位有效
                    // 数字，见 juce_String.cpp writeDouble），所以整数参数
                    // 要显式四舍五入。
                    auto text = decimals > 0 ? juce::String(v, decimals)
                                             : juce::String(juce::roundToInt(v));
                    while (text.containsChar('.') // 1.00 -> 1，0.50 -> 0.5
                        && (text.endsWithChar('0') || text.endsWithChar('.')))
                        text = text.dropLastCharacters(1);
                    return label.isNotEmpty() ? text + " " + label : text;
                })));
    };
    auto addChoice = [&layout](const char* id, const char* name, const juce::StringArray& choices, int def = 0) {
        layout.add(std::make_unique<ChoiceParameter>(id, name, choices, def));
    };

    // 主限幅器 / 输出
    addFloat("limiterThreshold", "Limiter Threshold", 0.3f, 1.0f, 1.0f);
    addFloat("limiterVolume", "Output Volume", 0.01f, 2.0f, 1.0f, "x");
    addFloat("limiterPan", "Channel Pan", -1.0f, 1.0f, 0.0f);

    // 播放增益控制（AGC）
    addBool("pgcEnable", "AGC Enable");
    addFloat("pgcStrength", "AGC Strength", 0.5f, 3.0f, 1.0f, "x");
    addFloat("pgcMaxGain", "AGC Max Gain", 1.0f, 10.0f, 1.0f, "x");
    addFloat("pgcOutputThreshold", "AGC Target Level", 0.3f, 1.0f, 1.0f);

    // LUFS 响度目标
    addBool("lufsEnable", "LUFS Enable");
    addFloat("lufsTarget", "LUFS Target", -24.0f, -8.0f, -14.0f, " LUFS");
    addFloat("lufsMaxGain", "LUFS Max Gain", 0.0f, 12.0f, 6.0f, " dB");
    addChoice("lufsSpeed", "LUFS Speed", { "1", "2", "3" }, 1);

    // FET 压缩器
    addBool("fetEnable", "FET Enable");
    addFloat("fetThreshold", "FET Threshold", -48.0f, 0.0f, -18.0f, " dB");
    addFloat("fetRatio", "FET Ratio", 0.5f, 20.0f, 1.0f, " :1");
    addFloat("fetKnee", "FET Knee", 0.0f, 12.0f, 0.0f, " dB");
    addBool("fetKneeAuto", "FET Auto Knee", true);
    addFloat("fetGain", "FET Makeup Gain", 0.0f, 24.0f, 0.0f, " dB");
    addBool("fetGainAuto", "FET Auto Gain", true);
    addFloat("fetAttack", "FET Attack", 1.0f, 100.0f, 20.0f, " ms");
    addBool("fetAttackAuto", "FET Auto Attack", true);
    addFloat("fetRelease", "FET Release", 5.0f, 500.0f, 50.0f, " ms");
    addBool("fetReleaseAuto", "FET Auto Release", true);
    addFloat("fetKneeMulti", "FET Knee Multi", 0.0f, 4.0f, 0.0f);
    addFloat("fetMaxAttack", "FET Max Attack", 1.0f, 100.0f, 80.0f, " ms");
    addFloat("fetMaxRelease", "FET Max Release", 5.0f, 500.0f, 200.0f, " ms");
    addFloat("fetCrest", "FET Crest Time", 5.0f, 300.0f, 100.0f, " ms");
    addFloat("fetAdapt", "FET Adapt Time", 1.0f, 16.0f, 4.0f, " s");
    addBool("fetNoClip", "FET No Clip", true);

    // 图形均衡器
    addBool("eqEnable", "EQ Enable");
    addChoice("eqBandCount", "EQ Bands", { "10", "15", "25", "31" }, 0);
    for (int i = 1; i <= 31; ++i)
    {
        auto id = "eqBand" + juce::String(i);
        addFloat(id.toRawUTF8(), id.toRawUTF8(), -12.0f, 12.0f, 0.0f, " dB");
    }

    // 动态均衡器
    addBool("deEnable", "DynEQ Enable");
    addChoice("deBandCount", "DynEQ Bands", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" }, 2);
    // 前三段是手调默认值，其余按约 2.5 倍对数间隔铺开，避免新增频段的点堆在同一频率。
    constexpr float deDefaultFreqs[] = { 60, 150, 400, 1000, 2500, 5000, 8000, 11000, 14000, 17000 };
    for (int b = 1; b <= 10; ++b)
    {
        const auto pfx = "de" + juce::String(b);
        addFloat((pfx + "Freq").toRawUTF8(), (pfx + "Freq").toRawUTF8(), 20.0f, 20000.0f, deDefaultFreqs[b - 1], " Hz");
        addFloat((pfx + "Q").toRawUTF8(), (pfx + "Q").toRawUTF8(), 0.5f, 8.0f, b == 3 ? 1.5f : 1.0f);
        addFloat((pfx + "Gain").toRawUTF8(), (pfx + "Gain").toRawUTF8(), -12.0f, 12.0f, 0.0f, " dB");
        addFloat((pfx + "Threshold").toRawUTF8(), (pfx + "Threshold").toRawUTF8(), -80.0f, 0.0f, -20.0f, " dB");
        addFloat((pfx + "Attack").toRawUTF8(), (pfx + "Attack").toRawUTF8(), 1.0f, 100.0f, 10.0f, " ms");
        addFloat((pfx + "Release").toRawUTF8(), (pfx + "Release").toRawUTF8(), 10.0f, 500.0f, 100.0f, " ms");
        addChoice((pfx + "Type").toRawUTF8(), (pfx + "Type").toRawUTF8(), { "Peak", "Low Shelf", "High Shelf" });
    }

    // 心理声学低音
    addBool("pBassEnable", "Psychoacoustic Bass Enable");
    addFloat("pBassCutoff", "PB Cutoff", 60.0f, 150.0f, 80.0f, " Hz");
    addFloat("pBassIntensity", "PB Intensity", 0.0f, 1.0f, 0.5f);
    addChoice("pBassHarmonicOrder", "PB Harmonic Order", { "2", "3", "4", "5" }, 1);
    addFloat("pBassOriginalLevel", "PB Original Level", 0.0f, 1.0f, 1.0f);

    // ViPER 低音
    addBool("bassEnable", "Bass Enable");
    addChoice("bassMode", "Bass Mode", { "Natural", "Pure Bass+", "Subwoofer" });
    addFloat("bassFreq", "Bass Cutoff", 15.0f, 150.0f, 70.0f, " Hz");
    addFloat("bassGain", "Bass Gain", 0.5f, 10.0f, 0.5f, "x");
    addBool("bassAntiPop", "Bass Anti-Pop");

    // 低音单声道
    addBool("bassMonoEnable", "Bass Mono Enable");
    addChoice("bassMonoMode", "Bass Mono Mode", { "Natural", "Pure Bass+", "Subwoofer" });
    addFloat("bassMonoFreq", "Bass Mono Cutoff", 15.0f, 150.0f, 70.0f, " Hz");
    addFloat("bassMonoGain", "Bass Mono Gain", 0.5f, 10.0f, 0.5f, "x");
    addBool("bassMonoAntiPop", "Bass Mono Anti-Pop");

    // 频谱延展
    addBool("specExtEnable", "Spectrum Extension Enable");
    addFloat("specExtStrength", "SE Reference Freq", 2200.0f, 8200.0f, 7600.0f, " Hz");
    addFloat("specExtExciter", "SE Exciter", 0.0f, 6.0f, 0.0f);

    // 卷积器
    addBool("convEnable", "Convolver Enable");
    addFloat("convCrossChannel", "Convolver Cross Channel", 0.0f, 1.0f, 0.0f);

    // DDC
    addBool("ddcEnable", "DDC Enable");

    // 声场环绕
    addBool("fieldEnable", "Field Surround Enable");
    addFloat("fieldWidening", "Field Widening", 0.0f, 8.0f, 0.0f);
    addFloat("fieldMidImage", "Field Mid Image", 1.0f, 2.0f, 1.5f);
    addFloat("fieldDepth", "Field Depth", 200.0f, 950.0f, 200.0f);

    // 差分环绕
    addBool("diffEnable", "Diff Surround Enable");
    addFloat("diffDelay", "Diff Delay", 1.0f, 20.0f, 5.0f, " ms");
    addBool("diffReverse", "Diff Reverse");
    addFloat("diffMix", "Diff Wet/Dry Mix", 0.0f, 1.0f, 1.0f);
    addFloat("diffLpCutoff", "Diff LP Cutoff", 0.0f, 20000.0f, 0.0f, " Hz");

    // 立体声声像器
    addBool("siEnable", "Stereo Imager Enable");
    addFloat("siLowWidth", "SI Low Width", 0.0f, 2.0f, 1.0f);
    addFloat("siMidWidth", "SI Mid Width", 0.0f, 2.0f, 1.0f);
    addFloat("siHighWidth", "SI High Width", 0.0f, 2.0f, 1.0f);
    addFloat("siLowCrossover", "SI Low Crossover", 80.0f, 400.0f, 200.0f, " Hz");
    addFloat("siHighCrossover", "SI High Crossover", 2000.0f, 8000.0f, 4000.0f, " Hz");

    // 耳机环绕+（VHE）
    addBool("vheEnable", "VHE Enable");
    addChoice("vheQuality", "VHE Level", { "Level 1", "Level 2", "Level 3", "Level 4", "Level 5" });

    // 混响
    addBool("reverbEnable", "Reverb Enable");
    addFloat("reverbRoomSize", "Reverb Room Size", 0.0f, 1.0f, 0.0f);
    addFloat("reverbWidth", "Reverb Width", 0.0f, 1.0f, 0.0f);
    addFloat("reverbDamp", "Reverb Damp", 0.0f, 1.0f, 0.0f);
    addFloat("reverbWet", "Reverb Wet", 0.0f, 1.0f, 0.0f);
    addFloat("reverbDry", "Reverb Dry", 0.0f, 1.0f, 1.0f);

    // 动态系统
    addBool("dsEnable", "Dynamic System Enable");
    addFloat("dsXLow", "DS X Low", 0.0f, 2400.0f, 100.0f, " Hz");
    addFloat("dsXHigh", "DS X High", 0.0f, 12000.0f, 5600.0f, " Hz");
    addFloat("dsYLow", "DS Y Low", 0.0f, 200.0f, 40.0f, " Hz");
    addFloat("dsYHigh", "DS Y High", 0.0f, 300.0f, 80.0f, " Hz");
    addFloat("dsSideGainLow", "DS Side Gain Low", 0.0f, 1.0f, 0.5f);
    addFloat("dsSideGainHigh", "DS Side Gain High", 0.0f, 1.0f, 0.5f);
    addFloat("dsStrength", "DS Strength", 0.0f, 1.0f, 0.5f);

    // ViPER Clarity
    addBool("clarityEnable", "Clarity Enable");
    addChoice("clarityMode", "Clarity Mode", { "Natural", "OZone", "XHiFi" });
    addFloat("clarityGain", "Clarity Gain", 0.0f, 4.5f, 0.5f, "x");

    // Cure 串音补偿
    addBool("cureEnable", "Cure Enable");
    addChoice("curePreset", "Cure Preset", { "Preset 1", "Preset 2", "Preset 3" });

    // 电子管模拟器
    addBool("tubeEnable", "Tube Enable");
    addChoice("tubeModel", "Tube Model", { "12AX7", "12AU7", "6N1P" });
    addFloat("tubeDrive", "Tube Drive", 0.0f, 1.0f, 0.5f);
    addFloat("tubeMix", "Tube Mix", 0.0f, 1.0f, 0.3f);

    // AnalogX
    addBool("axEnable", "AnalogX Enable");
    addChoice("axMode", "AnalogX Mode", { "Light", "Medium", "Heavy" });

    // 扬声器修正
    addBool("spkEnable", "Speaker Correction Enable");

    // 多段压缩器
    addBool("mbcEnable", "MBC Enable");
    // 引擎只支持 3/5 段，档位与之对应（0 = 3 段，1 = 5 段）。
    addChoice("mbcBandCount", "MBC Bands", { "3", "5" }, 1);
    constexpr float mbcDefaultCrossovers[] = { 120.0f, 500.0f, 4000.0f, 8000.0f };
    for (int c = 1; c <= 4; ++c)
    {
        auto id = "mbcCross" + juce::String(c);
        addFloat(id.toRawUTF8(), id.toRawUTF8(), 20.0f, 20000.0f, mbcDefaultCrossovers[c - 1], " Hz");
    }
    for (int b = 1; b <= 5; ++b)
    {
        const auto pfx = "mbc" + juce::String(b);
        addBool((pfx + "Enable").toRawUTF8(), (pfx + "Enable").toRawUTF8(), true);
        addFloat((pfx + "Threshold").toRawUTF8(), (pfx + "Threshold").toRawUTF8(), -48.0f, 0.0f, -18.0f, " dB");
        addFloat((pfx + "Ratio").toRawUTF8(), (pfx + "Ratio").toRawUTF8(), 0.5f, 20.0f, 2.0f, " :1");
        addFloat((pfx + "Knee").toRawUTF8(), (pfx + "Knee").toRawUTF8(), 0.0f, 12.0f, 0.0f, " dB");
        addBool((pfx + "KneeAuto").toRawUTF8(), (pfx + "KneeAuto").toRawUTF8(), true);
        addFloat((pfx + "Gain").toRawUTF8(), (pfx + "Gain").toRawUTF8(), 0.0f, 24.0f, 0.0f, " dB");
        addBool((pfx + "GainAuto").toRawUTF8(), (pfx + "GainAuto").toRawUTF8(), true);
        addFloat((pfx + "Attack").toRawUTF8(), (pfx + "Attack").toRawUTF8(), 1.0f, 100.0f, 1.0f, " ms");
        addBool((pfx + "AttackAuto").toRawUTF8(), (pfx + "AttackAuto").toRawUTF8(), true);
        addFloat((pfx + "Release").toRawUTF8(), (pfx + "Release").toRawUTF8(), 5.0f, 500.0f, 100.0f, " ms");
        addBool((pfx + "ReleaseAuto").toRawUTF8(), (pfx + "ReleaseAuto").toRawUTF8(), true);
        addFloat((pfx + "KneeMulti").toRawUTF8(), (pfx + "KneeMulti").toRawUTF8(), 0.0f, 4.0f, 0.0f);
        addFloat((pfx + "MaxAttack").toRawUTF8(), (pfx + "MaxAttack").toRawUTF8(), 1.0f, 100.0f, 44.0f, " ms");
        addFloat((pfx + "MaxRelease").toRawUTF8(), (pfx + "MaxRelease").toRawUTF8(), 5.0f, 500.0f, 200.0f, " ms");
        addFloat((pfx + "Crest").toRawUTF8(), (pfx + "Crest").toRawUTF8(), 5.0f, 300.0f, 100.0f, " ms");
        addFloat((pfx + "Adapt").toRawUTF8(), (pfx + "Adapt").toRawUTF8(), 1.0f, 16.0f, 4.0f, " s");
        addBool((pfx + "NoClip").toRawUTF8(), (pfx + "NoClip").toRawUTF8(), true);
    }

    addBool("bypass", "Bypass");

    return layout;
}

void ViPER4HostAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_.SetSamplingRate(static_cast<uint32_t>(sampleRate + 0.5));
    engine_.RequestBuffersReset();
    interleaved_.resize(static_cast<size_t>(samplesPerBlock) * 2);
}

void ViPER4HostAudioProcessor::releaseResources() {}

bool ViPER4HostAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    const auto& in = layouts.getMainInputChannelSet();
    if (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
        return in == out || in.isDisabled();
    return false;
}

void ViPER4HostAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numFrames = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numFrames <= 0)
        return;

    // ViPERDSP 是 2 声道交错格式；把所有声道折叠成立体声。
    const float* left = numChannels > 0 ? buffer.getReadPointer(0) : nullptr;
    const float* right = numChannels > 1 ? buffer.getReadPointer(1) : left;
    if (static_cast<int>(interleaved_.size()) < numFrames * 2)
        interleaved_.resize(static_cast<size_t>(numFrames) * 2);
    for (int i = 0; i < numFrames; ++i)
    {
        interleaved_[static_cast<size_t>(i) * 2] = left != nullptr ? left[i] : 0.0f;
        interleaved_[static_cast<size_t>(i) * 2 + 1] = right != nullptr ? right[i] : 0.0f;
    }

    // 文件数据由消息线程暂存，在这里换入：引擎会改写音频线程要读的
    // 缓冲区，所以加载本身必须在这个线程上跑。
    if (pendingMutex_.try_lock())
    {
        if (auto kernel = std::move(pendingKernel_))
            engine_.LoadConvolverKernel(kernel->samples.data(), kernel->frames, kernel->channels, 1);
        if (pendingKernelUnload_)
        {
            engine_.UnloadConvolverKernel();
            pendingKernelUnload_ = false;
        }
        if (auto ddc = std::move(pendingDdc_))
            engine_.LoadDdcCoefficients(ddc->sections44100.data(), ddc->sections48000.data(),
                static_cast<uint32_t>(ddc->sections44100.size()));
        pendingMutex_.unlock();
    }

    const bool bypassed = bypassPtr_->load(std::memory_order_relaxed) >= 0.5f;
    if (!bypassed)
    {
        viper::ViPERParams params{};
        buildEngineParams(params);
        if (paramsDiffer(params, lastAppliedParams_))
        {
            engine_.ApplyParams(params);
            lastAppliedParams_ = params;
        }

        // ponytail: 卷积器/VHE 会引入引擎内部延迟，但引擎没暴露延迟查询
        // 接口，所以我们报告零延迟。引擎加了 GetLatency API 时再回来改。
        engine_.Process(interleaved_, static_cast<uint32_t>(numFrames));
    }

    // 电平统计 + 供编辑器用的单声道频谱抽头。尽力而为：UI 按定时器
    // 轮询这些值，读到过期或撕裂的一帧也无妨。
    if (bypassed) // 旁路时编辑器上冻结的电平表会被误读成"还在工作"；
                  // 引擎也没跑，getter 只有过期值，直接清零。
    {
        meters_.fetGrDb.store(0.0f);
        meters_.limiterGrDb.store(0.0f);
        for (auto& g : meters_.mbcGrDb)
            g.store(0.0f);
        for (auto& g : meters_.deGainDb)
            g.store(0.0f);
    }
    else
    {
        constexpr float kRawToDb = 1.0f / 0.1151293f; // 引擎对数域 -> dB
        meters_.fetGrDb.store(-engine_.GetFetGainReduction() * kRawToDb);
        // 负 dB = 衰减，与 fetGrDb 同一约定（GrMeter 内部取负）。
        meters_.limiterGrDb.store(
            engine_.GetLimiterGain() > 0.0f ? 20.0f * std::log10(engine_.GetLimiterGain()) : 0.0f);
        for (int b = 0; b < 5; ++b)
            meters_.mbcGrDb[static_cast<size_t>(b)].store(
                -engine_.GetMultibandBandGainReduction(b) * kRawToDb);
        for (int b = 0; b < 10; ++b)
            meters_.deGainDb[static_cast<size_t>(b)].store(
                engine_.GetDynamicEqBandAppliedGainDb(b));
    }

    int start1, size1, start2, size2;
    spectrumFifo_.prepareToWrite(numFrames, start1, size1, start2, size2);
    for (int i = 0; i < size1; ++i)
        spectrumBuffer_[static_cast<size_t>(start1 + i)]
            = (interleaved_[static_cast<size_t>(i) * 2]
                  + interleaved_[static_cast<size_t>(i) * 2 + 1]) * 0.5f;
    for (int i = 0; i < size2; ++i)
        spectrumBuffer_[static_cast<size_t>(start2 + i)]
            = (interleaved_[static_cast<size_t>(size1 + i) * 2]
                  + interleaved_[static_cast<size_t>(size1 + i) * 2 + 1]) * 0.5f;
    // 忘了推进写指针的话 FIFO 会永远为空,频谱分支永远不触发。
    spectrumFifo_.finishedWrite(size1 + size2);

    float* outLeft = numChannels > 0 ? buffer.getWritePointer(0) : nullptr;
    float* outRight = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    for (int i = 0; i < numFrames; ++i)
    {
        if (outLeft) outLeft[i] = interleaved_[static_cast<size_t>(i) * 2];
        if (outRight) outRight[i] = interleaved_[static_cast<size_t>(i) * 2 + 1];
    }
}

void ViPER4HostAudioProcessor::buildEngineParams(viper::ViPERParams& p) const
{
    // 字面量 id 的查找是 std::map 树遍历但不堆分配；频段族走缓存指针
    // （见头文件说明），每块零分配。
    auto v = [this](const char* id) {
        return parameters_.getRawParameterValue(id)->load(std::memory_order_relaxed);
    };

    p.master_limiter.threshold = v("limiterThreshold");
    p.master_limiter.output_volume = v("limiterVolume");
    p.master_limiter.channel_pan = v("limiterPan");

    p.playback_gain_control.enable = v("pgcEnable") > 0.5f;
    p.playback_gain_control.strength = v("pgcStrength");
    p.playback_gain_control.max_gain = v("pgcMaxGain");
    p.playback_gain_control.output_threshold = v("pgcOutputThreshold");

    p.lufs.enable = v("lufsEnable") > 0.5f;
    p.lufs.target = v("lufsTarget");
    p.lufs.max_gain = v("lufsMaxGain");
    p.lufs.speed = static_cast<int>(v("lufsSpeed"));

    p.fet_compressor.enable = v("fetEnable") > 0.5f;
    p.fet_compressor.threshold = dbToRaw(v("fetThreshold"));
    p.fet_compressor.ratio = ratioToRaw(v("fetRatio"));
    p.fet_compressor.knee = dbToRaw(v("fetKnee"));
    p.fet_compressor.knee_auto = v("fetKneeAuto") > 0.5f;
    p.fet_compressor.gain = dbToRaw(v("fetGain"));
    p.fet_compressor.gain_auto = v("fetGainAuto") > 0.5f;
    p.fet_compressor.attack = v("fetAttack") / 1000.0f;
    p.fet_compressor.attack_auto = v("fetAttackAuto") > 0.5f;
    p.fet_compressor.release = v("fetRelease") / 1000.0f;
    p.fet_compressor.release_auto = v("fetReleaseAuto") > 0.5f;
    p.fet_compressor.knee_multi = v("fetKneeMulti");
    p.fet_compressor.max_attack = v("fetMaxAttack") / 1000.0f;
    p.fet_compressor.max_release = v("fetMaxRelease") / 1000.0f;
    p.fet_compressor.crest = v("fetCrest") / 1000.0f;
    p.fet_compressor.adapt = v("fetAdapt");
    p.fet_compressor.no_clip = v("fetNoClip") > 0.5f;

    p.equalizer.enable = v("eqEnable") > 0.5f;
    constexpr int eqBandCounts[] = { 10, 15, 25, 31 };
    p.equalizer.band_count = static_cast<uint32_t>(eqBandCounts[static_cast<int>(v("eqBandCount"))]);
    for (int i = 0; i < 31; ++i)
        p.equalizer.band_levels[static_cast<size_t>(i)]
            = eqBandPtrs_[static_cast<size_t>(i)]->load(std::memory_order_relaxed);

    p.dynamic_eq.enable = v("deEnable") > 0.5f;
    p.dynamic_eq.band_count = static_cast<uint32_t>(v("deBandCount")) + 1;
    for (int b = 0; b < 10; ++b)
    {
        const auto& d = deBandPtrs_[static_cast<size_t>(b)];
        auto& band = p.dynamic_eq.bands[static_cast<size_t>(b)];
        band.frequency = d.freq->load(std::memory_order_relaxed);
        band.q = d.q->load(std::memory_order_relaxed);
        band.gain = d.gain->load(std::memory_order_relaxed);
        band.threshold = d.threshold->load(std::memory_order_relaxed);
        band.attack = d.attack->load(std::memory_order_relaxed);
        band.release = d.release->load(std::memory_order_relaxed);
        band.filter_type = static_cast<int>(d.type->load(std::memory_order_relaxed));
    }

    p.psychoacoustic_bass.enable = v("pBassEnable") > 0.5f;
    p.psychoacoustic_bass.cutoff = static_cast<uint32_t>(v("pBassCutoff"));
    p.psychoacoustic_bass.intensity = v("pBassIntensity");
    p.psychoacoustic_bass.harmonic_order = static_cast<uint32_t>(v("pBassHarmonicOrder")) + 2;
    p.psychoacoustic_bass.original_level = v("pBassOriginalLevel");

    p.bass.enable = v("bassEnable") > 0.5f;
    p.bass.mode = static_cast<int>(v("bassMode"));
    p.bass.frequency = static_cast<uint32_t>(v("bassFreq"));
    p.bass.gain = v("bassGain");
    p.bass.anti_pop = v("bassAntiPop") > 0.5f;

    p.bass_mono.enable = v("bassMonoEnable") > 0.5f;
    p.bass_mono.mode = static_cast<int>(v("bassMonoMode"));
    p.bass_mono.frequency = static_cast<uint32_t>(v("bassMonoFreq"));
    p.bass_mono.gain = v("bassMonoGain");
    p.bass_mono.anti_pop = v("bassMonoAntiPop") > 0.5f;

    p.spectrum_extension.enable = v("specExtEnable") > 0.5f;
    p.spectrum_extension.strength = static_cast<int>(v("specExtStrength"));
    p.spectrum_extension.exciter = v("specExtExciter");

    p.convolver.enable = v("convEnable") > 0.5f;
    p.convolver.cross_channel = v("convCrossChannel");

    p.ddc.enable = v("ddcEnable") > 0.5f;

    p.field_surround.enable = v("fieldEnable") > 0.5f;
    p.field_surround.widening = v("fieldWidening");
    p.field_surround.mid_image = v("fieldMidImage");
    p.field_surround.depth = static_cast<short>(v("fieldDepth"));

    p.diff_surround.enable = v("diffEnable") > 0.5f;
    p.diff_surround.delay = v("diffDelay");
    p.diff_surround.reverse = v("diffReverse") > 0.5f;
    p.diff_surround.wet_dry_mix = v("diffMix");
    p.diff_surround.lp_cutoff = v("diffLpCutoff");

    p.stereo_imager.enable = v("siEnable") > 0.5f;
    p.stereo_imager.low_width = v("siLowWidth");
    p.stereo_imager.mid_width = v("siMidWidth");
    p.stereo_imager.high_width = v("siHighWidth");
    p.stereo_imager.low_crossover = v("siLowCrossover");
    p.stereo_imager.high_crossover = v("siHighCrossover");

    p.headphone_surround.enable = v("vheEnable") > 0.5f;
    p.headphone_surround.quality = static_cast<int>(v("vheQuality"));

    p.reverb.enable = v("reverbEnable") > 0.5f;
    p.reverb.room_size = v("reverbRoomSize");
    p.reverb.width = v("reverbWidth");
    p.reverb.damp = v("reverbDamp");
    p.reverb.wet = v("reverbWet");
    p.reverb.dry = v("reverbDry");

    p.dynamic_system.enable = v("dsEnable") > 0.5f;
    p.dynamic_system.x_coeff_low = static_cast<int>(v("dsXLow"));
    p.dynamic_system.x_coeff_high = static_cast<int>(v("dsXHigh"));
    p.dynamic_system.y_coeff_low = static_cast<int>(v("dsYLow"));
    p.dynamic_system.y_coeff_high = static_cast<int>(v("dsYHigh"));
    p.dynamic_system.side_gain_low = v("dsSideGainLow");
    p.dynamic_system.side_gain_high = v("dsSideGainHigh");
    p.dynamic_system.strength = v("dsStrength");

    p.clarity.enable = v("clarityEnable") > 0.5f;
    p.clarity.mode = static_cast<int>(v("clarityMode"));
    p.clarity.gain = v("clarityGain");

    p.cure.enable = v("cureEnable") > 0.5f;
    p.cure.crossfeed_preset = static_cast<int>(v("curePreset"));

    p.tube_simulator.enable = v("tubeEnable") > 0.5f;
    p.tube_simulator.model = static_cast<int>(v("tubeModel"));
    p.tube_simulator.drive = v("tubeDrive");
    p.tube_simulator.mix = v("tubeMix");

    p.analog_x.enable = v("axEnable") > 0.5f;
    p.analog_x.mode = static_cast<int>(v("axMode"));

    p.speaker_correction.enable = v("spkEnable") > 0.5f;

    p.multiband_compressor.enable = v("mbcEnable") > 0.5f;
    p.multiband_compressor.band_count = v("mbcBandCount") < 0.5f ? 3u : 5u;
    for (int c = 0; c < 4; ++c)
        p.multiband_compressor.crossover_frequencies[static_cast<size_t>(c)]
            = mbcCrossPtrs_[static_cast<size_t>(c)]->load(std::memory_order_relaxed);
    for (int b = 0; b < 5; ++b)
    {
        const auto& m = mbcBandPtrs_[static_cast<size_t>(b)];
        auto& band = p.multiband_compressor.bands[static_cast<size_t>(b)];
        band.enable = m.enable->load(std::memory_order_relaxed) > 0.5f;
        band.threshold = dbToRaw(m.threshold->load(std::memory_order_relaxed));
        band.ratio = ratioToRaw(m.ratio->load(std::memory_order_relaxed));
        band.knee = dbToRaw(m.knee->load(std::memory_order_relaxed));
        band.knee_auto = m.kneeAuto->load(std::memory_order_relaxed) > 0.5f;
        band.gain = dbToRaw(m.gain->load(std::memory_order_relaxed));
        band.gain_auto = m.gainAuto->load(std::memory_order_relaxed) > 0.5f;
        band.attack = m.attack->load(std::memory_order_relaxed) / 1000.0f;
        band.attack_auto = m.attackAuto->load(std::memory_order_relaxed) > 0.5f;
        band.release = m.release->load(std::memory_order_relaxed) / 1000.0f;
        band.release_auto = m.releaseAuto->load(std::memory_order_relaxed) > 0.5f;
        band.knee_multi = m.kneeMulti->load(std::memory_order_relaxed);
        band.max_attack = m.maxAttack->load(std::memory_order_relaxed) / 1000.0f;
        band.max_release = m.maxRelease->load(std::memory_order_relaxed) / 1000.0f;
        band.crest = m.crest->load(std::memory_order_relaxed) / 1000.0f;
        band.adapt = m.adapt->load(std::memory_order_relaxed);
        band.no_clip = m.noClip->load(std::memory_order_relaxed) > 0.5f;
    }
}

juce::String ViPER4HostAudioProcessor::loadConvolverFile(const juce::String& path)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager_.createReaderFor(juce::File(path)));
    if (reader == nullptr)
        return "convErrRead";
    // ponytail: 冲激响应长度上限 10 秒；更长的选择几乎都是选错文件。
    auto frames = juce::jlimit<int64_t>(16, static_cast<int64_t>(reader->sampleRate) * 10,
        reader->lengthInSamples);
    const int channels = static_cast<int>(std::min<int64_t>(2, reader->numChannels));

    juce::AudioBuffer<float> buffer(channels, static_cast<int>(frames));
    reader->read(&buffer, 0, static_cast<int>(frames), 0, true, true);

    auto kernel = std::make_shared<PendingKernel>();
    kernel->samples.resize(static_cast<size_t>(frames) * static_cast<size_t>(channels));
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < static_cast<int>(frames); ++i)
            kernel->samples[static_cast<size_t>(i) * channels + static_cast<size_t>(c)]
                = buffer.getSample(c, i);
    kernel->frames = static_cast<uint32_t>(frames);
    kernel->channels = static_cast<uint32_t>(channels);
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingKernel_ = std::move(kernel);
    }
    parameters_.state.setProperty("convolverPath", path, nullptr);
    loadedConvolverPath_ = path;
    return {};
}

juce::String ViPER4HostAudioProcessor::loadDdcFile(const juce::String& path)
{
    const juce::File file(path);
    // ponytail: DDC 文本上限 4 MB；更大的选择几乎都是选错文件。
    if (!file.existsAsFile() || file.getSize() > 4 * 1024 * 1024)
        return "ddcErrRead";

    auto ddc = std::make_shared<PendingDdc>();
    if (!parseDdcText(file.loadFileAsString(), ddc->sections44100, ddc->sections48000))
        return "ddcErrFormat";

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingDdc_ = std::move(ddc);
    }
    parameters_.state.setProperty("ddcPath", path, nullptr);
    loadedDdcPath_ = path;
    return {};
}

void ViPER4HostAudioProcessor::applyStateTree(const juce::ValueTree& newState)
{
    // uiLanguage 属性存在状态树里，所以换树时要带过去。
    const auto lang = getUiLanguage();
    {
        juce::ScopedLock lock(getCallbackLock());
        parameters_.replaceState(newState);
        parameters_.state.setProperty("uiLanguage", lang, nullptr);
    }
    // convolverPath/ddcPath 只存路径，内核不随树走：切槽位/换预设后由
    // syncFileDataFromState 对齐（重载或卸载）。文件 IO 在锁外，别堵音频线程。
    syncFileDataFromState();
}

void ViPER4HostAudioProcessor::syncFileDataFromState()
{
    // 状态换树后按树里的路径对齐文件数据：与已载入的不一致就重载；路径
    // 为空而已载入过就走引擎的卸载接口（走 pending 交接，在音频线程上
    // 执行，与载入同款）。
    if (const auto conv = parameters_.state.getProperty("convolverPath", {}).toString();
        conv.isNotEmpty())
    {
        if (conv != loadedConvolverPath_)
            loadConvolverFile(conv);
    }
    else if (loadedConvolverPath_.isNotEmpty())
    {
        {
            std::lock_guard<std::mutex> lock(pendingMutex_);
            pendingKernel_.reset();
            pendingKernelUnload_ = true;
        }
        loadedConvolverPath_ = {};
    }
    if (const auto ddc = parameters_.state.getProperty("ddcPath", {}).toString();
        ddc.isNotEmpty() && ddc != loadedDdcPath_)
        loadDdcFile(ddc);
}

void ViPER4HostAudioProcessor::activateAbSlot(int slot)
{
    if (slot == activeAbSlot)
        return;
    abSlots[activeAbSlot] = getParametersState().copyState();
    activeAbSlot = slot;
    if (abSlots[slot].isValid())
        applyStateTree(abSlots[slot]);
}

juce::AudioProcessorEditor* ViPER4HostAudioProcessor::createEditor()
{
    return new ViPER4HostAudioProcessorEditor(*this);
}
bool ViPER4HostAudioProcessor::hasEditor() const { return true; }

const juce::String ViPER4HostAudioProcessor::getName() const { return pluginDisplayName; }
bool ViPER4HostAudioProcessor::acceptsMidi() const { return false; }
bool ViPER4HostAudioProcessor::producesMidi() const { return false; }
bool ViPER4HostAudioProcessor::isMidiEffect() const { return false; }
double ViPER4HostAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int ViPER4HostAudioProcessor::getNumPrograms() { return 1; }
int ViPER4HostAudioProcessor::getCurrentProgram() { return 0; }
void ViPER4HostAudioProcessor::setCurrentProgram(int) {}
const juce::String ViPER4HostAudioProcessor::getProgramName(int) { return {}; }
void ViPER4HostAudioProcessor::changeProgramName(int, const juce::String&) {}

void ViPER4HostAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // 外层包一层：参数状态 + A/B 槽位快照一起存。
    if (auto xml = parameters_.copyState().createXml())
    {
        juce::XmlElement root("ViPER4HostState");
        root.addChildElement(xml.release());
        auto* ab = new juce::XmlElement("Ab");
        ab->setAttribute("active", activeAbSlot);
        for (auto& slot : abSlots)
            if (auto slotXml = slot.createXml())
                ab->addChildElement(slotXml.release());
        root.addChildElement(ab);
        copyXmlToBinary(root, destData);
    }
}

void ViPER4HostAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto root = getXmlFromBinary(data, sizeInBytes))
    {
        // 老文件没有外层包装，直接就是状态树；此时没有 A/B 可恢复。
        auto* stateXml = root.get();
        auto* ab = root->getChildByName("Ab");
        if (root->hasTagName("ViPER4HostState"))
        {
            stateXml = root->getChildElement(0);
            ab = root->getChildByName("Ab");
        }
        if (stateXml != nullptr && stateXml->hasTagName(parameters_.state.getType()))
        {
            {
                juce::ScopedLock lock(getCallbackLock());
                parameters_.replaceState(juce::ValueTree::fromXml(*stateXml));
            }
            // 重新加载恢复出的状态里记录的文件数据（含空路径时的内核卸载）。
            syncFileDataFromState();
            if (ab != nullptr)
            {
                activeAbSlot = juce::jlimit(0, 1, ab->getIntAttribute("active"));
                for (int s = 0; s < 2; ++s)
                    if (auto* slotXml = ab->getChildElement(s))
                        abSlots[s] = juce::ValueTree::fromXml(*slotXml);
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViPER4HostAudioProcessor();
}
