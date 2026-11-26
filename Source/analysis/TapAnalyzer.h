#pragma once

#include "analysis/Loudness.h"
#include "dsp/Filters.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <deque>
#include <memory>
#include <vector>

namespace mm::analysis
{
/** Spectrum channels (spec 5.1). */
enum class SpecChannel
{
    left,
    right,
    mid,
    side,
    stereo
};
inline constexpr int numSpecChannels = 5;

/** The six tonal bands (spec 5.2). */
inline constexpr int numTonalBands = 6;
inline constexpr double tonalEdges[numTonalBands + 1] = { 20.0, 60.0, 150.0, 500.0, 2000.0, 6000.0, 16000.0 };

/** Stereo analysis bands (spec 5.5). */
inline constexpr int numStereoBands = 4;
inline constexpr double stereoEdges[numStereoBands - 1] = { 150.0, 500.0, 4000.0 };

/** Settings of the spectrum display, written by the UI. */
struct SpectrumSettings
{
    int fftOrder = 13;      // 12, 13, 14 -> 4096, 8192, 16384
    int smoothing = 3;      // 0 none, 1: 1/12, 2: 1/6, 3: 1/3, 4: 1 octave
    double tilt = 4.5;      // dB/oct, pivot 1 kHz
    int channel = 4;        // SpecChannel
};

/** Energy accumulators for one stereo stream: spectrum (average, peak, real time), tonal bands,
    stereo correlation and width per band, per-band crest factor, low end, harshness. Used for the
    live IN and OUT taps and for offline analysis of reference files and album passports. */
class TapAnalyzer
{
public:
    TapAnalyzer();
    ~TapAnalyzer();

    void prepare (double sampleRate, int fftOrder);
    void reset();
    void process (const float* left, const float* right, int numSamples);

    /** Offline mode: skips the spectrogram, vectorscope and waveform, keeps the full loudness history. */
    void setLightweight (bool light) { lightweight = light; meter.setKeepFullHistory (light); }

    // ---- Loudness (5.3) --------------------------------------------------------------------------
    LoudnessMeter& loudness() { return meter; }
    const LoudnessMeter& loudness() const { return meter; }

    // ---- Spectrum (5.1) ---------------------------------------------------------------------------
    int fftSize() const { return n; }
    double sampleRate() const { return fs; }
    /** Per-bin power (mean square contribution) for the channel: average since reset, peak hold,
        and the most recent frame. */
    const std::vector<float>& averagePower (SpecChannel c) const { return avg[(size_t) c]; }
    const std::vector<float>& peakPower (SpecChannel c) const { return peak[(size_t) c]; }
    const std::vector<float>& framePower (SpecChannel c) const { return frame[(size_t) c]; }
    uint64_t frameCount() const { return frames; }

    // ---- Tonal balance (5.2) ---------------------------------------------------------------------
    /** Band energy share in dB (band energy / total 20 Hz - 16 kHz), integrated since reset. */
    std::array<double, numTonalBands> tonalIntegrated() const;
    /** Same over the last 10 seconds. */
    std::array<double, numTonalBands> tonalRecent() const;

    // ---- Harshness (5.7) and crest per band (5.8) -------------------------------------------------
    double harshnessDb() const;                 // integrated, 3-8 kHz vs 0.5-2 kHz
    const std::deque<float>& harshnessHistory() const { return harshHistory; } // 1 per second, 30 s
    std::array<double, numTonalBands> crestDb() const;

    // ---- Stereo (5.5) ------------------------------------------------------------------------------
    struct StereoBand
    {
        double correlation = 1.0, widthPercent = 0.0, monoLossDb = 0.0;
    };
    StereoBand stereoBroadband (bool shortTerm) const;
    StereoBand stereoBand (int band, bool shortTerm) const;
    double balanceDb() const; // L/R level difference, positive = right louder
    /** Recent (mid, side) points for the vectorscope, newest last. */
    void copyScope (std::vector<std::pair<float, float>>& out) const;

    // ---- Low end (5.4) ------------------------------------------------------------------------------
    double lowRate() const { return lowFs; }
    int lowFftSize() const { return lowN; }
    const std::vector<float>& lowAveragePower (SpecChannel c) const { return lowAvg[(size_t) c]; }
    const std::vector<float>& lowFramePower (SpecChannel c) const { return lowFrame[(size_t) c]; }
    /** 20-150 Hz band of the mid signal at the low rate, last 2 s, newest last. */
    void copyLowWave (std::vector<float>& out) const;
    double peakAsymmetryPercent() const;
    void setMonoFrequency (double hz) { monoFreq = hz; }
    StereoBand lowBelowMono() const;
    double subToBassDb() const; // energy 20-60 vs 60-120 Hz

