# ViPER4Host

ViPER audio effects as a VST2/VST3 plugin.

Wraps the [ViPERDSP](https://github.com/likelikeslike/ViPERDSP) engine as a
plugin so the ViPER processing chain can run inside any DAW or VST host.
Built with JUCE; VST2 support uses the FST compatibility headers.

## Exposed effects (first pass)

Master limiter / output, AGC (playback gain control), ViPER Bass, Bass Mono,
ViPER Clarity, Headphone Surround+ (VHE), Field Surround, Reverberation,
Cure crossfeed, Tube Simulator.

Equalizer (31-band), convolver, DDC, compressors and the remaining effects are
not exposed yet — the engine supports them, parameter plumbing is additive.

## Building

Requirements: Windows, CMake 3.22+, MSVC x64, Ninja.

```bash
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Outputs land in `build/ViPER4Host_artefacts/Release/` (VST2 `dll`, VST3 `vst3`).

## Notes

- ViPERDSP is reverse-engineered from ViPER4Android and is personal-use only;
  see its README for the legal notice.
- The engine is 2-channel interleaved; the plugin folds any supported bus
  (mono or stereo) into stereo.
- Zero latency is reported to the host. The engine applies internal delay for
  convolver/VHE but exposes no latency getter yet.

## Repository layout

- `Source/` — plugin sources
- `ViPERDSP/` — the DSP engine (submodule)
- `extern/JUCE`, `extern/FST` — framework and VST2 headers (submodules)
