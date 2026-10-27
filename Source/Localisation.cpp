#include "Localisation.h"
#include "LocalisationBinaryData.h"

#include <map>

namespace v4h::localisation
{
namespace
{
const char* kLanguages[] = { "en", "zh-Hans", "zh-Hant" };

juce::var loadLanguage(const juce::String& code)
{
    // JUCE 会重写二进制资源名："zh-Hans.json" -> "zhHans_json"。
    const juce::String resourceName = code.replace("-", "") + "_json";
    int size = 0;
    if (const auto* data = Viper4HostLocalisationData::getNamedResource(
            resourceName.toRawUTF8(), size))
        return juce::JSON::parse(juce::String::createStringFromData(data, size));
    return {};
}
} // namespace

const std::vector<LanguageOption>& availableLanguages()
{
    static const std::vector<LanguageOption> options = [] {
        std::vector<LanguageOption> result;
        for (const auto* code : kLanguages)
        {
            auto json = loadLanguage(code);
            result.push_back({ code, json.getProperty("displayName", code).toString() });
        }
        return result;
    }();
    return options;
}

juce::String tr(const juce::String& languageCode, const juce::String& key)
{
    static const auto cache = [] {
        std::map<juce::String, juce::var> map;
        for (const auto* code : kLanguages)
            map[code] = loadLanguage(code).getProperty("strings", {});
        return map;
    }();

    const auto& strings = cache.at(normaliseLanguageCode(languageCode));
    juce::String text = strings.getProperty(key, {});
    if (text.isEmpty() && languageCode != "en")
        text = cache.at("en").getProperty(key, {});
    return text.isNotEmpty() ? text : key;
}

juce::String choiceText(const juce::String& paramId, int index, const juce::String& languageCode)
{
    // 键名约定：<paramId><序号>。各分带的动态 EQ 滤波类型共用一套键
    // （de1Type、de2Type……都映射到 deType<序号>）。未翻译时返回空串
    // （tr() 会回退显示键名），调用方可据此改用原始选项文本。
    const juce::String key = paramId.endsWith("Type")
        ? "deType" + juce::String(index)
        : paramId + juce::String(index);
    const auto text = tr(languageCode, key);
    return text == key ? juce::String() : text;
}

juce::String normaliseLanguageCode(const juce::String& code)
{
    auto trimmed = code.trim().replaceCharacter('_', '-');
    for (const auto* supported : kLanguages)
        if (trimmed.equalsIgnoreCase(supported))
            return supported;
    return "en";
}

juce::String systemLanguage()
{
    const auto language = juce::SystemStats::getUserLanguage().toLowerCase();
    if (language == "zh")
    {
        const auto region = juce::SystemStats::getUserRegion().toUpperCase();
        if (region == "TW" || region == "HK" || region == "MO")
            return "zh-Hant";
        return "zh-Hans";
    }
    return "en";
}

void applyJuceTranslations(const juce::String& languageCode)
{
    const auto lang = normaliseLanguageCode(languageCode);
    const auto table = lang == "en" ? juce::var() : loadLanguage(lang).getProperty("juce", {});
    auto* pairs = table.getDynamicObject();
    if (pairs == nullptr)
    {
        juce::LocalisedStrings::setCurrentMappings(nullptr);
        return;
    }

    // LocalisedStrings 每行解析一对 "原文"="译文"。
    juce::StringArray items;
    for (const auto& pair : pairs->getProperties())
        items.add(pair.name.toString().quoted() + "=" + pair.value.toString().quoted());
    juce::LocalisedStrings::setCurrentMappings(
        new juce::LocalisedStrings(items.joinIntoString("\n"), false));
}
} // namespace v4h::localisation
