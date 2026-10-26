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
    // JUCE's binary data sanitises file names: "zh-Hans.json" -> "zhHans_json".
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
    // Key convention: <paramId><index>. Per-band dynamic-EQ filter types share
    // one key set (de1Type, de2Type, ... all map to deType<index>). Returns
    // empty when untranslated (tr() falls back to the key) so callers can use
    // the raw choice string.
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
} // namespace v4h::localisation
