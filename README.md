# ViPER4Host

ViPER audio effects as a VST2/VST3 plugin.

Wraps the [ViPERDSP](https://github.com/likelikeslike/ViPERDSP) engine as a
plugin so the ViPER processing chain can run inside any DAW or VST host.
Built with JUCE; VST2 support uses the FST compatibility headers.

## Exposed effects

Everything the engine supports is exposed: master limiter / output, AGC,
LUFS loudness targeting, FET and multiband compressors, graphic EQ
(10/15/25/31-band) and dynamic EQ, ViPER Bass, Bass Mono, psychoacoustic
bass, spectrum extension, convolver, DDC, field / differential surround,
stereo imager, headphone surround+ (VHE), reverberation, dynamic system,
clarity, Cure crossfeed, tube simulator, AnalogX and speaker correction.

## Building

Requirements: Windows, CMake 3.22+, MSVC x64, Ninja.

```bash
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Outputs land in `build/ViPER4Host_artefacts/Release/` (VST2 `dll`, VST3 `vst3`).

## Notes

- UI localisation: English / 简体中文 / 繁體中文, switchable in the editor and
  saved per instance with the host session. Terms follow ViPER4Windows.
- ViPERDSP is reverse-engineered from ViPER4Android and is personal-use only;
  see its README for the legal notice.
- The engine is 2-channel interleaved; the plugin folds any supported bus
  (mono or stereo) into stereo.
- Zero latency is reported to the host. The engine applies internal delay for
  convolver/VHE but exposes no latency getter yet.

## Repository layout

- `Source/` — plugin sources
- `extern/ViPERDSP` — the DSP engine (submodule)
- `extern/JUCE`, `extern/FST` — framework and VST2 headers (submodules)
