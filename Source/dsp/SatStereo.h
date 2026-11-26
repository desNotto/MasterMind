#pragma once

#include "dsp/Module.h"
#include "dsp/Oversampler.h"

#include <array>

namespace mm::dsp
{
/** 4.6 Three-band oversampled saturation. */
class Saturation final : public Module
{
public:
    struct Band
    {
        float driveDb = 0.0f;
        int type = 0; // Tube, Tape, Transformer, Diode
        float bias = 0.0f, mix = 1.0f, outDb = 0.0f;
    };
    struct Settings
    {
        float xo1 = 150.0f, xo2 = 3000.0f;
        std::array<Band, 3> bands;
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;
    int latency() const noexcept override { return os.latency(); }

    void setHighQuality (bool high) noexcept { os.setUseHigh (high); }

    static double shape (int type, double x, double bias) noexcept;

private:
    void updateSplit (double osRate) noexcept;

    double fs = 48000.0;
    int maxBlock = 512;
    DualOversampler os;
    double lastRate = 0.0;
    struct Channel
    {
        LinkwitzRiley xo1, xo2;
        Cascade ap2; // compensation of the low band for xo2
        Biquad dc;
    };
    std::array<Channel, 2> ch;
    struct BandRamp
    {
        Ramp drive, mix, out, bias;
    };
    std::array<BandRamp, 3> ramps;
};

/** 4.7 Width per band, mid/side balance and the correlation guard. */
class StereoStage final : public Module
{
public:
    struct Settings
    {
        float lowSplit = 120.0f; // follows the mono-maker frequency
        float split = 4000.0f;
        float midWidth = 1.0f, highWidth = 1.0f; // 0..2
        float balanceDb = 0.0f;
        bool guardOn = false;
        float guardThreshold = 0.0f;
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

    /** Width reduction applied by the guard per band (0..1, 1 = no reduction), for the meter. */
    float guardFactor (int band) const noexcept { return guardMeters[(size_t) band].get(); }

private:
    void updateSplits (double lo, double hi) noexcept;

    double fs = 48000.0;
    struct ThreeWay
    {
        LinkwitzRiley lo, hi;
        Cascade ap; // compensation of the low band for the high split
    };
    std::array<ThreeWay, 2> split; // mid, side
    FreqRamp lowFreq, highFreq;
    Ramp midW, highW, bal;
    std::array<double, 3> corrLR {}, corrLL {}, corrRR {};
    std::array<double, 3> guard { 1.0, 1.0, 1.0 };
    double corrCoeff = 0.0, guardCoeff = 0.0;
    std::array<Meter, 3> guardMeters;
};
} // namespace mm::dsp
