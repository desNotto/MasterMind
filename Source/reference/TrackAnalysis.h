#pragma once

#include "analysis/TapAnalyzer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace mm::reference
{
/** Whole-track measurements used for references (7.1) and album passports (7.4). */
struct TrackAnalysis
{
    juce::String name, path;
    double integrated = -100.0, lra = 0.0, plr = 0.0, maxTruePeak = -100.0, harshness = 0.0, subToBass = 0.0, lowCorrelation = 1.0;
    std::array<double, analysis::numTonalBands> tonal {}, crest {};
    std::array<double, analysis::numStereoBands> width {}, correlation {};
    /** Band power per 1/12 octave in dB at spectrumFreqs(), normalised so that the total energy
        20 Hz - 16 kHz is 0 dB (flat for pink noise). */
    std::vector<float> spectrum;
    std::vector<float> shortTerm; // LUFS at 10 Hz
    double durationSeconds = 0.0;
    juce::String date, pluginVersion;

    juce::var toJson() const;
    static std::optional<TrackAnalysis> fromJson (const juce::var& v);
};

/** Fixed log-frequency grid of the stored spectra (240 points, 20 Hz - 20 kHz). */
const std::vector<double>& spectrumFreqs();

/** Converts the state of an analyzer (fed with a whole track) into a TrackAnalysis. */
TrackAnalysis fromAnalyzer (const analysis::TapAnalyzer& a, double durationSeconds);

/** Normalised band-power spectrum at spectrumFreqs() from an analyzer (for live comparisons). */
std::vector<float> normalisedSpectrum (const analysis::TapAnalyzer& a);

/** Decodes and analyses an audio file (WAV, FLAC, MP3, OGG). Runs on a worker thread.
    `progress` gets 0..1; `cancelled` is polled. Returns nullopt with `error` set on failure.
    With toSeconds > fromSeconds only that part of the song is analysed (the metrics region, D-041). */
std::optional<TrackAnalysis> analyseFile (const juce::File& file, std::function<void (float)> progress,
                                          const std::atomic<bool>& cancelled, juce::String& error,
                                          double fromSeconds = 0.0, double toSeconds = -1.0);

/** Decodes a file into memory at the target sample rate (for reference playback). */
bool decodeForPlayback (const juce::File& file, double targetRate, juce::AudioBuffer<float>& out, juce::String& error,
                        const std::atomic<bool>& cancelled);

juce::AudioFormatManager& formatManager();
} // namespace mm::reference
