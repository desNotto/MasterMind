#pragma once

#include <juce_core/juce_core.h>

#include <array>
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

/** Advice for a tonal balance band above (up) or below (dn) the target (mockup v7). */
struct TonalAdvice
{
    const char* upHeard;
    std::vector<const char*> upSteps;
    const char* dnHeard;
    std::vector<const char*> dnSteps;
};

const std::vector<ModuleText>& modules();
const std::vector<KnobText>& knobs();
const std::vector<ControlText>& controls();
const std::vector<TonalAdvice>& tonalAdvice();

/** Multiband advice per band (mockup v12): knob tooltip id ("mb.thresh") and one text per band
    (Low, Lo-mid, Mid, Hi-mid, High). */
struct BandAdvice
{
    const char* id;
    std::array<const char*, 5> perBand;
};
const std::vector<BandAdvice>& multibandAdvice();

inline juce::String u (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
} // namespace mm::ui::tips
