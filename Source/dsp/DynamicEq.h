#pragma once

#include "dsp/Module.h"
#include "dsp/Resonance.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>

namespace mm::dsp
{
enum class EqKind
{
    bell,
    lowShelf,
    highShelf,
    notch,
    lowCut,
    highCut
};

/** 4.2 Ten-band dynamic EQ with minimum-phase or linear-phase operation. */
class DynamicEq final : public Module
{
public:
    static constexpr int numBands = 10;

    struct Band
    {
        bool on = false;
        int type = 0; // raw choice index, see kindOf()
        int slope = 3;
        float freq = 1000.0f, gainDb = 0.0f, q = 0.71f;
        int placement = 0; // Stereo, Mid, Side, Left, Right
        bool dynamic = false;
        float thresholdDb = -24.0f, rangeDb = 6.0f;
        int mode = 0; // compress, expand
        float attackMs = 10.0f, releaseMs = 100.0f;
    };

    struct Settings
    {
        bool linearPhase = false;
        std::array<Band, numBands> bands;
    } settings;

    static EqKind kindOf (int bandIndex, int typeChoice) noexcept;

    void prepare (const Context& ctx) override;
    void reset() override;
    void process (float* left, float* right, int numSamples) noexcept override;
    int latency() const noexcept override { return (settings.linearPhase ? firLength / 2 : 0) + resonance.latency(); }

    ResonanceSuppressor resonance;

    /** Effective (dynamic) gain of each band in dB for the UI. */
    float effectiveGainDb (int band) const noexcept { return meters[(size_t) band].get(); }
    /** Detector level of a dynamic band in dB (the level the threshold acts on); -100 otherwise. */
    float detectorLevelDb (int band) const noexcept { return levelMeters[(size_t) band].get(); }

    /** Builds and loads the linear-phase kernels for a settings snapshot. Allocates: call from a
        worker thread, never from the audio thread. */
    void buildLinearPhase (const Settings& snapshot);

    /** Magnitude response in dB of a settings snapshot (static gains), for the UI curve. */
    static double responseDb (const Settings& s, double fs, double f, const float* dynamicGainsDb = nullptr);
    static double bandResponseDb (const Band& b, int bandIndex, double fs, double f, float gainDb);

    int getFirLength() const noexcept { return firLength; }

private:
    struct BandState
    {
        std::array<Cascade, 2> filter;
        std::array<Biquad, 2> detector;
        std::array<Cascade, 2> delta; // linear-phase mode: dynamic part only
        FreqRamp freq;
        Ramp gain, q;
        Envelope env;
        double dynAmount = 0.0;
        bool wasOn = false;
        int lastType = -1, lastSlope = -1;
    };

    void designBand (int index, BandState& st, double f, double g, double q, bool deltaOnly) noexcept;
    void processMinimumPhase (float* left, float* right, int numSamples) noexcept;
    void processLinearPhase (float* left, float* right, int numSamples) noexcept;
    void runBands (float* left, float* right, int numSamples, bool deltaOnly) noexcept;

    double fs = 48000.0;
    int maxBlock = 512, firLength = 4096;
    std::array<BandState, numBands> state;
    std::array<Meter, numBands> meters, levelMeters;

    std::array<juce::dsp::Convolution, 4> conv; // LL, LR, RL, RR
    juce::AudioBuffer<float> scratch;
    bool convPrepared = false;
};
} // namespace mm::dsp
