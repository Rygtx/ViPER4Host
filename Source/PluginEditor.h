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
    // Category tab count; keep in sync with pageKeys in the .cpp.
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
        // Curve editor (graphic EQ / dynamic EQ groups): a child of the group,
        // laid out between the title bar and the rows.
        std::unique_ptr<v4h::EqCurve> curve;
        // Same slot for the MBC crossover strip and the stereo imager view.
        std::unique_ptr<v4h::XoverStrip> strip;
        std::unique_ptr<v4h::SiView> siView;
        // Horizontal GR meter (FET compressor / output limiter groups).
        std::unique_ptr<v4h::GrMeter> grMeter;
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
    // Creates the group and its rows; returns it so callers can attach extras
    // (curve editor, file row) to it. titleEnable=false keeps any "*Enable"
    // param as a plain row (MBC's selected-band panel has no title switch).
    ParamGroup* addGroup(const juce::String& groupId, std::vector<juce::String> paramIds,
        bool titleEnable = true);
    // (Re)binds a row's widgets and attachments to one parameter. Used both by
    // addGroup and by selectDeBand/selectMbcBand, which re-point the detail
    // rows at whichever band the strip/curve selected.
    void bindRow(ParamRow& row, const juce::String& id, const juce::String& languageCode,
        juce::Component& parent, bool titleEnable = true);
    // Points the DynEQ / MBC detail rows at band index `band` (0-based).
    void selectDeBand(int band);
    void selectMbcBand(int band);
    // Hides rows/groups beyond the active band counts (eqBandCount, deBandCount,
    // mbcBandCount), then relayouts.
    void updateBandVisibility();
    void fillCombo(juce::ComboBox& combo, const juce::String& paramId,
        const juce::StringArray& choices, const juce::String& languageCode);
    void relabelAll(const juce::String& languageCode);
    void selectLanguage();
    void pickFile(ParamGroup& group);
    void refreshFileStatus(ParamGroup& group);
    // 30 Hz metering: polls processor meters, re-runs the spectrum FFT and
    // repaints the visible graphs / GR meters.
    void pullMetersAndSpectrum();
    // Presets & A/B compare.
    juce::File presetsDir() const;
    void refreshPresetList();
    void savePresetDialog();
    void activateAbSlot(int slot);

    ViPER4HostAudioProcessor& processor_;
    v4h::ViperLookAndFeel lookAndFeel_;
    std::vector<std::unique_ptr<ParamGroup>> groups_;
    // DynEQ / MBC detail rows, in bind order — one per suffix.
    std::vector<ParamRow*> deBandRows_, mbcBandRows_;
    int selectedDeBand_ = 0, selectedMbcBand_ = 0;
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

    // Presets & A/B compare. Slot state is captured on switch; edits always
    // land in the live state (and in the active slot on the next switch).
    juce::ComboBox presetBox_;
    juce::TextButton abButtons_[2];
    juce::TextButton abCopy_;
    juce::ValueTree abSlots_[2];
    int activeAbSlot_ = 0;
    juce::Array<juce::File> userPresetFiles_;
    // FFT pipeline for the spectrum underlay (mono output tap -> log bins).
    juce::dsp::FFT fft_{11}; // 2^11 = 2048
    // JUCE's real transform writes 2N floats for an N-point input.
    std::array<float, 4096> fftSamples_{};
    std::array<float, 2048> hann_{};
    std::array<float, v4h::EqCurve::kSpectrumPoints> spectrumDb_{};
    // Meter snapshots the graphs read while painting.
    float mbcGrDb_[5]{};
    float deGainDb_[10]{};
    int resizeIdleTicks_ = -1; // -1 = size saved; counts up after a resize
    std::array<int, 2> savedSize_{660, 600};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViPER4HostAudioProcessorEditor)
};
