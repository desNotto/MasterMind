#pragma once

#include "dsp/Filters.h"
#include "dsp/TruePeak.h"

#include <array>
#include <deque>
#include <vector>

namespace mm::analysis
{
/** Histogram of loudness values in 0.01 dB bins from -70 to +10 LUFS, used for gating without
    storing every block (memory stays constant for songs of any length). */
class LoudnessHistogram
{
public:
    static constexpr double minDb = -70.0, maxDb = 10.0, step = 0.01;
    static constexpr int bins = (int) ((maxDb - minDb) / step) + 1;

    LoudnessHistogram() : counts ((size_t) bins, 0u) {}

    void clear() { std::fill (counts.begin(), counts.end(), 0u); total = 0; }
    void add (double lufs);

    /** BS.1770 integrated loudness with absolute (-70) and relative (relGate LU) gating. */
    double gatedMean (double relGate) const;

    /** EBU Tech 3342 loudness range: 95th minus 10th percentile after -20 LU relative gating. */
    double range() const;

    uint64_t count() const { return total; }

private:
    static double binDb (int i) { return minDb + i * step; }
    std::vector<uint32_t> counts;
    uint64_t total = 0;
};

/** ITU-R BS.1770-4 / EBU R128 loudness meter for a stereo signal, plus peak statistics.
    Not real-time safe in reset(); process() does not allocate. Runs on the analysis thread. */
class LoudnessMeter
{
public:
    void prepare (double sampleRate);
    void reset();

    void process (const float* left, const float* right, int numSamples);

    // Results (LUFS, -inf when there is no signal)
    double momentary() const { return momentaryLufs; }
    double shortTerm() const { return shortTermLufs; }
    double integrated() const;
    double loudnessRange() const { return stHistogram.range(); }

    double maxMomentary() const { return maxM; }
    double maxShortTerm() const { return maxS; }

    double truePeakDb() const;          // maximum since reset, dBTP
    double recentTruePeakDb() const;    // maximum over the last 3 s, dBTP
    double samplePeakDb() const;        // maximum since reset, dBFS
    double rmsDb() const;               // since reset, dBFS
    double crestDb() const { return samplePeakDb() - rmsDb(); }

    uint64_t sampleOvers() const { return overs; }
    uint64_t truePeakOvers() const { return tpOvers; }
    void setTruePeakCeiling (double dbtp) { tpCeiling = dsp::dbToGain (dbtp); }

    /** Short-term history: one value per 100 ms, newest last (for the history graph). */
    const std::deque<float>& shortTermHistory() const { return history; }

    /** Keeps every short-term value since reset (offline analysis of whole files). */
    void setKeepFullHistory (bool keep) { keepFull = keep; }
    const std::vector<float>& shortTermFullHistory() const { return fullHistory; }

    static double kWeightedOffset() { return -0.691; }

private:
    void finishSubBlock();

    double fs = 48000.0;
    std::array<dsp::Biquad, 2> pre, rlb; // per channel
    int subBlockLength = 4800, subBlockFill = 0;
    double subBlockSum = 0.0;
    std::array<double, 30> subBlocks {}; // 100 ms energies, newest at subIndex
    int subIndex = 0, subCount = 0;

    double momentaryLufs = -1000.0, shortTermLufs = -1000.0, maxM = -1000.0, maxS = -1000.0;
    LoudnessHistogram blockHistogram, stHistogram;

    std::array<dsp::TruePeakDetector, 2> tp;
    float tpMax = 0.0f, sampleMax = 0.0f;
    std::array<float, 30> tpSub {}; // per 100 ms sub-block maximum
    float tpSubCurrent = 0.0f;
    double sumSquares = 0.0;
    uint64_t samples = 0, overs = 0, tpOvers = 0;
    double tpCeiling = 1.0;
    std::deque<float> history;
    std::vector<float> fullHistory;
    bool keepFull = false;
};

/** BS.1770 K-weighting coefficients for any sample rate (exposed for tests). */
dsp::Coeffs kWeightingShelf (double fs);
dsp::Coeffs kWeightingHighpass (double fs);
} // namespace mm::analysis
