#pragma once

// JUCE-free interface to the ViPERDSP engine. Both walls matter:
//  - ViPERDSP's global IIRFilter class is ambiguous with juce::IIRFilter when
//    ViPER.h lands in a JUCE translation unit, so this header must not
//    include any viper headers.
//  - This header itself is safe to include from the plugin sources.

#include <memory>
#include <vector>

namespace viper
{
struct ViPERParams;
}

// Wraps the ViPERDSP engine (hard-wired to 2-channel interleaved audio).
class ViperEngine
{
public:
    ViperEngine();
    ~ViperEngine();
    ViperEngine(const ViperEngine&) = delete;
    ViperEngine& operator=(const ViperEngine&) = delete;

    void prepare(double sampleRate, int maxBlockSize);

    // interleaved holds >= numFrames * 2 stereo-interleaved samples, in place.
    void process(std::vector<float>& interleaved, int numFrames);

    // Applies the full parameter set; cheap no-op when nothing changed
    // (each viper params struct has an operator==). Safe on the audio thread.
    void applyParams(const viper::ViPERParams& params);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
