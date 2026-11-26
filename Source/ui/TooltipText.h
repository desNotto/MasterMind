#pragma once

#include <juce_core/juce_core.h>

#include <vector>

/** Approved Russian tooltip texts (mockup v4, D-011). Plain UTF-8 literals. */
namespace mm::ui::tips
{
struct ModuleText
{
    const char* key;
    const char* name;
    const char* text;
};

struct KnobText
{
    const char* id;
    const char* label;
    const char* range;
    const char* text;
    const char* kind; // "Совет" or "Внимание"
    const char* tip;
};

struct ControlText
{
    const char* id;
    const char* title;
    const char* values;
    const char* text;
    const char* kind;
    const char* tip;
    const char* gesture;
};

const std::vector<ModuleText>& modules();
const std::vector<KnobText>& knobs();
const std::vector<ControlText>& controls();

inline juce::String u (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
} // namespace mm::ui::tips
