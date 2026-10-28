#pragma once

#include <JuceHeader.h>
#include <vector>

// 基于 JSON 的界面多语言方案，与 DeepFilterNet-VST 相同：
// Localisation/*.json 通过 juce_add_binary_data 编进二进制，
// 按键名查找，找不到时回退英文。
namespace v4h::localisation
{
struct LanguageOption
{
    juce::String code;
    juce::String displayName;
};

const std::vector<LanguageOption>& availableLanguages();
juce::String tr(const juce::String& languageCode, const juce::String& key);
// 选项型参数的已翻译条目，未翻译时回退英文。
juce::String choiceText(const juce::String& paramId, int index, const juce::String& languageCode);
juce::String normaliseLanguageCode(const juce::String& code);
// 与系统区域最匹配的语言（zh-CN -> zh-Hans，zh-TW/HK -> zh-Hant，
// ja/ko/ru/es/de 等直接匹配，其余 en）。
juce::String systemLanguage();
// 把 JSON 的 "juce" 表喂给 JUCE 的全局 LocalisedStrings，翻译 JUCE 自带的
// 界面文案（音频设置面板等，即那些 TRANS() 字符串）。只能在独立版里调用——
// 插件模式下全局翻译表会泄漏到宿主进程。
void applyJuceTranslations(const juce::String& languageCode);
} // namespace v4h::localisation
