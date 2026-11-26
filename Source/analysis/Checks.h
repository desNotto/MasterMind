#pragma once

#include "analysis/TapAnalyzer.h"

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace mm::analysis
{
enum class Light
{
    ok,
    warn,
    bad,
    info
};

/** Traffic-light thresholds (settings.json, spec 5 common rules). */
struct Thresholds
{
    double tonalGreen = 1.0, tonalYellow = 2.5;          // dB vs target
    double monoLossGreen = -1.0, monoLossYellow = -3.0;   // dB
    double lowCorrGreen = 0.8, lowCorrYellow = 0.5;
    double plrGreen = 8.0, plrYellow = 6.0;               // LU
    double harshGreen = 1.5, harshYellow = 3.0;           // dB vs target
    double tpYellowMargin = 0.5;                          // dB over the ceiling
};

/** Targets for the checks: tonal shares (dB), corridor half-widths, harshness, loudness. */
struct Targets
{
    std::array<double, numTonalBands> tonal = defaultTonalTarget();
    std::array<double, numTonalBands> corridor { 1.5, 1.5, 1.5, 1.5, 1.5, 1.5 };
    double harshness = defaultHarshnessTarget();
    double integratedLufs = -14.0;
    double truePeakCeiling = -1.0;
    juce::String name = "Default (4.5 dB/oct)";
    int referenceCount = 0;
};

struct Check
{
    Light light = Light::ok;
    juce::String title, value, detail, tooltip;
};

/** Values needed by the checks, taken from the OUT tap. */
struct CheckInput
{
    std::array<double, numTonalBands> tonal {};
    double monoLossDb = 0.0, lowCorrelation = 1.0, plr = 0.0, harshness = 0.0;
    double truePeak = -100.0, integrated = -100.0;
    bool hasSignal = false;
};

std::vector<Check> computeChecks (const CheckInput& in, const Targets& t, const Thresholds& th);

/** Plain-language tonal deviation label, e.g. "Sub +2.4 dB vs target". */
juce::String tonalLabel (int band, double deviation);
const char* tonalBandName (int band);
const char* tonalBandRange (int band);
} // namespace mm::analysis
