#include "ViperEngine.h"

#include <viper/ViPER.h>

struct ViperEngine::Impl
{
    ViPER engine;
    double sampleRate = 0.0;
    int maxBlockSize = 0;
    viper::ViPERParams lastApplied{};
};

ViperEngine::ViperEngine()
    : impl_(std::make_unique<Impl>())
{
}
ViperEngine::~ViperEngine() = default;

void ViperEngine::prepare(double sampleRate, int maxBlockSize)
{
    if (sampleRate <= 0.0 || maxBlockSize <= 0)
        return;

    impl_->sampleRate = sampleRate;
    impl_->maxBlockSize = maxBlockSize;
    impl_->engine.SetSamplingRate(static_cast<uint32_t>(sampleRate + 0.5));
    impl_->engine.RequestBuffersReset();
}

void ViperEngine::applyParams(const viper::ViPERParams& params)
{
    const viper::ViPERParams& last = impl_->lastApplied;
    if (params.master_limiter == last.master_limiter
        && params.playback_gain_control == last.playback_gain_control
        && params.lufs == last.lufs
        && params.bass == last.bass
        && params.bass_mono == last.bass_mono
        && params.spectrum_extension == last.spectrum_extension
        && params.field_surround == last.field_surround
        && params.stereo_imager == last.stereo_imager
        && params.headphone_surround == last.headphone_surround
        && params.reverb == last.reverb
        && params.clarity == last.clarity
        && params.cure == last.cure
        && params.tube_simulator == last.tube_simulator)
        return;

    impl_->engine.ApplyParams(params);
    impl_->lastApplied = params;
}

void ViperEngine::process(std::vector<float>& interleaved, int numFrames)
{
    if (numFrames <= 0)
        return;

    if (numFrames > impl_->maxBlockSize)
        prepare(impl_->sampleRate, numFrames);

    // ponytail: ViPERDSP applies internal delay for convolver/VHE but exposes
    // no latency getter; we report zero latency to the host. Revisit if
    // ViPERDSP grows a GetLatency API.
    impl_->engine.Process(interleaved, static_cast<uint32_t>(numFrames));
}
