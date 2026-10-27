# ViPER4Host

ViPER 音效的 VST2/VST3 插件。

把 [ViPERDSP](https://github.com/likelikeslike/ViPERDSP) 引擎包装成插件,
让整套 ViPER 处理链可以在任意 DAW 或 VST 宿主里运行。基于 JUCE 构建;
VST2 支持使用 FST 兼容头文件。

使用说明书:[MANUAL.md](MANUAL.md)(中文,逐功能讲解原理与用法)。

## 已实现的效果

引擎支持的功能全部开放:主输出限幅器、自动音量补偿(AGC)、LUFS 响度
目标、FET 压缩器与多段压缩器、图形均衡器(10/15/25/31 段)与动态均衡器、
ViPER 低音增强、低音单声道、心理声学低音、频谱延展、卷积器、DDC、
环绕扩展 / 差分环绕、立体声声像、耳机环绕+(VHE)、混响、动态系统、
清晰度、听觉保护(Cure 串音补偿)、电子管模拟、AnalogX 与音箱校正。

## 构建

环境要求:Windows、CMake 3.22+、MSVC x64、Ninja。

```bash
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

产物位于 `build/ViPER4Host_artefacts/Release/`(VST2 为 `dll`,VST3 为
`vst3`)。Windows 上也可以直接运行 `build.bat`(自动设置 MSVC 环境,
配置类型为 RelWithDebInfo)。

## 说明

- 界面支持 English / 简体中文 / 繁體中文,可在编辑器里切换,选择随插件
  实例保存在宿主工程中。术语与 ViPER4Windows 一致。
- ViPERDSP 逆向自 ViPER4Android,仅限个人使用;法律声明见其 README。
- 引擎为 2 声道交错格式;插件会把所有支持的声道布局(单声道或立体声)
  折叠成立体声处理。
- 插件向宿主报告零延迟。卷积器 / VHE 有引擎内部延迟,但引擎尚未提供
  延迟查询接口。

## 仓库结构

- `Source/` — 插件源码
- `extern/ViPERDSP` — DSP 引擎(子模块)
- `extern/JUCE`、`extern/FST` — 框架与 VST2 头文件(子模块)

## 鸣谢

- [ViPERDSP](https://github.com/likelikeslike/ViPERDSP) — 原始上游 DSP 引擎,作者
  [likelikeslike](https://github.com/likelikeslike)。本仓库的 `extern/ViPERDSP`
  子模块使用其分支 [Rygtx/ViPERDSP](https://github.com/Rygtx/ViPERDSP)。
- ViPER4Android 原作:Zhuhang 与 ViPER520。
- 逆向工程:[Martmists](https://github.com/Martmists-GH)、[Iscle](https://github.com/iscle)
  与 [likelikeslike](https://github.com/likelikeslike)
  ([AndroidAudioMods/ViPERFX_RE](https://github.com/AndroidAudioMods/ViPERFX_RE)、
  [likelikeslike/ViPERFX_RE](https://github.com/likelikeslike/ViPERFX_RE))。
- [JUCE](https://juce.com/)(插件框架)与 [FST](https://git.iem.at/zmoelnig/FST)(VST2 兼容头文件)。

## 许可证

- 本仓库自有代码(`Source/`、构建脚本与文档)以 [GPL-3.0](LICENSE) 发布。
- `extern/ViPERDSP` 引擎本身没有开源许可:代码逆向自 ViPER4Android,原知识产权
  归 Zhuhang 与 ViPER520,仅限个人使用、禁止商用,详见其
  [README](extern/ViPERDSP/README.md)。
- `extern/JUCE` 采用 AGPLv3,`extern/FST` 采用 GPLv3,以各自仓库内的许可证
  文本为准。
