// Auto match engine (D-040) and the Low-End phase Auto (mockup v18).
#include "analysis/TapAnalyzer.h"
#include "automatch/MatchEngine.h"
#include "dsp/PhaseAuto.h"
#include "reference/TrackAnalysis.h"
#include "state/Parameters.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>

using namespace mm;

namespace
{
/** Drum-like noise: low and high parts with their own gains, a 4 Hz hit envelope, some stereo. */
juce::AudioBuffer<float> makeSignal (double fs, double seconds, float lowGain, float highGain, float level, float hitDepth, uint32_t seed)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (2, n);
    uint32_t s = seed;
    const auto rnd = [&s] { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 16777216.0f * 2.0f - 1.0f; };
    const float a = (float) std::exp (-2.0 * 3.14159265 * 250.0 / fs);
    float lp = 0.0f, lp2 = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float x = rnd();
        lp = a * lp + (1.0f - a) * x;
        lp2 = a * lp2 + (1.0f - a) * lp;
        const float hi = x - lp;
        const float t = (float) std::fmod (i / fs * 4.0, 1.0);
        const float env = 1.0f - hitDepth + hitDepth * std::exp (-t * 12.0f);
        const float v = level * env * (lowGain * 4.0f * lp2 + highGain * hi);
        b.setSample (0, i, v);
        b.setSample (1, i, 0.85f * v + 0.15f * level * env * highGain * rnd());
    }
    return b;
}

automatch::Target targetFrom (const juce::AudioBuffer<float>& ref, double fs)
{
    analysis::TapAnalyzer a;
    a.setLightweight (true);
    a.prepare (fs, 13);
    a.process (ref.getReadPointer (0), ref.getReadPointer (1), ref.getNumSamples());
    automatch::Target t;
    t.tonal = a.tonalIntegrated();
    t.corridor.fill (1.0);
    t.crest = a.crestDb();
    t.hasCrest = true;
    for (int b = 0; b < 4; ++b)
        t.width[(size_t) b] = a.stereoBand (b, false).widthPercent;
    t.hasWidth = true;
    t.lufs = a.loudness().integrated();
    t.harshness = a.harshnessDb();
    t.hasHarshness = true;
    t.ceiling = -1.0;
    return t;
}

automatch::ParamMap defaults()
{
    // the default value of every parameter, as a fresh plugin
    automatch::ParamMap p;
    struct Dummy final : public juce::AudioProcessor
    {
        Dummy() : apvts (*this, nullptr, "D", mm::params::createLayout()) {}
        const juce::String getName() const override { return "D"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}
        juce::AudioProcessorValueTreeState apvts;
    } d;
    for (auto* prm : d.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            p[rp->paramID] = rp->convertFrom0to1 (rp->getValue());
    return p;
}

struct Run
{
    double startCost, endCost, lufs, targetLufs, seconds;
    int evals;
    automatch::ParamMap result;
};

Run runMode (automatch::Mode mode, double seconds, bool thinMix = false)
{
    const double fs = 48000.0;
    // default: a dark, quieter, very dynamic mix against a brighter, louder, denser reference;
    // thinMix: a mix with little low end against a reference with a lot (the boomy case of D-041)
    const auto mix = thinMix ? makeSignal (fs, seconds, 0.35f, 1.0f, 0.3f, 0.5f, 7u) : makeSignal (fs, seconds, 1.6f, 0.5f, 0.2f, 0.8f, 7u);
    const auto ref = thinMix ? makeSignal (fs, seconds, 2.2f, 0.8f, 0.5f, 0.5f, 99u) : makeSignal (fs, seconds, 1.0f, 1.0f, 0.5f, 0.4f, 99u);
    const auto target = targetFrom (ref, fs);
    const auto current = defaults();
    analysis::TapAnalyzer in;
    in.setLightweight (true);
    in.prepare (fs, 13);
    in.process (mix.getReadPointer (0), mix.getReadPointer (1), mix.getNumSamples());
    const auto fixed = automatch::fixedSetup (current, in.loudness().integrated());
    const auto t0 = std::chrono::steady_clock::now();
    automatch::Matcher m (mode, fs, current, fixed, mix, (int) (0.5 * fs), target, 8);
    m.startThread();
    while (! m.done.load() && m.isThreadRunning())
        juce::Thread::sleep (20);
    m.waitForThreadToExit (-1);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    REQUIRE (m.done.load());
    juce::String knobs;
    for (const auto& d : automatch::dims())
        knobs << d.id << "=" << m.result[d.id] << " ";
    UNSCOPED_INFO ("knobs " << knobs << " neutral lufs " << m.neutralFeatures.lufs << " tp " << m.finalFeatures.truePeak);
    return { m.startCost.load(), m.bestCost.load(), m.finalFeatures.lufs, target.lufs, sec, m.evals.load(), m.result };
}
} // namespace

TEST_CASE ("Auto match Fast moves the mix towards the reference", "[automatch]")
{
    const auto r = runMode (automatch::Mode::fast, 4.5);
    INFO ("start " << r.startCost << " end " << r.endCost << " lufs " << r.lufs << " target " << r.targetLufs << " time " << r.seconds << " s, " << r.evals << " passes");
    CHECK (r.endCost < 0.5 * r.startCost);
    CHECK (std::abs (r.lufs - r.targetLufs) < 1.0);
}

