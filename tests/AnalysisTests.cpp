// Analysis (spec 5) and monitoring safety (spec 6.0) tests.
#include "analysis/Checks.h"
#include "analysis/TapAnalyzer.h"
#include "monitoring/Monitoring.h"
#include "monitoring/Profiles.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

using Catch::Approx;
using namespace mm::analysis;

namespace
{
constexpr double twoPi = 6.283185307179586;

void feed (TapAnalyzer& a, const std::vector<float>& l, const std::vector<float>& r)
{
    for (size_t off = 0; off < l.size(); off += 1024)
    {
        const int n = (int) std::min<size_t> (1024, l.size() - off);
        a.process (l.data() + off, r.data() + off, n);
    }
}

std::vector<float> sine (double fs, double f, double amp, double seconds, double phase = 0.0)
{
    std::vector<float> v ((size_t) (fs * seconds));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = (float) (amp * std::sin (twoPi * f * (double) i / fs + phase));
    return v;
}
} // namespace

TEST_CASE ("Spectrum puts a sine at the right frequency and level", "[analysis][spectrum]")
{
    const double fs = 48000.0;
    TapAnalyzer a;
    a.prepare (fs, 13);
    const auto s = sine (fs, 1000.0, 0.5, 3.0);
    feed (a, s, s);
    std::vector<double> freqs;
    for (int i = 0; i < 600; ++i)
        freqs.push_back (20.0 * std::pow (1000.0, i / 599.0));
    std::vector<float> db;
    powerToDisplay (a.averagePower (SpecChannel::stereo), fs, a.fftSize(), freqs, 0, 0.0, db);
    size_t best = 0;
    for (size_t i = 0; i < db.size(); ++i)
        if (db[i] > db[best])
            best = i;
    CHECK (freqs[best] == Approx (1000.0).epsilon (0.03));
    // without smoothing, the sum of the bins around the peak is the sine power A^2 / 2
    double total = 0.0;
    for (float pw : a.averagePower (SpecChannel::stereo))
        total += pw;
    CHECK (10.0 * std::log10 (total) == Approx (-9.03).margin (0.1));
}

TEST_CASE ("Tonal balance: default target shares sum to the whole", "[analysis][tonal]")
{
    const auto t = defaultTonalTarget();
    double sum = 0.0;
    for (double v : t)
        sum += std::pow (10.0, v / 10.0);
    CHECK (sum == Approx (1.0).margin (1e-6));
}

TEST_CASE ("Tonal balance shows energy in the right band", "[analysis][tonal]")
{
    const double fs = 48000.0;
    TapAnalyzer a;
    a.prepare (fs, 13);
    const auto s = sine (fs, 100.0, 0.3, 4.0); // Bass band 60-150 Hz
    feed (a, s, s);
    const auto t = a.tonalIntegrated();
    CHECK (t[1] > -0.5); // almost all energy in Bass
    CHECK (t[3] < -30.0);
}

TEST_CASE ("Stereo correlation, width and mono loss", "[analysis][stereo]")
{
    const double fs = 48000.0;
    {
        TapAnalyzer a;
        a.prepare (fs, 13);
        const auto s = sine (fs, 1000.0, 0.3, 2.0);
        feed (a, s, s);
        CHECK (a.stereoBroadband (false).correlation == Approx (1.0).margin (0.01));
        CHECK (a.stereoBroadband (false).widthPercent == Approx (0.0).margin (0.5));
        CHECK (a.stereoBroadband (false).monoLossDb == Approx (0.0).margin (0.05));
    }
    {
        TapAnalyzer a;
        a.prepare (fs, 13);
        const auto s = sine (fs, 1000.0, 0.3, 2.0);
        auto inv = s;
        for (auto& v : inv)
            v = -v;
        feed (a, s, inv);
        CHECK (a.stereoBroadband (false).correlation == Approx (-1.0).margin (0.01));
        CHECK (a.stereoBroadband (false).monoLossDb < -40.0);
        CHECK (a.stereoBand (2, false).correlation == Approx (-1.0).margin (0.05)); // 500 Hz - 4 kHz band
    }
    {
        TapAnalyzer a;
        a.prepare (fs, 13);
        const auto l = sine (fs, 1000.0, 0.3, 2.0);
        const auto r = sine (fs, 1000.0, 0.3, 2.0, twoPi / 4.0); // 90 degrees: uncorrelated
        feed (a, l, r);
        CHECK (a.stereoBroadband (false).correlation == Approx (0.0).margin (0.02));
        CHECK (a.stereoBroadband (false).monoLossDb == Approx (-3.01).margin (0.1));
    }
}

TEST_CASE ("Low-end analysis: note level and sub/bass ratio", "[analysis][lowend]")
{
    const double fs = 48000.0;
    TapAnalyzer a;
    a.prepare (fs, 13);
    auto s = sine (fs, 46.25, 0.3, 4.0); // F#1
    const auto b = sine (fs, 92.5, 0.3, 4.0);
    for (size_t i = 0; i < s.size(); ++i)
        s[i] += b[i];
    feed (a, s, s);
    CHECK (a.subToBassDb() == Approx (0.0).margin (0.6)); // equal sine levels in 20-60 and 60-120
    CHECK (std::abs (a.peakAsymmetryPercent()) < 40.0);
}

