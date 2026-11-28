#pragma once

#include "dsp/Module.h"

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace mm::dsp
{
/** Spectral part of the Limiter (D-037, after the idea of Newfangled Elevate).

    A linear-phase filter bank of 8, 16 or 26 triangular bands (log-frequency triangles that sum to
    one, so the bands reconstruct the input exactly, delayed by half the filter length) applies a
    gain per band. The gains change once per frame (B samples) and are interpolated linearly inside
    the frame (two convolutions per output, crossfaded), so the output is exactly
    sum_b g_b(t) * (h_b * x)(t) with piecewise-linear g_b.

    The convolution is uniformly partitioned (FFT size 2B, K = L / B partitions). Per frame the
    effective filter Hg = sum_b g_b H_b is built in the frequency domain, so the cost does not
    depend on the band count apart from that sum.

    Two outputs: `shaped` carries the adaptive band reductions (the broadband limiter listens to
    it), `full` additionally carries the attack emphasis (the limiter gain is applied to it, so the
    emphasised attacks pass the limiter and are caught by the clipper and the safety stage).

    Real-time safe: everything is allocated in prepare(). */
class SpectralBank
{
public:
    static constexpr int maxBands = 26;

    struct Settings
    {
        int bandSet = 2;          // 0: 8 bands, 1: 16 bands, 2: 26 bands
        float adaptGainDb = 6.0f; // how far one band may move away from the others
        float adaptSpeed = 0.7f;  // 0: equal time constants, 1: low bands slow, high bands fast
        float emphasis = 0.0f;    // attack emphasis 0..1 (up to +6 dB)
        float adaptTrans = 0.5f;  // 0: one emphasis for all bands, 1: every band on its own
        float soften = 0.0f;      // transient softening 0..1 (up to -6 dB on attacks, both outputs, D-042)
        float link = 1.0f;
        int mode = 1;             // Transparent, Balanced, Aggressive (release base)
        double ceiling = 0.89;    // linear, for the pressure estimate
    } settings;

    void prepare (double sampleRate);
    void reset();

    /** Processes n samples: input (after Drive) -> shaped and full outputs, delayed by latency(). */
    void process (const float* inL, const float* inR, float* shapedL, float* shapedR, float* fullL, float* fullR, int n) noexcept;

    int latency() const noexcept { return blockSize + filterLength / 2; }

    /** Centre frequencies of a band set. */
    static std::vector<double> centres (int bandSet);
    static int bandCount (int bandSet) noexcept { return bandSet == 0 ? 8 : (bandSet == 1 ? 16 : 26); }

    /** Meters for the UI (relaxed atomics): reduction and emphasis per band in dB, for the current set. */
    std::array<Meter, maxBands> bandGrDb, bandEmphDb;
    std::atomic<int> meterBands { 26 };

private:
    struct BandSetData
    {
        int bands = 0;
        std::vector<double> fc;
        std::vector<float> spectra;          // [band][partition][N + 2] filter partition spectra
        std::vector<int> lowBandLong, lowBandShort;  // per analysis bin: lower band index
        std::vector<float> wLong, wShort;    // weight of the lower band (the upper band gets 1 - w)
    };
    struct GainState
    {
        std::array<double, maxBands> program {}, peak {}, offset {}, fast {}, slow {};
        std::array<std::array<float, maxBands>, 16> emphDelay {}, softDelay {}, limDelay {};
        std::array<double, maxBands> lim {}; // fast band limiting, dB (D-043)
        int emphPos = 0;
        std::array<float, maxBands> gShaped {}, gFull {};
    };

    void designSet (BandSetData& set, int bandSet);
    void runFrame() noexcept;
    void analyse (int ch) noexcept;
    void analyseShort (int ch) noexcept;
    void computeGains (GainState& st, const float* eLong, const float* eShort, double pressureDb, double frameNeedDb) noexcept;
    void buildFilter (const BandSetData& set, const float* gains, float* dest) noexcept;
    void convolve (int ch, const float* filter, float* out) noexcept;

    double fs = 48000.0;
    int blockSize = 256, fftSize = 512, filterLength = 4096, partitions = 16, bins = 257;
    std::unique_ptr<juce::dsp::FFT> fftBlock, fftLong, fftAna;
    std::array<BandSetData, 3> sets;
    int activeSet = 2;

    // per channel
    std::array<std::vector<float>, 2> history;   // last filterLength input samples (ring)
    std::array<int, 2> histPos {};
    std::array<std::vector<float>, 2> fdl;       // K input spectra (ring of frames)
    int fdlPos = 0;
    std::array<std::vector<float>, 2> inFrame, outShaped, outFull;
    int framePos = 0;
    std::array<std::array<float, maxBands>, 2> eLong {}, eShort {};
    std::array<GainState, 2> state;
    std::array<double, 2> framePeak {};
    double pressure = 0.0;

    // filters: [ch][shaped/full][prev/cur]
    std::array<std::array<std::array<std::vector<float>, 2>, 2>, 2> hg;
    int cur = 0, frameCount = 0;
    // filters equal in both slots (gains did not change): one convolution is enough
    std::array<std::array<bool, 2>, 2> sameAsPrev {};
    std::array<bool, 2> fullEq { true, true }, fullEqPrev { true, true };
    std::array<std::array<std::array<float, maxBands>, 2>, 2> builtGains {};
    std::vector<float> work, work2, windowLong, windowShort, analysisBuf;
    std::array<float, maxBands> linkedGShaped {}, linkedGFull {};
};
} // namespace mm::dsp
