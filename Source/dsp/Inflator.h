// MasterMind Inflator module (D-038).
// The curve, its coefficients, the clip stages and the three-band split are taken from
// JS Inflator by yg331 (Kiriki-liszt), https://github.com/Kiriki-liszt/JS_Inflator,
// Copyright (c) 2024 yg331, licensed under the GNU General Public License v3.0.
// MasterMind is distributed under the same licence (see LICENSE).
#pragma once

#include "dsp/Module.h"
#include "dsp/Dynamics.h"
#include "dsp/Oversampler.h"

#include <array>
#include <vector>

namespace mm::dsp
{
/** Inflator (replaces the 4.8 Clipper in slot 7): an odd-symmetric polynomial that raises the
    quiet parts of the wave more than the peaks, mixed with the dry signal by Effect.
    Oversampled by the Quality setting (4x normal, 8x high), like the Clipper it replaces. */
class Inflator final : public Module
{
public:
    struct Settings
    {
        float inputDb = 0.0f;   // -6 .. +12 dB
        float effect = 0.3f;    // 0 .. 1
        float curve = 0.0f;     // -50 .. +50
        float outputDb = 0.0f;  // -12 .. 0 dB
        bool clip = true;       // clip to 0 dBFS before and after the curve
        bool split = false;     // three bands at 240 Hz and 2.4 kHz
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;
    int latency() const noexcept override { return os.latency(); }
    void setHighQuality (bool high) noexcept { os.setUseHigh (high); }

    /** The Inflator curve for one sample (curve -50 .. +50), JS Inflator's process_inflator. */
    static double shape (double x, double curve) noexcept;
    /** Effect mix of the curve as the plugin applies it (clip on: the input is limited to +-1). */
    static double transfer (double x, double curve, double effect, bool clip) noexcept;

    /** Input peak of the last block in dBFS (after Input). */
    float inputPeakDb() const noexcept { return inMeter.get(); }
    /** Loudness the module adds: RMS after the curve and Effect minus RMS before, about 300 ms (dB). */
    float inflateDb() const noexcept { return inflMeter.get(); }
    /** Peak reduction by the 0 dB clip stages of the last block (dB, <= 0). */
    float clipReductionDb() const noexcept { return clipMeter.get(); }

    /** Waveform for the UI (single producer: audio thread, single consumer: message thread):
        mono after Input and mono after Effect at the base rate; the output lags the input by
        latency() samples. Returns the number of samples read. */
    int readCapture (float* in, float* out, int maxSamples) noexcept;

private:
    /** JS Inflator's band split: two one-pole sections, low < 240 Hz, high > 2.4 kHz, the mid band
        is the rest, so the three bands sum to the input. */
    struct OnePole
    {
        double C = 0.0, R = 0.0, I = 0.0;
    };
    struct BandSplit
    {
        OnePole LP, HP;
        double G = 1.0, GR = 1.0;
        void set (double fcLow, double fcHigh, double fs) noexcept;
        void clear() noexcept { LP.R = LP.I = HP.R = HP.I = 0.0; }
    };

    double fs = 48000.0, lastRate = 0.0;
    DualOversampler os;
    std::array<BandSplit, 2> split;
    Ramp inGain, outGain, effect, curve;
    Meter inMeter, inflMeter, clipMeter;
    double energyIn = 0.0, energyOut = 0.0, energyCoeff = 0.0;
    static constexpr int captureSize = 16384;
    juce::AbstractFifo capture { captureSize };
    std::vector<float> capIn, capOut, scratchIn;
};
} // namespace mm::dsp