TEST_CASE ("Auto match lifts a thin low end gently", "[automatch]")
{
    // the reference has far more low end: the EQ may lift Sub and Bass by 3 dB at most, the rest
    // comes from harmonics, the low bands are dynamic and the low end is cleaned (D-041)
    auto r = runMode (automatch::Mode::fast, 4.5, true);
    INFO ("start " << r.startCost << " end " << r.endCost);
    CHECK (r.result["eq_b1_gain"] <= 3.0f);
    CHECK (r.result["eq_b2_gain"] <= 3.0f);
    CHECK (r.result["lem_tr_amount"] > 0.0f);
    CHECK (r.result["eq_b1_dyn"] == 1.0f);
    CHECK (r.result["eq_b1_thr"] < 0.0f);
    CHECK (r.result["hpf_freq"] == 30.0f);
    CHECK (r.result["lem_mono_on"] == 1.0f);
    CHECK (r.endCost < r.startCost);
}

TEST_CASE ("Auto match modes: time and distance", "[.bench]")
{
    for (auto mode : { automatch::Mode::fast, automatch::Mode::hybrid, automatch::Mode::precise })
    {
        const auto r = runMode (mode, 8.5);
        WARN ("mode " << (int) mode << ": distance " << r.startCost << " -> " << r.endCost << ", LUFS " << r.lufs << " (target " << r.targetLufs << "), "
                      << r.evals << " passes in " << r.seconds << " s");
    }
}

TEST_CASE ("Phase Auto finds the angle that makes the low band symmetric", "[automatch][phase]")
{
    // a bass note with a strong second harmonic in a phase that makes the peaks lopsided
    const double rate = 1500.0, f = 55.0;
    std::vector<float> x ((size_t) (2.0 * rate));
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / rate;
        x[i] = (float) (0.5 * std::sin (2.0 * 3.14159265 * f * t) + 0.3 * std::sin (2.0 * 3.14159265 * 2.0 * f * t + 1.57));
    }
    const auto res = dsp::phaseauto::search ({ x }, rate, 80.0, 400);
    REQUIRE (res.valid);
    INFO ("before " << res.before << " % after " << res.after << " % at " << res.angle << " deg");
    CHECK (res.before > 15.0);
    CHECK (res.after < 0.5 * res.before);
    // silence: no result
    const std::vector<float> quiet (3000, 0.0f);
    CHECK (! dsp::phaseauto::search ({ quiet }, rate, 80.0, 400).valid);
}

TEST_CASE ("Reference metrics region: only that part of the song is analysed", "[automatch][region]")
{
    // 6 s of a 60 Hz tone, then 6 s of bright noise: the region of the noise has almost no Sub
    const double fs = 44100.0;
    const int half = (int) (6.0 * fs);
    juce::AudioBuffer<float> b (2, 2 * half);
    uint32_t s = 3u;
    for (int i = 0; i < 2 * half; ++i)
    {
        s = s * 1664525u + 1013904223u;
        const float v = i < half ? 0.5f * (float) std::sin (2.0 * 3.14159265 * 60.0 * i / fs) : 0.3f * ((float) (s >> 8) / 16777216.0f * 2.0f - 1.0f);
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    const auto file = juce::File::createTempFile (".wav");
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        REQUIRE (os != nullptr);
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
        REQUIRE (w != nullptr);
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    std::atomic<bool> cancel { false };
    juce::String err;
    const auto whole = reference::analyseFile (file, {}, cancel, err);
    const auto region = reference::analyseFile (file, {}, cancel, err, 7.0, 12.0);
    file.deleteFile();
    REQUIRE (whole.has_value());
    REQUIRE (region.has_value());
    INFO ("Sub share whole " << whole->tonal[0] << " dB, region " << region->tonal[0] << " dB; duration " << region->durationSeconds);
    CHECK (region->tonal[0] < whole->tonal[0] - 10.0);
    CHECK (std::abs (region->durationSeconds - 5.0) < 0.1);
}

TEST_CASE ("Reach target finds a clean way to the target loudness", "[automatch][reach]")
{
    const double fs = 48000.0;
    const auto mix = makeSignal (fs, 4.5, 1.0f, 1.0f, 0.3f, 0.6f, 11u);
    auto current = defaults();
    current["lim_on"] = 1.0f;
    analysis::TapAnalyzer in;
    in.setLightweight (true);
    in.prepare (fs, 13);
    in.process (mix.getReadPointer (0), mix.getReadPointer (1), mix.getNumSamples());
    const double target = in.loudness().integrated() + 8.0;
    const auto t0 = std::chrono::steady_clock::now();
    automatch::LoudnessSearch s (fs, current, mix, (int) (0.5 * fs), target, 8);
    s.startThread();
    while (! s.done.load() && s.isThreadRunning())
        juce::Thread::sleep (20);
    s.waitForThreadToExit (-1);
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    REQUIRE (s.done.load());
    INFO ("target " << target << " reached " << s.reachedLufs << " in " << sec << " s, " << s.evals.load() << " passes; Drive " << s.result["lim_drive"]
                    << " Clip " << s.result["lim_clip"] << " Inflator " << s.result["infl_effect"] << " artefacts " << s.artefacts);
    CHECK (s.reached);
    CHECK (std::abs (s.reachedLufs - target) <= 0.3);
    CHECK (s.result.count ("lim_drive") == 1);
}
