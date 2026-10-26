// NOTE: ViPERDSP headers (viper/ViPER.h) must never land in a JUCE
// translation unit — their global IIRFilter class is ambiguous with
// juce::IIRFilter. ViperEngine.h keeps them walled off in its own TU.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ViperEngine.h"

ViPER4HostAudioProcessor::ViPER4HostAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                         .withInput("Input", juce::AudioChannelSet::stereo(), true))
    , parameters_(*this, nullptr, "PARAMS", createParameterLayout())
    , engine_(std::make_unique<ViperEngine>())
{
}

ViPER4HostAudioProcessor::~ViPER4HostAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout
ViPER4HostAudioProcessor::createParameterLayout()
{
    using Parameter = juce::AudioParameterFloat;
    using BoolParameter = juce::AudioParameterBool;
    using ChoiceParameter = juce::AudioParameterChoice;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    auto addBool = [&layout](const char* id, const char* name, bool def = false) {
        layout.add(std::make_unique<BoolParameter>(id, name, def));
    };
    auto addFloat = [&layout](const char* id, const char* name, float min, float max, float def,
                          const char* suffix = "") {
        layout.add(std::make_unique<Parameter>(id, name, juce::NormalisableRange<float>(min, max), def, suffix));
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

    // ViPER Bass
    addBool("bassEnable", "Bass Enable");
    addChoice("bassMode", "Bass Mode", { "Natural", "Pure Bass+", "Subwoofer" });
    addFloat("bassFreq", "Bass Frequency", 15.0f, 150.0f, 60.0f, " Hz");
    addFloat("bassGain", "Bass Gain", 0.5f, 10.0f, 1.0f, "x");

    // Bass Mono
    addBool("bassMonoEnable", "Bass Mono Enable");
    addFloat("bassMonoFreq", "Bass Mono Frequency", 15.0f, 150.0f, 60.0f, " Hz");
    addFloat("bassMonoGain", "Bass Mono Gain", 0.5f, 10.0f, 1.0f, "x");

    // ViPER Clarity
    addBool("clarityEnable", "Clarity Enable");
    addChoice("clarityMode", "Clarity Mode", { "Natural", "OZone", "XHiFi" });
    addFloat("clarityGain", "Clarity Gain", 0.0f, 4.5f, 1.0f, "x");

    // Headphone surround+ (VHE)
    addBool("vheEnable", "VHE Enable");
    addChoice("vheQuality", "VHE Level", { "Level 1", "Level 2", "Level 3", "Level 4", "Level 5" });

    // Field surround
    addBool("fieldEnable", "Field Surround Enable");
    addFloat("fieldWidening", "Field Widening", 0.0f, 8.0f, 1.0f);
    addFloat("fieldMidImage", "Field Mid Image", 1.0f, 2.0f, 1.0f);
    addFloat("fieldDepth", "Field Depth", 200.0f, 950.0f, 500.0f);

    // Reverberation
    addBool("reverbEnable", "Reverb Enable");
    addFloat("reverbRoomSize", "Reverb Room Size", 0.0f, 1.0f, 0.3f);
    addFloat("reverbWidth", "Reverb Width", 0.0f, 1.0f, 0.5f);
    addFloat("reverbDamp", "Reverb Damp", 0.0f, 1.0f, 0.5f);
    addFloat("reverbWet", "Reverb Wet", 0.0f, 1.0f, 0.25f);
    addFloat("reverbDry", "Reverb Dry", 0.0f, 1.0f, 1.0f);

    // Cure crossfeed
    addBool("cureEnable", "Cure Enable");
    addChoice("curePreset", "Cure Preset", { "Preset 1", "Preset 2", "Preset 3" });

    // Tube simulator
    addBool("tubeEnable", "Tube Enable");
    addChoice("tubeModel", "Tube Model", { "12AX7", "12AU7", "6N1P" });
    addFloat("tubeDrive", "Tube Drive", 0.0f, 1.0f, 0.5f);
    addFloat("tubeMix", "Tube Mix", 0.0f, 1.0f, 0.3f);

    addBool("bypass", "Bypass");

    return layout;
}

void ViPER4HostAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    engine_->prepare(sampleRate, samplesPerBlock);
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

    if (parameters_.getRawParameterValue("bypass")->load(std::memory_order_relaxed) < 0.5f)
    {
        viper::ViPERParams params{};
        buildEngineParams(params);
        engine_->applyParams(params);
        engine_->process(interleaved_, numFrames);
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

    p.master_limiter.threshold = v("limiterThreshold");
    p.master_limiter.output_volume = v("limiterVolume");
    p.master_limiter.channel_pan = v("limiterPan");

    p.playback_gain_control.enable = v("pgcEnable") > 0.5f;
    p.playback_gain_control.strength = v("pgcStrength");
    p.playback_gain_control.max_gain = v("pgcMaxGain");
    p.playback_gain_control.output_threshold = 1.0f;

    p.bass.enable = v("bassEnable") > 0.5f;
    p.bass.mode = static_cast<int>(v("bassMode"));
    p.bass.frequency = static_cast<uint32_t>(v("bassFreq"));
    p.bass.gain = v("bassGain");
    p.bass.anti_pop = true;

    p.bass_mono.enable = v("bassMonoEnable") > 0.5f;
    p.bass_mono.mode = 0;
    p.bass_mono.frequency = static_cast<uint32_t>(v("bassMonoFreq"));
    p.bass_mono.gain = v("bassMonoGain");
    p.bass_mono.anti_pop = true;

    p.clarity.enable = v("clarityEnable") > 0.5f;
    p.clarity.mode = static_cast<int>(v("clarityMode"));
    p.clarity.gain = v("clarityGain");

    p.headphone_surround.enable = v("vheEnable") > 0.5f;
    p.headphone_surround.quality = static_cast<int>(v("vheQuality"));

    p.field_surround.enable = v("fieldEnable") > 0.5f;
    p.field_surround.widening = v("fieldWidening");
    p.field_surround.mid_image = v("fieldMidImage");
    p.field_surround.depth = static_cast<short>(v("fieldDepth"));

    p.reverb.enable = v("reverbEnable") > 0.5f;
    p.reverb.room_size = v("reverbRoomSize");
    p.reverb.width = v("reverbWidth");
    p.reverb.damp = v("reverbDamp");
    p.reverb.wet = v("reverbWet");
    p.reverb.dry = v("reverbDry");

    p.cure.enable = v("cureEnable") > 0.5f;
    p.cure.crossfeed_preset = static_cast<int>(v("curePreset"));

    p.tube_simulator.enable = v("tubeEnable") > 0.5f;
    p.tube_simulator.model = static_cast<int>(v("tubeModel"));
    p.tube_simulator.drive = v("tubeDrive");
    p.tube_simulator.mix = v("tubeMix");
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
            parameters_.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ViPER4HostAudioProcessor();
}
