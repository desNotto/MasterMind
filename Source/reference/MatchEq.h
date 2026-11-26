#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <vector>

namespace mm::reference
{
/** One proposed filter of the Match EQ (spec 7.3). */
struct EqSuggestion
{
    enum Type
    {
        bell,
        lowShelf,
        highShelf
    } type = bell;
    double freq = 1000.0, gainDb = 0.0, q = 1.0;
};

/** Proposes up to 8 bell or shelf filters (gain within +-3 dB, Q 0.5-2, 30 Hz-16 kHz) from the
    smoothed difference between our average spectrum and the target (both normalised, dB at
    spectrumFreqs()). `strength` (0..1) scales the proposal. Nothing is applied here. */
std::vector<EqSuggestion> suggestMatchEq (const std::vector<float>& ours, const std::vector<float>& target, double strength);

/** Writes the suggestions into free Dynamic EQ bands (shelves into bands 1 and 10, bells into
    2..9) and returns the previous values of the touched parameters for Undo. */
std::map<juce::String, float> applyMatchEq (juce::AudioProcessorValueTreeState& apvts, const std::vector<EqSuggestion>& s);
void undoMatchEq (juce::AudioProcessorValueTreeState& apvts, const std::map<juce::String, float>& previous);

/** Response of the suggestions in dB at f (for the preview curve). */
double matchEqResponseDb (const std::vector<EqSuggestion>& s, double fs, double f);
} // namespace mm::reference
