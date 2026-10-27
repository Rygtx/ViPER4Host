#pragma once

#include <JuceHeader.h>
#include <vector>

// JSON-based UI localisation, same scheme as DeepFilterNet-VST:
// Localisation/*.json compiled into the binary via juce_add_binary_data,
// lookup by key with English fallback.
namespace v4h::localisation
{
struct LanguageOption
{
    juce::String code;
    juce::String displayName;
};

const std::vector<LanguageOption>& availableLanguages();
juce::String tr(const juce::String& languageCode, const juce::String& key);
// Translated choice entry for a choice parameter, English fallback.
juce::String choiceText(const juce::String& paramId, int index, const juce::String& languageCode);
juce::String normaliseLanguageCode(const juce::String& code);
// Best match for the OS locale (zh-CN -> zh-Hans, zh-TW/HK -> zh-Hant, else en).
juce::String systemLanguage();
// Feed JUCE's own UI strings (audio settings panel etc., the TRANS() ones) into
// JUCE's global LocalisedStrings from the JSON "juce" table. Callers must gate
// this to the standalone app — in plugin mode the global table would leak into
// the host process.
void applyJuceTranslations(const juce::String& languageCode);
} // namespace v4h::localisation
