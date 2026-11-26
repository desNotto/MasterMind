#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <map>

/** String table (spec section 8, localisation): English is the default, Russian is switchable in the
    options menu. Control, module and tab names stay English (technical terms the tooltips refer to);
    headings, status texts and explanations are translated. Keys may carry a context after "##". */
namespace mm::ui
{
enum class Language
{
    english,
    russian
};

inline std::atomic<int>& languageFlag()
{
    static std::atomic<int> lang { 0 };
    return lang;
}

inline void setLanguage (Language l) { languageFlag().store ((int) l); }
inline Language language() { return (Language) languageFlag().load(); }

const std::map<juce::String, juce::String>& russianTable();

inline juce::String tr (const juce::String& key)
{
    const auto english = key.upToFirstOccurrenceOf ("##", false, false);
    if (language() == Language::english)
        return english;
    const auto& t = russianTable();
    const auto it = t.find (key);
    if (it != t.end())
        return it->second;
    const auto it2 = t.find (english);
    return it2 != t.end() ? it2->second : english;
}

inline juce::String tr (const char* key) { return tr (juce::String (juce::CharPointer_UTF8 (key))); }
} // namespace mm::ui
