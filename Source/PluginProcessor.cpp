#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Localisation.h"

ViPER4HostAudioProcessor::ViPER4HostAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Input", juce::AudioChannelSet::stereo(), true))
    , parameters_(*this, nullptr, "PARAMS", createParameterLayout())
{
    formatManager_.registerBasicFormats();
    parameters_.state.setProperty("uiLanguage", v4h::localisation::systemLanguage(), nullptr);
}

juce::String ViPER4HostAudioProcessor::getUiLanguage() const
{
    return parameters_.state.getProperty("uiLanguage", "en").toString();
}

void ViPER4HostAudioProcessor::setUiLanguage(const juce::String& languageCode)
{
    parameters_.state.setProperty("uiLanguage", languageCode, nullptr);
}

ViPER4HostAudioProcessor::~ViPER4HostAudioProcessor() = default;

namespace
{
// FET/MBC compressor thresholds and knees live in the natural-log domain:
// raw = dB * ln(10)/20. Ratios are stored as the signed slope -1/R.
constexpr float kDbToLog = 0.1151293f;
float dbToRaw(float db) { return db * kDbToLog; }
float ratioToRaw(float ratio) { return -1.0f / ratio; }

// Every sub-struct has operator==; compare all blocks so unwired params can't
// silently skip re-apply. Lives host-side to keep the ViPERDSP submodule
// pinned at its upstream commit.
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
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout
ViPER4HostAudioProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using BoolParameter = juce::AudioParameterBool;
    using ChoiceParameter = juce::AudioParameterChoice;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Defaults mirror ViPER4Windows (F:\Project\ViPER4Windows,
    // ViPER4Windows/lib/models/viper_state.dart), converted to host units.
    // Exception: FET/MBC Adapt stays at 4 s — V4W's engine copy stores a raw
    // log coefficient there that doesn't map to seconds.

    auto addBool = [&layout](const char* id, const char* name, bool def = false) {
        layout.add(std::make_unique<BoolParameter>(id, name, def));
    };
    auto addFloat = [&layout](const char* id, const char* name, float min, float max, float def,
                          const char* suffix = "") {
        // ponytail: range-based decimals (wide ranges are Hz/integers); pass
        // explicit stringFromValue per param if one ever needs custom formatting.
        const int decimals = max <= 10.0f ? 2 : 0;
        const juce::String label = suffix;
        layout.add(std::make_unique<Parameter>(
            id, name, juce::NormalisableRange<float>(min, max), def,
            juce::AudioParameterFloatAttributes()
                .withLabel(label)
                // JUCE 8's getText() doesn't append the label, so do it here.
                .withStringFromValueFunction([decimals, label](float v, int) {
                    // String(v, 0) isn't "0 decimals" — it falls back to %g
                    // (6 sig digits, juce_String.cpp writeDouble), so round
                    // integer params explicitly.
                    auto text = decimals > 0 ? juce::String(v, decimals)
                                             : juce::String(juce::roundToInt(v));
                    while (text.containsChar('.') // 1.00 -> 1, 0.50 -> 0.5
                        && (text.endsWithChar('0') || text.endsWithChar('.')))
                        text = text.dropLastCharacters(1);
                    return label.isNotEmpty() ? text + " " + label : text;
                })));
    };
    auto addChoice = [&layout](const char* id, const char* name, const juce::StringArray& choices, int def = 0) {
        layout.add(std::make_unique<ChoiceParameter>(id, name, choices, def));
    };

    // Master limiter / output
    addFloat("limiterThreshold", "Limiter Threshold", 0.3f, 1.0f, 1.0f);
    addFloat("limiterVolume", "Output Volume", 0.01f, 2.0f, 1.0f, "x");
    addFloat("limiterPan", "Channel Pan", -1.0f, 1.0f, 0.0f);

    // Playback gain control (AGC)
    addBool("pgcEnable", "AGC Enable");
    addFloat("pgcStrength", "AGC Strength", 0.5f, 3.0f, 1.0f, "x");
    addFloat("pgcMaxGain", "AGC Max Gain", 1.0f, 10.0f, 1.0f, "x");
    addFloat("pgcOutputThreshold", "AGC Output Threshold", 0.3f, 1.0f, 1.0f);

    // LUFS loudness targeting
    addBool("lufsEnable", "LUFS Enable");
    addFloat("lufsTarget", "LUFS Target", -24.0f, -8.0f, -14.0f, " LUFS");
    addFloat("lufsMaxGain", "LUFS Max Gain", 0.0f, 12.0f, 6.0f, " dB");
    addChoice("lufsSpeed", "LUFS Speed", { "1", "2", "3" }, 1);

    // FET compressor
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

    // Graphic equalizer
    addBool("eqEnable", "EQ Enable");
    addChoice("eqBandCount", "EQ Bands", { "10", "15", "25", "31" }, 0);
    for (int i = 1; i <= 31; ++i)
    {
        auto id = "eqBand" + juce::String(i);
        addFloat(id.toRawUTF8(), id.toRawUTF8(), -12.0f, 12.0f, 0.0f, " dB");
    }

    // Dynamic equalizer
    addBool("deEnable", "DynEQ Enable");
    addChoice("deBandCount", "DynEQ Bands", { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" }, 2);
    constexpr float deDefaultFreqs[] = { 60, 150, 400, 60, 60, 60, 60, 60, 60, 60 };
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

    // Psychoacoustic bass
    addBool("pBassEnable", "Psychoacoustic Bass Enable");
    addFloat("pBassCutoff", "PB Cutoff", 60.0f, 150.0f, 80.0f, " Hz");
    addFloat("pBassIntensity", "PB Intensity", 0.0f, 1.0f, 0.5f);
    addChoice("pBassHarmonicOrder", "PB Harmonic Order", { "2", "3", "4", "5" }, 1);
    addFloat("pBassOriginalLevel", "PB Original Level", 0.0f, 1.0f, 1.0f);

    // ViPER Bass
    addBool("bassEnable", "Bass Enable");
    addChoice("bassMode", "Bass Mode", { "Natural", "Pure Bass+", "Subwoofer" });
    addFloat("bassFreq", "Bass Frequency", 15.0f, 150.0f, 70.0f, " Hz");
    addFloat("bassGain", "Bass Gain", 0.5f, 10.0f, 0.5f, "x");
    addBool("bassAntiPop", "Bass Anti-Pop");

    // Bass Mono
    addBool("bassMonoEnable", "Bass Mono Enable");
    addChoice("bassMonoMode", "Bass Mono Mode", { "Natural", "Pure Bass+", "Subwoofer" });
    addFloat("bassMonoFreq", "Bass Mono Frequency", 15.0f, 150.0f, 70.0f, " Hz");
    addFloat("bassMonoGain", "Bass Mono Gain", 0.5f, 10.0f, 0.5f, "x");
    addBool("bassMonoAntiPop", "Bass Mono Anti-Pop");

    // Spectrum extension
    addBool("specExtEnable", "Spectrum Extension Enable");
    addFloat("specExtStrength", "SE Reference Freq", 2200.0f, 8200.0f, 7600.0f, " Hz");
    addFloat("specExtExciter", "SE Exciter", 0.0f, 6.0f, 0.0f);

    // Convolver
    addBool("convEnable", "Convolver Enable");
    addFloat("convCrossChannel", "Convolver Cross Channel", 0.0f, 1.0f, 0.0f);

    // DDC
    addBool("ddcEnable", "DDC Enable");

    // Field surround
    addBool("fieldEnable", "Field Surround Enable");
    addFloat("fieldWidening", "Field Widening", 0.0f, 8.0f, 0.0f);
    addFloat("fieldMidImage", "Field Mid Image", 1.0f, 2.0f, 1.5f);
    addFloat("fieldDepth", "Field Depth", 200.0f, 950.0f, 200.0f);

    // Differential surround
    addBool("diffEnable", "Diff Surround Enable");
    addFloat("diffDelay", "Diff Delay", 1.0f, 20.0f, 5.0f, " ms");
    addBool("diffReverse", "Diff Reverse");
    addFloat("diffMix", "Diff Wet/Dry Mix", 0.0f, 1.0f, 1.0f);
    addFloat("diffLpCutoff", "Diff LP Cutoff", 0.0f, 20000.0f, 0.0f, " Hz");

    // Stereo imager
    addBool("siEnable", "Stereo Imager Enable");
    addFloat("siLowWidth", "SI Low Width", 0.0f, 2.0f, 1.0f);
    addFloat("siMidWidth", "SI Mid Width", 0.0f, 2.0f, 1.0f);
    addFloat("siHighWidth", "SI High Width", 0.0f, 2.0f, 1.0f);
    addFloat("siLowCrossover", "SI Low Crossover", 80.0f, 400.0f, 200.0f, " Hz");
    addFloat("siHighCrossover", "SI High Crossover", 2000.0f, 8000.0f, 4000.0f, " Hz");

    // Headphone surround+ (VHE)
    addBool("vheEnable", "VHE Enable");
    addChoice("vheQuality", "VHE Level", { "Level 1", "Level 2", "Level 3", "Level 4", "Level 5" });

    // Reverberation
    addBool("reverbEnable", "Reverb Enable");
    addFloat("reverbRoomSize", "Reverb Room Size", 0.0f, 1.0f, 0.0f);
    addFloat("reverbWidth", "Reverb Width", 0.0f, 1.0f, 0.0f);
    addFloat("reverbDamp", "Reverb Damp", 0.0f, 1.0f, 0.0f);
    addFloat("reverbWet", "Reverb Wet", 0.0f, 1.0f, 0.0f);
    addFloat("reverbDry", "Reverb Dry", 0.0f, 1.0f, 1.0f);

    // Dynamic system
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

    // Cure crossfeed
    addBool("cureEnable", "Cure Enable");
    addChoice("curePreset", "Cure Preset", { "Preset 1", "Preset 2", "Preset 3" });

    // Tube simulator
    addBool("tubeEnable", "Tube Enable");
    addChoice("tubeModel", "Tube Model", { "12AX7", "12AU7", "6N1P" });
    addFloat("tubeDrive", "Tube Drive", 0.0f, 1.0f, 0.5f);
    addFloat("tubeMix", "Tube Mix", 0.0f, 1.0f, 0.3f);

    // AnalogX
    addBool("axEnable", "AnalogX Enable");
    addChoice("axMode", "AnalogX Mode", { "Light", "Medium", "Heavy" });

    // Speaker correction
    addBool("spkEnable", "Speaker Correction Enable");

    // Multiband compressor
    addBool("mbcEnable", "MBC Enable");
    addChoice("mbcBandCount", "MBC Bands", { "1", "2", "3", "4", "5" }, 4);
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

    // ViPERDSP is 2-channel interleaved; fold everything into stereo.
    const float* left = numChannels > 0 ? buffer.getReadPointer(0) : nullptr;
    const float* right = numChannels > 1 ? buffer.getReadPointer(1) : left;
    if (static_cast<int>(interleaved_.size()) < numFrames * 2)
        interleaved_.resize(static_cast<size_t>(numFrames) * 2);
    for (int i = 0; i < numFrames; ++i)
    {
        interleaved_[static_cast<size_t>(i) * 2] = left != nullptr ? left[i] : 0.0f;
        interleaved_[static_cast<size_t>(i) * 2 + 1] = right != nullptr ? right[i] : 0.0f;
    }

    // File-based payloads are staged by the message thread and swapped in
    // here: the engine mutates buffers the audio thread reads, so the load
    // itself must run on this thread.
    if (pendingMutex_.try_lock())
    {
        if (auto kernel = std::move(pendingKernel_))
            engine_.LoadConvolverKernel(kernel->samples.data(), kernel->frames, kernel->channels, 1);
        if (auto ddc = std::move(pendingDdc_))
            engine_.LoadDdcCoefficients(ddc->sections44100.data(), ddc->sections48000.data(),
                static_cast<uint32_t>(ddc->sections44100.size()));
        pendingMutex_.unlock();
    }

    if (parameters_.getRawParameterValue("bypass")->load(std::memory_order_relaxed) < 0.5f)
    {
        viper::ViPERParams params{};
        buildEngineParams(params);
        if (paramsDiffer(params, lastAppliedParams_))
        {
            engine_.ApplyParams(params);
            lastAppliedParams_ = params;
        }

        // ponytail: ViPERDSP applies internal delay for convolver/VHE but
        // exposes no latency getter; we report zero latency. Revisit if the
        // engine grows a GetLatency API.
        engine_.Process(interleaved_, static_cast<uint32_t>(numFrames));
    }

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
    // getRawParameterValue is an O(1) lookup; once per block is negligible.
    auto v = [this](const char* id) {
        return parameters_.getRawParameterValue(id)->load(std::memory_order_relaxed);
    };
    auto vs = [this](const juce::String& id) {
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
        p.equalizer.band_levels[static_cast<size_t>(i)] = vs("eqBand" + juce::String(i + 1));

    p.dynamic_eq.enable = v("deEnable") > 0.5f;
    p.dynamic_eq.band_count = static_cast<uint32_t>(v("deBandCount")) + 1;
    for (int b = 0; b < 10; ++b)
    {
        const juce::String pfx = "de" + juce::String(b + 1);
        auto& band = p.dynamic_eq.bands[static_cast<size_t>(b)];
        band.frequency = vs(pfx + "Freq");
        band.q = vs(pfx + "Q");
        band.gain = vs(pfx + "Gain");
        band.threshold = vs(pfx + "Threshold");
        band.attack = vs(pfx + "Attack");
        band.release = vs(pfx + "Release");
        band.filter_type = static_cast<int>(vs(pfx + "Type"));
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
    p.multiband_compressor.band_count = static_cast<uint32_t>(v("mbcBandCount")) + 1;
    for (int c = 0; c < 4; ++c)
        p.multiband_compressor.crossover_frequencies[static_cast<size_t>(c)] =
            vs("mbcCross" + juce::String(c + 1));
    for (int b = 0; b < 5; ++b)
    {
        const juce::String pfx = "mbc" + juce::String(b + 1);
        auto& band = p.multiband_compressor.bands[static_cast<size_t>(b)];
        band.enable = vs(pfx + "Enable") > 0.5f;
        band.threshold = dbToRaw(vs(pfx + "Threshold"));
        band.ratio = ratioToRaw(vs(pfx + "Ratio"));
        band.knee = dbToRaw(vs(pfx + "Knee"));
        band.knee_auto = vs(pfx + "KneeAuto") > 0.5f;
        band.gain = dbToRaw(vs(pfx + "Gain"));
        band.gain_auto = vs(pfx + "GainAuto") > 0.5f;
        band.attack = vs(pfx + "Attack") / 1000.0f;
        band.attack_auto = vs(pfx + "AttackAuto") > 0.5f;
        band.release = vs(pfx + "Release") / 1000.0f;
        band.release_auto = vs(pfx + "ReleaseAuto") > 0.5f;
        band.knee_multi = vs(pfx + "KneeMulti");
        band.max_attack = vs(pfx + "MaxAttack") / 1000.0f;
        band.max_release = vs(pfx + "MaxRelease") / 1000.0f;
        band.crest = vs(pfx + "Crest") / 1000.0f;
        band.adapt = vs(pfx + "Adapt");
        band.no_clip = vs(pfx + "NoClip") > 0.5f;
    }
}

juce::String ViPER4HostAudioProcessor::loadConvolverFile(const juce::String& path)
{
    auto reader = std::unique_ptr<juce::AudioFormatReader>(formatManager_.createReaderFor(juce::File(path)));
    if (reader == nullptr)
        return "convErrRead";
    // ponytail: cap IR length at 10 s; longer picks are almost always a wrong file.
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
    return {};
}

juce::String ViPER4HostAudioProcessor::loadDdcFile(const juce::String& path)
{
    auto text = juce::File(path).loadFileAsString();
    if (text.isEmpty())
        return "ddcErrRead";

    juce::StringArray tokens;
    tokens.addTokens(text, " \r\n\t,", "");
    std::vector<float> values;
    values.reserve(static_cast<size_t>(tokens.size()));
    for (const auto& t : tokens)
        if (t.trim().isNotEmpty())
            values.push_back(t.getFloatValue());

    // ponytail: assumes the ViPER4Windows .ddc text layout — whitespace
    // separated biquad sections of 5 floats, first half for 44.1 kHz, second
    // half for 48 kHz. Add a header-aware parser if real files disagree.
    if (values.size() < 10 || values.size() % 10 != 0)
        return "ddcErrFormat";

    const size_t half = values.size() / 2;
    const size_t sections = half / 5;
    auto ddc = std::make_shared<PendingDdc>();
    for (size_t s = 0; s < sections; ++s)
    {
        viper::BiquadSection lo{}, hi{};
        for (int k = 0; k < 5; ++k)
        {
            lo[static_cast<size_t>(k)] = values[s * 5 + static_cast<size_t>(k)];
            hi[static_cast<size_t>(k)] = values[half + s * 5 + static_cast<size_t>(k)];
        }
        ddc->sections44100.push_back(lo);
        ddc->sections48000.push_back(hi);
    }
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pendingDdc_ = std::move(ddc);
    }
    parameters_.state.setProperty("ddcPath", path, nullptr);
    return {};
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
    if (auto xml = parameters_.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void ViPER4HostAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters_.state.getType()))
        {
            parameters_.replaceState(juce::ValueTree::fromXml(*xml));
            // Reload file-based payloads queued by the restored state.
            if (const auto conv = parameters_.state.getProperty("convolverPath", {}).toString(); conv.isNotEmpty())
                loadConvolverFile(conv);
            if (const auto ddc = parameters_.state.getProperty("ddcPath", {}).toString(); ddc.isNotEmpty())
                loadDdcFile(ddc);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViPER4HostAudioProcessor();
}
