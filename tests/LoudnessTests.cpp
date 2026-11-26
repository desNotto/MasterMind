// Loudness and true-peak acceptance tests (spec section 10).
#include "analysis/Loudness.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using Catch::Approx;
using mm::analysis::LoudnessMeter;

namespace
{
constexpr double twoPi = 6.283185307179586;

void feedSine (LoudnessMeter& m, double fs, double freq, double peakDb, double seconds, double phase = 0.0)
{
    const double a = std::pow (10.0, peakDb / 20.0);
    const int n = (int) std::lround (seconds * fs);
    std::vector<float> l ((size_t) n), r ((size_t) n);
    for (int i = 0; i < n; ++i)
        l[(size_t) i] = r[(size_t) i] = (float) (a * std::sin (twoPi * freq * i / fs + phase));
    for (int off = 0; off < n; off += 512) // block-wise like the analysis thread
        m.process (l.data() + off, r.data() + off, std::min (512, n - off));
}
} // namespace

TEST_CASE ("1 kHz stereo sine at -23 dBFS reads -23 LUFS M, S and I", "[loudness]")
{
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
    {
        LoudnessMeter m;
        m.prepare (fs);
        feedSine (m, fs, 1000.0, -23.0, 20.0);
        INFO ("fs = " << fs);
        CHECK (m.momentary() == Approx (-23.0).margin (0.1));
        CHECK (m.shortTerm() == Approx (-23.0).margin (0.1));
        CHECK (m.integrated() == Approx (-23.0).margin (0.1));
    }
}

TEST_CASE ("Absolute gate excludes the part below -70 LUFS", "[loudness]")
{
    const double fs = 48000.0;
    LoudnessMeter m;
    m.prepare (fs);
    feedSine (m, fs, 1000.0, -80.0, 10.0);
    feedSine (m, fs, 1000.0, -23.0, 20.0);
    CHECK (m.integrated() == Approx (-23.0).margin (0.1));
}

TEST_CASE ("Relative gate ignores quiet passages 20 LU down", "[loudness]")
{
    const double fs = 48000.0;
    LoudnessMeter m;
    m.prepare (fs);
    feedSine (m, fs, 1000.0, -20.0, 20.0);
    feedSine (m, fs, 1000.0, -40.0, 20.0); // -40 is below the relative gate (-30 LU)
    CHECK (m.integrated() == Approx (-20.0).margin (0.15));
}

TEST_CASE ("Loudness range of two levels 10 dB apart is about 10 LU", "[loudness]")
{
    const double fs = 48000.0;
    LoudnessMeter m;
    m.prepare (fs);
    feedSine (m, fs, 1000.0, -20.0, 30.0);
    feedSine (m, fs, 1000.0, -30.0, 30.0);
    CHECK (m.loudnessRange() == Approx (10.0).margin (0.5));
}

TEST_CASE ("True peak of an fs/4 sine with 45 degree phase", "[truepeak]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        LoudnessMeter m;
        m.prepare (fs);
        // samples fall at +-0.7071 of the amplitude: sample peak -3 dBFS, true peak 0 dBFS
        feedSine (m, fs, fs / 4.0, 0.0, 2.0, twoPi / 8.0);
        INFO ("fs = " << fs);
        CHECK (m.samplePeakDb() == Approx (-3.01).margin (0.02));
        CHECK (m.truePeakDb() == Approx (0.0).margin (0.2));
    }
}

TEST_CASE ("True peak equals sample peak for a low-frequency sine", "[truepeak]")
{
    LoudnessMeter m;
    m.prepare (48000.0);
    feedSine (m, 48000.0, 100.0, -6.0, 2.0);
    CHECK (m.truePeakDb() == Approx (-6.0).margin (0.05));
}

TEST_CASE ("K-weighting gain near 1 kHz and at 20 Hz", "[loudness]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        const auto shelf = mm::analysis::kWeightingShelf (fs);
        const auto hp = mm::analysis::kWeightingHighpass (fs);
        const double g1k = shelf.magnitudeDb (fs, 997.0) + hp.magnitudeDb (fs, 997.0);
        CHECK (g1k == Approx (0.691).margin (0.05)); // cancels the -0.691 offset
        CHECK (shelf.magnitudeDb (fs, 10000.0) == Approx (4.0).margin (0.1));
        CHECK (hp.magnitudeDb (fs, 20.0) < -10.0);
    }
}
