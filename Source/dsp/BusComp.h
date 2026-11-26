#pragma once

#include "dsp/Module.h"

#include <array>

namespace mm::dsp
{
/** 4.5 Bus compressor (VCA: feed-forward clean glue; Punch: feedback detector, faster). */
class BusComp final : public Module
{
public:
    struct Settings
    {
        int model = 0;
        float thresholdDb = -12.0f, ratio = 2.0f, attackMs = 10.0f, releaseMs = 200.0f;
        bool releaseAuto = true;
        bool hardKnee = false;
        float scHpf = 100.0f, mix = 1.0f, rmsAmount = 0.5f, link = 1.0f;
        bool autoMakeup = false;
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

    float gainReductionDb() const noexcept { return grMeter.get(); }

    /** Static curve output level for an input level (dB), for the UI transfer graph. */
    static double staticCurveDb (const Settings& s, double inDb);
    static double makeupDb (const Settings& s);

private:
    double fs = 48000.0;
    std::array<Biquad, 2> sc;
    std::array<LevelDetector, 2> det;
    std::array<GainSmoother, 2> smooth;
    std::array<double, 2> fbOut {};
    FreqRamp scFreq;
    Ramp mix, makeup;
    Meter grMeter;
};
} // namespace mm::dsp
