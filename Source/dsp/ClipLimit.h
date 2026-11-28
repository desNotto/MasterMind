#pragma once

#include "dsp/Module.h"
#include "dsp/Oversampler.h"
#include "dsp/BandClipper.h"
#include "dsp/Filters.h"
#include "dsp/SpectralBank.h"
#include "dsp/TruePeak.h"

#include <array>
#include <memory>
#include <vector>

namespace mm::dsp
{
/** 4.8 Oversampled clipper (4x normal, 8x high) with low-band protection. */
class Clipper final : public Module
{
public:
    struct Settings
    {
        int type = 1; // Hard, Soft (tanh), Soft knee, Diode
        float thresholdDb = -1.0f, shape = 0.5f, mix = 1.0f;
        bool lowProtect = true;
        float driveDb = 0.0f; // gain before clipping (mockup v15)
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;
    int latency() const noexcept override { return os.latency(); }
    void setHighQuality (bool high) noexcept { os.setUseHigh (high); }

    /** Peak reduction of the last block in dB (<= 0) and per-sample reduction for the histogram. */
    float peakReductionDb() const noexcept { return prMeter.get(); }
    /** Input peak of the last block in dBFS (oversampled, before clipping). */
    float inputPeakDb() const noexcept { return inMeter.get(); }

    static double clip (int type, double x, double t, double shape) noexcept;

    /** Waveform for the UI (single producer: audio thread, single consumer: message thread):
        mono input after Drive and mono output at the base rate. The output lags the input by
        latency() samples. Returns the number of samples read. */
    int readCapture (float* in, float* out, int maxSamples) noexcept;

private:
    double fs = 48000.0;
    DualOversampler os;
    double lastRate = 0.0;
    std::array<Cascade, 2> lowProt;
    Ramp thr, mix, drive;
    Meter prMeter, inMeter;
    static constexpr int captureSize = 16384;
    juce::AbstractFifo capture { captureSize };
    std::vector<float> capIn, capOut, scratchIn;
};

/** 4.9 True-peak-safe look-ahead limiter with a fast transient stage and a slower program stage. */
class Limiter final : public Module
{
public:
    struct Settings
    {
        float lookaheadMs = 2.0f, releaseMs = 100.0f;
        bool releaseAuto = true;
        int mode = 1; // Transparent, Balanced, Aggressive
        float driveDb = 0.0f, ceilingDb = -1.0f, link = 1.0f, transient = 0.0f;
        int bands = 0;             // 0 Classic, 1: 8, 2: 16, 3: 26 bands (D-037, structural)
        float adaptGainDb = 6.0f, adaptSpeed = 0.7f, adaptTrans = 0.5f;
        // D-042: spectral clipper before the limiter (dB off the band peaks), slow first stage
        // (0..1: share of up to 3 dB of reduction), transient softening of the bands (0..1)
        float clipDb = 0.0f, leveler = 0.0f, soften = 0.0f;
        // density: how far (0..1 of 12 dB) the fast stage may release below the slow stage
        float density = 0.0f;
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;
    /** Cached at the end of reset(): other threads may ask while prepare() rebuilds the stages. */
    int latency() const noexcept override { return latencyCache.load (std::memory_order_relaxed); }

    /** The spectral band stage (nullptr in Classic or inside the safety stage). */
    const SpectralBank* spectralBank() const noexcept { return spectral() ? bank.get() : nullptr; }
    bool spectral() const noexcept { return activeBands > 0 && bank != nullptr; }

    float gainReductionDb() const noexcept { return grMeter.get(); }
    /** Spectral clipper peak reduction and slow stage reduction of the last block, dB (>= 0). */
    float clipReductionDb() const noexcept { return bandClip != nullptr ? bandClip->reductionDb() : 0.0f; }
    float levelerReductionDb() const noexcept { return levelMeter.get(); }
    /** Peak of the last block in dBFS after Drive (before limiting) and at the output. */
    float inputPeakDb() const noexcept { return inMeter.get(); }
    float outputPeakDb() const noexcept { return outMeter.get(); }

    /** Fills the per-sample gain reduction (dB) of the last block for the histogram. */
    const float* lastGainReduction() const noexcept { return grTrace.data(); }

private:
    /** Sliding minimum over the last span + 1 samples (monotonic deque, O(1) amortized). */
    struct MinWindow
    {
        std::vector<double> val;
        std::vector<int> idx;
        int head = 0, tail = 0;
        void prepare (int capacity);
        void reset();
        double push (double v, int index, int span) noexcept;
        void shift (int by) noexcept;
    };

    /** Moving average over len samples. */
    struct Boxcar
    {
        std::vector<double> buf;
        int pos = 0, len = 1;
        double sum = 0.0;
        void prepare (int capacity);
        void reset (int length);
        double push (double v) noexcept;
    };

    /** Gain computer of one channel (D-036): hold -> fast release (transient stage) and a slow
        program-dependent stage (the deeper of the two wins) -> look-ahead minimum -> two cascaded
        moving averages (smooth S-shaped attack). The gain never exceeds the requirement at a peak. */
    struct Pipe
    {
        MinWindow hold, look;
        Boxcar box1, box2;
        double fastDb = 0.0, slowDb = 0.0;
        void prepare (int holdCapacity, int lookCapacity);
        void reset (int window);
    };

    struct Coeffs
    {
        int holdSamples = 0;
        double fastRel = 0.0, slowAtt = 0.0, slowRel = 0.0, densityDb = 0.0;
        bool slow = true;
    };
    double processPipe (Pipe& p, double required, const Coeffs& k) const noexcept;

    void processCore (float* const* det, float* const* app, float* const* out, int numSamples, bool applyDrive) noexcept;
    /** Drive, the slow first stage and the spectral clipper into `driven` (D-042). */
    float preStages (const float* left, const float* right, int numSamples) noexcept;

    // spectral mode: band stage, the limiter core, a clipper for the emphasised attacks and a short
    // true-peak safety stage (a nested Limiter in Classic mode)
    std::unique_ptr<SpectralBank> bank;
    std::unique_ptr<Clipper> clip;
    std::unique_ptr<Limiter> safety;
    std::unique_ptr<BandClipper> bandClip;
    double levelDb = 0.0;
    bool coreTransientRelease = true; // Transients shortens the release in Classic only
    Meter levelMeter;
    std::array<std::vector<float>, 2> driven, shaped, full, proc;
    // The clipper and the safety stage only matter with attack emphasis (Transients > 0). Without it
    // a delay line of the same length replaces them (about half the CPU); switching crossfades.
    DelayLine tailBypass;
    enum class Tail { off, warming, fadeIn, on, draining, fadeOut } tail = Tail::off;
    int tailCount = 0;
    int activeBands = 0;
    bool nested = false;
    std::atomic<int> latencyCache { 0 };

    double fs = 48000.0;
    int maxBlock = 512, lookahead = 96, maxLookahead = 480, maxHold = 2400, sampleIndex = 0;
    std::array<TruePeakDetector8, 2> detector;
    std::array<std::vector<float>, 2> delayBuf;
    int delayPos = 0;
    std::array<Pipe, 2> pipes;
    Ramp drive;
    Meter grMeter, inMeter, outMeter;
    std::vector<float> grTrace;
};
} // namespace mm::dsp