TEST_CASE ("Checks give plain-language labels and lights", "[analysis][checks]")
{
    CheckInput in;
    in.tonal = defaultTonalTarget();
    in.tonal[0] += 2.9;
    in.monoLossDb = -0.4;
    in.lowCorrelation = 0.96;
    in.plr = 7.8;
    in.harshness = defaultHarshnessTarget() + 3.1;
    in.truePeak = -1.1;
    in.integrated = -8.9;
    in.hasSignal = true;
    Targets t;
    const auto checks = computeChecks (in, t, Thresholds {});
    REQUIRE (checks.size() == 12);
    CHECK (checks[0].light == Light::bad);
    CHECK (tonalLabel (0, 2.4) == "Sub +2.4 dB vs target");
    CHECK (checks[6].light == Light::ok);   // mono loss
    CHECK (checks[8].light == Light::warn); // PLR 7.8
    CHECK (checks[9].light == Light::bad);  // harshness +3.1
    CHECK (checks[10].light == Light::ok);  // true peak below ceiling
    CHECK (checks[11].light == Light::info);
    for (const auto& c : checks)
        CHECK (c.tooltip.isNotEmpty());
}

TEST_CASE ("Monitoring is bit-identical to disabled when rendering offline", "[monitoring][safety]")
{
    using mm::monitoring::Monitoring;
    const double fs = 48000.0;
    Monitoring m;
    m.prepare (fs, 512);
    // enable every feature
    mm::monitoring::ParametricProfile prof;
    juce::String err;
    REQUIRE (mm::monitoring::parseParametric ("Preamp: -6 dB\nFilter 1: ON PK Fc 105 Hz Gain -3.2 dB Q 1.41\nFilter 2: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70\n", prof, err));
    m.setCorrection (0, Monitoring::buildCorrection (prof, fs, 1.0, 3.0, true));
    m.setCorrection (1, Monitoring::buildCorrection (prof, fs, 1.0, 3.0, true));
    m.setSystem (Monitoring::buildSystem (mm::monitoring::defaultSystemPresets()[0], fs));
    auto ref = std::make_unique<mm::monitoring::ReferenceAudio>();
    ref->audio.setSize (2, 48000);
    ref->audio.clear();
    ref->integrated = -10.0;
    m.setReference (std::move (ref));
    m.correctionSlot = 1;
    m.systemIndex = 0;
    m.listenMode = (int) Monitoring::Listen::sideOnly;
    m.bandSolo = 2;
    m.deltaListen = true;
    m.chainBypass = true;
    m.referenceActive = true;
    m.refPlaying = true;
    REQUIRE (m.isActive());

    juce::Random rnd (1);
    juce::AudioBuffer<float> b (2, 512), original (2, 512), delayed (2, 512);
    for (int block = 0; block < 20; ++block)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
            {
                b.setSample (ch, i, rnd.nextFloat() * 2.0f - 1.0f);
                delayed.setSample (ch, i, rnd.nextFloat() * 2.0f - 1.0f);
            }
        original.makeCopyOf (b);
        REQUIRE_FALSE (m.process (b, delayed, true));
        for (int ch = 0; ch < 2; ++ch)
            REQUIRE (std::memcmp (b.getReadPointer (ch), original.getReadPointer (ch), 512 * sizeof (float)) == 0);
    }
    // and in real time the same settings do change the audio
    REQUIRE (m.process (b, delayed, false));
}

TEST_CASE ("AutoEQ/REW parametric profiles parse; corrupt input is rejected", "[monitoring][profiles]")
{
    mm::monitoring::ParametricProfile p;
    juce::String err;
    REQUIRE (mm::monitoring::parseParametric ("Preamp: -6.2 dB\r\nFilter 1: ON PK Fc 105 Hz Gain -3.2 dB Q 1.41\r\nFilter 2: OFF PK Fc 200 Hz Gain 2 dB Q 1\r\nFilter 3: ON HSC Fc 10000 Hz Gain 4 dB Q 0.7", p, err));
    CHECK (p.preampDb == Approx (-6.2));
    CHECK (p.filters.size() == 2);
    CHECK_FALSE (mm::monitoring::parseParametric ("\x01\x02garbage", p, err));
    CHECK (err.isNotEmpty());

    // boost limit and cut-only below 300 Hz
    const auto set = mm::monitoring::Monitoring::buildCorrection (p, 48000.0, 1.0, 3.0, true);
    double maxDb = -100;
    for (double f = 20; f < 20000; f *= 1.05)
    {
        double db = mm::dsp::gainToDb (set->preampGain);
        for (const auto& c : set->sections)
            db += c.magnitudeDb (48000.0, f);
        maxDb = std::max (maxDb, db);
    }
    CHECK (maxDb <= 0.01); // auto preamp: no boost above 0 dB
}
