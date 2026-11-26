#pragma once

#include "dsp/Module.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace mm::dsp
{
/** Resonance suppressor (spec 4.2, optional): STFT-based dynamic cut of narrow peaks that stick
    out of the smoothed spectrum, between a low and a high frequency. Runs after the Dynamic EQ
    bands. Latency: one FFT frame (reported while it is on). */
class ResonanceSuppressor
{
public:
    struct Settings
    {
        bool on = false;
        float lowHz = 150.0f, highHz = 12000.0f;
        float depthDb = 6.0f;   // 0..20
        float sharpness = 0.5f; // 0..1: higher = only narrower peaks
        float speed = 0.5f;     // 0..1: higher = faster attack/release
        float mix = 1.0f;
        bool delta = false;     // listen to what is removed
    } settings;

    void prepare (double sampleRate, int maxBlock);
    void reset();
    void process (float* left, float* right, int numSamples) noexcept;
    int latency() const noexcept { return settings.on ? frameSize : 0; }

    /** Strongest reduction of the last frame in dB (<= 0), for the meter. */
    float maxReductionDb() const noexcept { return meter.get(); }

private:
    void processFrame() noexcept;

    double fs = 48000.0;
    int order = 11, frameSize = 2048, hop = 512;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, inL, inR, outL, outR, bufL, bufR, mag, smooth, gain, dryL, dryR;
    int inPos = 0, hopCount = 0, outPos = 0;
    double attackCoeff = 0.0, releaseCoeff = 0.0;
    bool wasOn = false;
    Meter meter;
};
} // namespace mm::dsp
