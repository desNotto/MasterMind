#pragma once

#include "dsp/Module.h"

#include <array>

namespace mm::dsp
{
/** 4.4 Low-End Manager: mono maker, phase rotator and Low-End Translator. Works on mid/side. */
class LowEnd final : public Module
{
public:
    struct Settings
    {
        bool monoOn = true;
        float monoFreq = 120.0f;
        int monoSlope = 1; // 12, 24, 48 dB/oct
        float monoAmount = 1.0f;
        bool rotOn = false;
        float rotFreq = 80.0f, rotAngle = 0.0f;
        bool trOn = false;
        float trAmount = 0.0f, trMix = 0.5f; // mix: 0 = 2nd only, 1 = 3rd only
    } settings;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;

    /** Translator harmonic level relative to the low band, dB (UI meter). */
    float translatorLevelDb() const noexcept { return trMeter.get(); }

private:
    void updateMono (double f) noexcept;
    void updateRotator (double f, double angleDeg) noexcept;

    double fs = 48000.0;
    // mono maker: side split, mid gets the matching allpass so phase stays coherent
    LinkwitzRiley sideSplit;
    Cascade midAllpass;
    FreqRamp monoFreq;
    Ramp monoAmount;
    int monoSlope = -1;
    // phase rotator: four first-order allpasses on mid and side
    static constexpr int rotStages = 4;
    std::array<std::array<Biquad, rotStages>, 2> rot;
    FreqRamp rotFreq;
    Ramp rotAngle;
    bool rotWasOn = false;
    // translator
    Cascade trBand, trHighpass;
    Envelope trEnv;
    Ramp trAmount;
    Meter trMeter;
};
} // namespace mm::dsp
