#pragma once

#include "dsp/Filters.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace mm::monitoring
{
/** One filter of a correction profile or a system emulation. */
struct FilterSpec
{
    enum Type
    {
        peak,
        lowShelf,
        highShelf,
        lowPass,
        highPass
    } type = peak;
    double freq = 1000.0, gainDb = 0.0, q = 0.71;
    int order = 2; // for low/high pass
};

/** Parametric EQ text in the AutoEQ / REW export format:
        Preamp: -6.2 dB
        Filter 1: ON PK Fc 105 Hz Gain -3.2 dB Q 1.41
        Filter 2: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70
    Unknown or disabled lines are skipped. Returns false (with a message) if nothing usable. */
struct ParametricProfile
{
    double preampDb = 0.0;
    std::vector<FilterSpec> filters;
    juce::String name;
};
bool parseParametric (const juce::String& text, ParametricProfile& out, juce::String& error);

/** System emulation preset (spec 6.2). */
struct SystemPreset
{
    juce::String name;
    std::vector<FilterSpec> filters;
    bool mono = false;
    bool softLimit = false;
};

/** Built-in starting points of the six emulations. */
std::vector<SystemPreset> defaultSystemPresets();

/** JSON round trip (presets are editable as JSON files). */
juce::var systemPresetToJson (const SystemPreset& p);
bool systemPresetFromJson (const juce::var& v, SystemPreset& out);

/** Loads all presets from the translation folder, writing the defaults first if it is empty.
    Never throws; corrupt files are skipped and reported in `errors`. */
std::vector<SystemPreset> loadSystemPresets (const juce::File& folder, juce::StringArray& errors);

/** Coefficients for a filter spec at a sample rate (low/high pass may produce several sections). */
std::vector<dsp::Coeffs> design (const FilterSpec& f, double fs);
} // namespace mm::monitoring