    // ---- Spectrogram (5.6) --------------------------------------------------------------------------
    static constexpr int spectrogramBins = 256;
    /** Columns of log-frequency levels (dB, mid signal), about 25 per second. */
    const std::deque<std::array<float, spectrogramBins>>& spectrogram() const { return specgram; }
    double spectrogramColumnRate() const { return fs / sgHop; }
    uint64_t spectrogramTotal() const { return sgTotal; }
    void setSpectrogramSeconds (double s) { sgSeconds = std::clamp (s, 1.0, 60.0); }

private:
    void analyseFrame();
    void analyseLowFrame();
    void analyseSpectrogramFrame();

    double fs = 48000.0;
    LoudnessMeter meter;

    // main FFT
    int order = 13, n = 8192, hop = 2048, fill = 0;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, ringL, ringR, work;
    int ringPos = 0;
    double windowPower = 1.0;
    std::array<std::vector<float>, numSpecChannels> avg, peak, frame;
    std::array<std::vector<double>, numSpecChannels> sum;
    uint64_t frames = 0;

    // tonal and harshness energies (stereo channel)
    std::array<double, numTonalBands> tonalSum {};
    double tonalTotal = 0.0;
    struct SecondBucket
    {
        std::array<double, numTonalBands> bands {};
        double total = 0.0, harshHi = 0.0, harshLo = 0.0;
        int frames = 0;
    };
    std::deque<SecondBucket> recent; // 1 s buckets, last 10 s
    SecondBucket currentSecond;
    int samplesThisSecond = 0;
    double harshHiSum = 0.0, harshLoSum = 0.0;
    std::deque<float> harshHistory;

    // crest per tonal band (time domain filter bank on mid)
    struct CrestBand
    {
        dsp::Cascade hp, lp;
        double peak = 0.0, sumSq = 0.0;
        uint64_t count = 0;
    };
    std::array<CrestBand, numTonalBands> crest;

    // stereo per band: LR4 split of L and R
    struct StereoAcc
    {
        double ll = 0, rr = 0, lr = 0, mm = 0, ss = 0; // integrated sums
        double sll = 0, srr = 0, slr = 0, smm = 0, sss = 0; // short-term (300 ms) averages
    };
    std::array<std::array<dsp::LinkwitzRiley, numStereoBands - 1>, 2> stereoSplit;
    std::array<std::array<dsp::Cascade, numStereoBands - 1>, 2> stereoAp;
    std::array<StereoAcc, numStereoBands> stereoAcc;
    StereoAcc broad;
    double shortCoeff = 0.0;
    double sumL2 = 0.0, sumR2 = 0.0;
    std::vector<std::pair<float, float>> scope;
    int scopePos = 0, scopeDecim = 1, scopeCounter = 0;

    // low end: decimated stream
    int decim = 32, decimCounter = 0, lowN = 2048, lowFill = 0, lowRingPos = 0;
    double lowFs = 1500.0, monoFreq = 120.0;
    std::array<dsp::Cascade, 2> antiAlias;
    std::unique_ptr<juce::dsp::FFT> lowFft;
    std::vector<float> lowWindow, lowRingL, lowRingR, lowWork;
    double lowWindowPower = 1.0;
    std::array<std::vector<float>, numSpecChannels> lowAvg, lowFrame;
    std::array<std::vector<double>, numSpecChannels> lowSum;
    uint64_t lowFrames = 0;
    dsp::Cascade waveBand;
    std::vector<float> wave;
    int wavePos = 0;
    std::array<dsp::Cascade, 2> monoLp;
    double monoForFilter = 0.0;
    StereoAcc lowAcc;

    // spectrogram
    static constexpr int sgOrder = 12;
    int sgN = 4096, sgHop = 1920, sgFill = 0;
    double sgSeconds = 20.0;
    std::unique_ptr<juce::dsp::FFT> sgFft;
    std::vector<float> sgWindow, sgRing, sgWork;
    int sgRingPos = 0;
    std::array<std::pair<int, int>, spectrogramBins> sgBinRanges {};
    std::deque<std::array<float, spectrogramBins>> specgram;
    uint64_t sgTotal = 0;
    bool lightweight = false;
};

/** Converts per-bin power of an FFT into display points on a log frequency axis, with
    fractional-octave smoothing and tilt. Returns dB values. */
void powerToDisplay (const std::vector<float>& power, double fs, int fftSize, const std::vector<double>& freqs,
                     int smoothing, double tiltDbPerOct, std::vector<float>& outDb);

/** Default tonal target: a spectrum falling 4.5 dB/oct per FFT bin (the spectrum tilt), expressed
    as band shares in dB. See docs/DECISIONS.md (D-016). */
std::array<double, numTonalBands> defaultTonalTarget();
double defaultHarshnessTarget();
} // namespace mm::analysis
