#pragma once

#include "dsp/Module.h"

#include <array>
#include <random>

namespace mm::dsp
{
/** 4.1 Input trim, polarity, DC blocker and subsonic high-pass. */
class InputStage final : public Module
{
public:
    struct Settings
    {
        float trimDb = 0.0f;
        bool polarityL = false, polarityR = false;
        bool hpfOn = false;
        float hpfFreq = 25.0f;
        int hpfSlope = 1; // 0: 12, 1: 24, 2: 48 dB/oct
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

    /** Magnitude of the subsonic filter at f (for the spectrum overlay). */
    double hpfMagnitudeDb (double f) const;

private:
    void updateFilters (double freq) noexcept;

    double fs = 48000.0;
    Ramp gain;
    FreqRamp freq;
    int slope = -1;
    std::array<Cascade, 2> hpf;
    std::array<double, 2> dcX {}, dcY {};
    double dcR = 0.0;
    bool hpfWasOn = false;
};

/** 4.10 Output gain and dither (dither is the last step of the processing chain). */
class OutputStage final : public Module
{
public:
    struct Settings
    {
        float gainDb = 0.0f;
        int dither = 0; // 0 off, 1 TPDF, 2 noise-shaped
        int bits = 1;   // 0: 16, 1: 24
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

private:
    Ramp gain;
    std::minstd_rand rng { 12345 };
    std::array<std::array<double, 2>, 2> err {};
};
} // namespace mm::dsp
