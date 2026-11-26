#pragma once

#include "dsp/Module.h"
#include "dsp/Oversampler.h"
#include "dsp/TruePeak.h"

#include <array>
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

private:
    double fs = 48000.0;
    DualOversampler os;
    double lastRate = 0.0;
    std::array<Cascade, 2> lowProt;
    Ramp thr, mix;
    Meter prMeter, inMeter;
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
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;
    int latency() const noexcept override { return lookahead + detector[0].latency(); }

    float gainReductionDb() const noexcept { return grMeter.get(); }

    /** Fills the per-sample gain reduction (dB) of the last block for the histogram. */
    const float* lastGainReduction() const noexcept { return grTrace.data(); }

private:
    /** Release-smoothed required gain -> sliding minimum (look-ahead + 1) -> moving average
        (look-ahead). The output gain never exceeds the requirement at a peak. */
    struct Pipe
    {
        std::vector<double> minVal, avgBuf;
        std::vector<int> minIdx;
        int head = 0, tail = 0, avgPos = 0;
        double avgSum = 0.0, release = 1.0, sustained = 0.0;
        void prepare (int capacity);
        void reset (int window);
        double process (double required, int index, int window, double relCoeff) noexcept;
    };

    double fs = 48000.0;
    int maxBlock = 512, lookahead = 96, maxLookahead = 480, sampleIndex = 0;
    std::array<TruePeakDetector8, 2> detector;
    std::array<std::vector<float>, 2> delayBuf;
    int delayPos = 0;
    std::array<Pipe, 2> pipes;
    double sustainedCoeff = 0.0;
    Ramp drive;
    Meter grMeter;
    std::vector<float> grTrace;
};
} // namespace mm::dsp
