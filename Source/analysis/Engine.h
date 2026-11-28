#pragma once

#include "analysis/Checks.h"
#include "analysis/TapAnalyzer.h"
#include "reference/TrackAnalysis.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <vector>

namespace mm::analysis
{
/** Gain-reduction histogram for clipper and limiter (spec 5.9). */
class GrHistogram
{
public:
    static constexpr int bins = 96;          // 0.25 dB steps up to 24 dB
    static constexpr double binWidth = 0.25;

    void reset();
    void add (float grDb); // grDb <= 0
    void tickSecond();
    /** Counts over the last `seconds` (0 = whole song). */
    std::array<uint64_t, bins> counts (int seconds) const;

private:
    std::array<uint64_t, bins> whole {};
    std::array<std::array<uint32_t, bins>, 60> perSecond {};
    int current = 0, filledSeconds = 0;
};

/** Everything the UI draws, copied from the analysis thread about 30 times per second. */
struct TapView
{
    double momentary = -100, shortTerm = -100, integrated = -100, lra = 0, maxMomentary = -100, maxShortTerm = -100;
    double truePeak = -100, recentTruePeak = -100, samplePeak = -100, crest = 0, plr = 0, psr = 0;
    uint64_t sampleOvers = 0, truePeakOvers = 0;
    std::vector<float> history; // short-term, 10 Hz, last 60 s

    std::vector<float> specRt, specAvg, specPeak; // display dB at Snapshot::freqs
    std::array<double, numTonalBands> tonalIntegrated {}, tonalRecent {}, crest6 {};
    double harshness = -100;
    std::vector<float> harshHistory;

    TapAnalyzer::StereoBand broadShort, broadIntegrated;
    std::array<TapAnalyzer::StereoBand, numStereoBands> bandsShort, bandsIntegrated;
    double balanceDb = 0.0;
    std::vector<std::pair<float, float>> scope;

    std::vector<float> lowSpec;  // dB at Snapshot::lowFreqs (selected channel)
    std::vector<float> lowMid, lowSide; // dB, mid and side at lowFreqs
    std::vector<float> lowWave;
    double asymmetry = 0, subToBass = 0;
    TapAnalyzer::StereoBand lowMono;
    bool hasSignal = false;
};

struct Snapshot
{
    uint64_t serial = 0;
    double sampleRate = 48000.0;
    std::vector<double> freqs, lowFreqs;
    TapView in, out;
    std::array<uint64_t, GrHistogram::bins> clipHist {}, limHist {};
    std::vector<Check> checks;
    Targets targets;
    uint64_t preLimiterSampleOvers = 0, preLimiterTpOvers = 0;
    uint64_t droppedFrames = 0;
};

/** Background analysis of the IN and OUT taps (spec section 3 and 5). The audio thread only
    writes into a lock-free FIFO; all analysis runs on this object's thread. */
class Engine : private juce::Thread
{
public:
    Engine();
    ~Engine() override;

    /** Called from prepareToPlay (not real-time). */
    void prepare (double sampleRate);
    void shutdown();

    /** Audio thread: pushes one block of both taps plus per-sample gain reduction (dB, <= 0) of the
        clipper and limiter. Never blocks; drops frames when the FIFO is full. */
    void push (const float* inL, const float* inR, const float* outL, const float* outR, float clipGrDb,
               const float* limGr, int numSamples) noexcept;

    /** Any thread: request an analysis reset (Reset button, transport start). */
    void requestReset() noexcept { resetRequested.store (true); }

    /** UI thread: copies the latest snapshot. Returns false if nothing new since `lastSerial`. */
    bool copySnapshot (Snapshot& dest, uint64_t lastSerial) const;

    /** UI thread: copies spectrogram columns of a tap newer than `sinceIndex`. */
    uint64_t copySpectrogram (bool fromOut, uint64_t sinceIndex, std::vector<std::array<float, TapAnalyzer::spectrogramBins>>& out) const;
    double spectrogramColumnRate() const { return colRate.load(); }

    // Settings written by the UI or processor (atomic)
    std::atomic<int> fftOrder { 13 }, smoothing { 3 }, channel { 4 }, lowChannel { 2 }, grSeconds { 0 };
    std::atomic<float> tilt { 4.5f }, monoFreq { 120.0f }, ceiling { -1.0f };
    std::atomic<bool> tonalLast10s { false };
    std::atomic<uint64_t> preLimiterSampleOvers { 0 }, preLimiterTpOvers { 0 };
    // published for monitoring (loudness-matched A/B and reference matching)
    std::atomic<float> inShortTermLufs { -100.0f }, outShortTermLufs { -100.0f }, outIntegratedLufs { -100.0f };

    /** Passport of the OUT tap since the last reset (album, spec 7.4). Any thread except audio. */
    reference::TrackAnalysis makePassport (const juce::String& trackName);
    /** Normalised average OUT spectrum for reference comparisons and Match EQ. */
    std::vector<float> outSpectrumNormalised();

    void setTargets (const Targets& t);
    Targets getTargets() const;

    /** Auto match (D-040): records the plugin input on the analysis thread (never the audio thread).
        Silent chunks are skipped, so the recording waits for playback. UI thread. */
    void startCapture (double seconds);
    void cancelCapture();
    /** 0..1 while recording, 1 when the recording is complete. */
    float captureProgress() const noexcept { return captureTotal > 0 ? (float) captureFill.load() / (float) captureTotal : 0.0f; }
    /** Moves the completed recording out (empty buffer if not complete). UI thread. */
    juce::AudioBuffer<float> takeCapture();
    double getSampleRate() const noexcept { return fs; }
    void setThresholds (const Thresholds& t);

private:
    void run() override;
    void buildSnapshot();
    void fillView (TapAnalyzer& a, TapView& v, std::vector<float>& rtState);

    static constexpr int frameWidth = 6;
    static constexpr int capacity = 1 << 18;
    juce::AbstractFifo fifo { capacity };
    std::vector<float> fifoData;
    std::atomic<uint64_t> dropped { 0 };
    std::atomic<bool> resetRequested { false }, prepared { false };

    double fs = 48000.0;
    TapAnalyzer inTap, outTap;
    juce::AudioBuffer<float> capture;
    std::atomic<int> captureFill { 0 };
    int captureTotal = 0;
    std::atomic<bool> capturing { false };
    GrHistogram clipHist, limHist;
    int samplesToSecond = 0, currentOrder = 13;
    uint64_t samplesSinceReset = 0;
    std::vector<float> rtIn, rtOut;
    double lastBuild = 0.0;

    mutable juce::SpinLock snapLock;
    Snapshot snap;
    uint64_t serial = 0;

    mutable juce::SpinLock sgLock;
    std::vector<std::array<float, TapAnalyzer::spectrogramBins>> sgIn, sgOut; // ring
    uint64_t sgInCount = 0, sgOutCount = 0;
    size_t sgInSeen = 0, sgOutSeen = 0;
    std::atomic<double> colRate { 25.0 };

    mutable juce::SpinLock targetLock;
    Targets targets;
    Thresholds thresholds;
    juce::CriticalSection prepareLock;
};
} // namespace mm::analysis
