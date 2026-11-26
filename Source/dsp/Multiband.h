#pragma once

#include "dsp/Module.h"

#include <juce_dsp/juce_dsp.h>

#include <array>

namespace mm::dsp
{
/** 4.3 Five-band dynamics with Linkwitz-Riley or linear-phase crossovers. */
class Multiband final : public Module
{
public:
    static constexpr int numBands = 5;

    struct Band
    {
        float thresholdDb = -20.0f, ratio = 1.5f, attackMs = 20.0f, releaseMs = 200.0f;
        bool releaseAuto = false;
        float kneeDb = 6.0f, makeupDb = 0.0f, mix = 1.0f, link = 1.0f;
        int mode = 0; // stereo, mid/side
        bool solo = false, bypass = false;
        float trimDb = 0.0f;
    };

    struct Settings
    {
        bool linearPhase = false;
        std::array<float, 4> crossovers { 80.0f, 250.0f, 1500.0f, 5500.0f };
        std::array<Band, numBands> bands;
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;
    int latency() const noexcept override { return settings.linearPhase ? firLength / 2 : 0; }

    float gainReductionDb (int band) const noexcept { return grMeters[(size_t) band].get(); }

    /** Builds linear-phase crossover kernels (allocates; worker thread only). */
    void buildLinearPhase (const Settings& snapshot);

    /** Sorted crossover frequencies with a minimum spacing. */
    static std::array<double, 4> sanitise (const std::array<float, 4>& xo, double fs);

private:
    void updateCrossovers (const std::array<double, 4>& xo) noexcept;
    void splitMinimumPhase (int ch, float* x, int len) noexcept;
    void splitLinearPhase (int ch, float* x, int len) noexcept;
    void compressBands (int len) noexcept;

    double fs = 48000.0;
    int maxBlock = 512, firLength = 4096;

    struct Channel
    {
        std::array<LinkwitzRiley, 4> xo;
        std::array<Cascade, 6> ap; // b1: ap2,ap3,ap4 (0..2), b2: ap3,ap4 (3..4), b3: ap4 (5)
    };
    std::array<Channel, 2> chans;
    std::array<FreqRamp, 4> xoRamp;

    struct BandDyn
    {
        std::array<GainSmoother, 2> smooth;
        Ramp makeup, trim, mix;
    };
    std::array<BandDyn, numBands> dyn;
    std::array<Meter, numBands> grMeters;

    juce::AudioBuffer<float> bandBuf; // [ch * numBands + band]
    juce::AudioBuffer<float> scratch;
    std::array<juce::dsp::Convolution, 8> conv; // [ch * 4 + k] low-pass k
    DelayLine linDelay;
};
} // namespace mm::dsp
