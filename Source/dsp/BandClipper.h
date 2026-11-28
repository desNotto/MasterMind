#pragma once

#include "dsp/Filters.h"
#include "dsp/Module.h"
#include "dsp/Oversampler.h"

#include <array>

namespace mm::dsp
{
/** Spectral clipper of the Limiter (D-042, after the clipper stage of Newfangled Elevate).

    Four bands (120 Hz, 1 kHz, 5 kHz, Linkwitz-Riley 4th order with allpass compensation, so the
    bands sum to an allpass of the input) are soft-clipped separately at 2x oversampling. The threshold
    of each band follows its own peak level (instant attack, 1.5 s release) and sits `amountDb` below
    it, so the loudest peaks of each band lose up to that much before the limiter. A clipped kick does
    not distort the guitars, and the distortion of each band stays where that band is loud.

    With amount 0 the stage is bypassed through a delay of the same latency (crossfaded over 20 ms),
    so it costs nothing and changes nothing. Real-time safe after prepare(). */
class BandClipper
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    int latency() const noexcept { return os.latency(); }

    float amountDb = 0.0f; // 0..9 dB

    void process (float* left, float* right, int numSamples) noexcept;

    /** Largest peak reduction of the last block over all bands, dB (>= 0). */
    float reductionDb() const noexcept { return meter.get(); }

private:
    static constexpr int numBands = 4;
    struct Channel
    {
        LinkwitzRiley split1k, split120, split5k;
        std::array<Cascade, numBands> ap; // phase compensation by the split of the other branch
        Cascade residualLp;               // the clipping products above 18 kHz are removed
    };
    std::array<Channel, 2> ch;
    std::array<double, numBands> env {}, ms {}; // peak envelope and mean square (300 ms) per band
    double envRelease = 0.0, msCoeff = 0.0;
    DualOversampler os;
    DelayLine dry;
    juce::AudioBuffer<float> work;
    double fs = 48000.0, mix = 0.0, mixStep = 0.0;
    Meter meter;
};
} // namespace mm::dsp
