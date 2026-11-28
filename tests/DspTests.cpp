// Processing chain acceptance tests (spec section 10).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHost.h"
#include "dsp/Filters.h"

#include <juce_dsp/juce_dsp.h>

#include <cmath>
#include <thread>

using Catch::Approx;
using mmtest::Host;

namespace
{
constexpr double twoPi = 6.283185307179586;

/** Output equals input delayed by `delay` samples, bit for bit. */
bool delayedIdentical (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& out, int delay)
{
    for (int ch = 0; ch < 2; ++ch)
        for (int i = delay; i < in.getNumSamples(); ++i)
            if (std::memcmp (out.getReadPointer (ch) + i, in.getReadPointer (ch) + i - delay, sizeof (float)) != 0)
                return false;
    return true;
}

double sineGainDb (Host& h, double fs, double freq, double amp = 0.1)
{
    const int n = (int) fs; // 1 s
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
        b.setSample (0, i, (float) (amp * std::sin (twoPi * freq * i / fs))), b.setSample (1, i, b.getSample (0, i));
    mmtest::runChain (h, b, 512);
    double sum = 0.0;
    for (int i = n / 2; i < n; ++i)
        sum += (double) b.getSample (0, i) * b.getSample (0, i);
    const double rms = std::sqrt (sum / (n / 2));
    return 20.0 * std::log10 (rms / (amp / std::sqrt (2.0)));
}

/** Independent 8x true-peak meter for the acceptance test: a much longer interpolator (256 taps per
    phase, Kaiser beta 10) than the limiter's own detector. Checked against ideal FFT interpolation. */
double independentTruePeakDb (const juce::AudioBuffer<float>& b, int* where = nullptr)
{
    double peak = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        mm::dsp::TruePeakDetectorT<8, 256, 100> meter;
        const float* d = b.getReadPointer (ch);
        // no zero padding after the end: an artificial step to silence is not part of the output
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double v = meter.process (d[i]);
            if (v > peak)
            {
                peak = v;
                if (where != nullptr)
                    *where = i - 128;
            }
        }
    }
    return 20.0 * std::log10 (std::max (peak, 1e-12));
}
} // namespace

TEST_CASE ("Default state is a bit-exact delay of the reported latency", "[dsp][transparency]")
{
    juce::Random random (1234);
    for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        for (int bs : { 1, 32, 64, 512, 2048, 8192 })
        {
            Host h;
            h.chain.prepare (fs, bs, false);
            const int latency = h.chain.latencySamples();
            juce::AudioBuffer<float> in (2, std::max (4 * bs, latency + 2048));
            mmtest::fillNoise (in, random);
            juce::AudioBuffer<float> out;
            out.makeCopyOf (in);
            mmtest::runChain (h, out, bs);
            INFO ("fs " << fs << " block " << bs << " latency " << latency);
            REQUIRE (delayedIdentical (in, out, latency));
        }
}

TEST_CASE ("Bit-exact with randomly varying block sizes and all modules bypassed", "[dsp][transparency]")
{
    Host h;
    h.set ("in_on", 0.0f);
    h.set ("out_on", 0.0f);
    h.chain.prepare (48000.0, 1024, false);
    const int latency = h.chain.latencySamples();
    juce::Random random (99);
    juce::AudioBuffer<float> in (2, 48000);
    mmtest::fillNoise (in, random);
    juce::AudioBuffer<float> out;
    out.makeCopyOf (in);
    for (int start = 0; start < out.getNumSamples();)
    {
        const int n = std::min (1 + random.nextInt (1024), out.getNumSamples() - start);
        float* chans[2] = { out.getWritePointer (0) + start, out.getWritePointer (1) + start };
        juce::AudioBuffer<float> view (chans, 2, n);
        h.chain.process (view);
        start += n;
    }
    REQUIRE (delayedIdentical (in, out, latency));
}

TEST_CASE ("NaN and Inf input produce silence without a crash", "[dsp][robustness]")
{
    Host h;
    h.set ("lim_on", 1.0f);
    h.set ("bus_on", 1.0f);
    h.chain.prepare (48000.0, 512, false);
    juce::AudioBuffer<float> b (2, 512);
    for (float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() })
    {
        b.clear();
        b.setSample (1, 100, bad);
        REQUIRE_FALSE (h.chain.process (b));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                REQUIRE (b.getSample (ch, i) == 0.0f);
    }
    juce::Random r (5);
    mmtest::fillNoise (b, r, 0.5f);
    REQUIRE (h.chain.process (b));
    for (int i = 0; i < 512; ++i)
        REQUIRE (std::isfinite (b.getSample (0, i)));
}

TEST_CASE ("An impulse comes out exactly at the reported latency", "[dsp][latency]")
{
    for (int quality : { 1, 2 })
        for (float lookahead : { 0.5f, 2.0f, 5.0f })
        {
            Host h;
            // modules with latency (clipper and limiter oversampling/look-ahead) plus EQ and bus comp.
            // Multiband, Low-End, Saturation and Stereo split the signal with minimum-phase allpass
            // crossovers, which move the peak of an impulse by design; their latency is covered by the
            // bit-exact bypass test.
            for (auto id : { "eq_on", "bus_on", "clip_on", "lim_on" })
                h.set (id, 1.0f);
            h.set ("quality", (float) quality);
            h.set ("lim_lookahead", lookahead);
            h.set ("lim_ceiling", 0.0f);
            h.set ("clip_thr", 0.0f);
            h.set ("lem_mono_on", 0.0f);
            for (int b = 1; b <= 5; ++b)
                h.set (mm::params::mb (b, "ratio"), 1.0f);
            h.set ("bus_thr", 0.0f);
            h.chain.prepare (48000.0, 512, false);
            const int latency = h.chain.latencySamples();
            juce::AudioBuffer<float> b (2, 8192);
            b.clear();
            mmtest::runChain (h, b, 512); // let module fades settle
            b.clear();
            b.setSample (0, 0, 0.25f);
            b.setSample (1, 0, 0.25f);
            mmtest::runChain (h, b, 512);
            int peakPos = 0;
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (std::abs (b.getSample (0, i)) > std::abs (b.getSample (0, peakPos)))
                    peakPos = i;
            INFO ("quality " << quality << " lookahead " << lookahead);
            CHECK (peakPos == latency);
        }
}

TEST_CASE ("Linear-phase EQ reports its latency and the impulse lands on it", "[dsp][latency]")
{
    Host h;
    h.set ("eq_on", 1.0f);
    h.set ("eq_phase", 1.0f);
    h.set ("eq_b5_on", 1.0f);
    h.set ("eq_b5_gain", 3.0f);
    h.chain.prepare (48000.0, 512, false);
    h.chain.updateLinearPhase();
    const int latency = h.chain.latencySamples();
    REQUIRE (latency >= 2048);

    // the convolution engine swaps the new kernel in asynchronously: run silence until it is active
    juce::AudioBuffer<float> b (2, 512);
    for (int i = 0; i < 400; ++i)
    {
        b.clear();
        h.chain.process (b);
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    juce::AudioBuffer<float> sig (2, latency + 4096);
    sig.clear();
    sig.setSample (0, 0, 0.5f);
    sig.setSample (1, 0, 0.5f);
    mmtest::runChain (h, sig, 512);
    int peakPos = 0;
    for (int i = 0; i < sig.getNumSamples(); ++i)
        if (std::abs (sig.getSample (0, i)) > std::abs (sig.getSample (0, peakPos)))
            peakPos = i;
    CHECK (peakPos == latency);
}

TEST_CASE ("EQ bands match the RBJ biquad reference within 0.05 dB", "[dsp][eq]")
{
    const double fs = 48000.0;
    struct Case { int band; int type; float freq, gain, q; };
    for (const auto& c : { Case { 5, 0, 1000.0f, 6.0f, 1.0f }, Case { 3, 0, 250.0f, -9.0f, 4.0f },
                           Case { 4, 1, 400.0f, 4.0f, 0.71f }, Case { 6, 2, 3000.0f, -5.0f, 0.71f } })
    {
        Host h;
        h.set ("eq_on", 1.0f);
        h.set (mm::params::eq (c.band, "on"), 1.0f);
        h.set (mm::params::eq (c.band, "type"), (float) c.type);
        h.set (mm::params::eq (c.band, "freq"), c.freq);
        h.set (mm::params::eq (c.band, "gain"), c.gain);
        h.set (mm::params::eq (c.band, "q"), c.q);
        h.chain.prepare (fs, 512, false);
        const double fq = h.get (mm::params::eq (c.band, "freq")), g = h.get (mm::params::eq (c.band, "gain")),
                     q = h.get (mm::params::eq (c.band, "q"));
        for (double f : { 100.0, 250.0, 700.0, 1000.0, 2000.0, 5000.0 })
        {
            mm::dsp::Coeffs ref;
            if (c.type == 0)
                ref = mm::dsp::Coeffs::peak (fs, fq, q, g);
            else if (c.type == 1)
                ref = mm::dsp::Coeffs::lowShelf (fs, fq, q, g);
            else
                ref = mm::dsp::Coeffs::highShelf (fs, fq, q, g);
            Host h2;
            h2.set ("eq_on", 1.0f);
            h2.set (mm::params::eq (c.band, "on"), 1.0f);
            h2.set (mm::params::eq (c.band, "type"), (float) c.type);
            h2.set (mm::params::eq (c.band, "freq"), c.freq);
            h2.set (mm::params::eq (c.band, "gain"), c.gain);
            h2.set (mm::params::eq (c.band, "q"), c.q);
            h2.chain.prepare (fs, 512, false);
            INFO ("band " << c.band << " f " << f);
            CHECK (sineGainDb (h2, fs, f) == Approx (ref.magnitudeDb (fs, f)).margin (0.05));
        }
    }
}

TEST_CASE ("Linkwitz-Riley crossover bands sum flat within 0.01 dB", "[dsp][crossover]")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        Host h;
        h.set ("mb_on", 1.0f);
        for (int b = 1; b <= 5; ++b)
            h.set (mm::params::mb (b, "ratio"), 1.0f);
        h.chain.prepare (fs, 512, false);
        juce::AudioBuffer<float> b (2, 1 << 15);
        b.clear();
        mmtest::runChain (h, b, 512);
        b.clear();
        b.setSample (0, 0, 1.0f);
        b.setSample (1, 0, 1.0f);
        mmtest::runChain (h, b, 512);
        const int order = 15;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) 2 << order, 0.0f);
        std::copy (b.getReadPointer (0), b.getReadPointer (0) + (1 << order), data.begin());
        fft.performFrequencyOnlyForwardTransform (data.data());
        double worst = 0.0;
        for (int k = 1; k < (1 << order) / 2; ++k)
        {
            const double f = k * fs / (1 << order);
            if (f < 20.0 || f > 20000.0)
                continue;
            worst = std::max (worst, std::abs (20.0 * std::log10 (data[(size_t) k])));
        }
        INFO ("fs " << fs);
        CHECK (worst < 0.01);
    }
}

TEST_CASE ("Mono maker leaves signals above its frequency unchanged", "[dsp][lowend]")
{
    const double fs = 48000.0;
    Host h;
    h.set ("lem_on", 1.0f);
    h.set ("lem_mono_freq", 120.0f);
    h.chain.prepare (fs, 512, false);
    const int n = 48000;
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        b.setSample (0, i, (float) (0.5 * std::sin (twoPi * 1000.0 * i / fs)));
        b.setSample (1, i, 0.0f); // fully one-sided signal: pure side component above the crossover
    }
    mmtest::runChain (h, b, 512);
    double l = 0.0, r = 0.0;
    for (int i = n / 2; i < n; ++i)
    {
        l = std::max (l, (double) std::abs (b.getSample (0, i)));
        r = std::max (r, (double) std::abs (b.getSample (1, i)));
    }
    CHECK (20.0 * std::log10 (l / 0.5) == Approx (0.0).margin (0.05));
    CHECK (r < 0.5 * 0.01); // the other channel stays below -40 dB

    // below the frequency the side signal is removed
    for (int i = 0; i < n; ++i)
    {
        b.setSample (0, i, (float) (0.5 * std::sin (twoPi * 40.0 * i / fs)));
        b.setSample (1, i, (float) (-0.5 * std::sin (twoPi * 40.0 * i / fs)));
    }
    mmtest::runChain (h, b, 512);
    double peak = 0.0;
    for (int i = n / 2; i < n; ++i)
        peak = std::max (peak, (double) std::abs (b.getSample (0, i)));
    CHECK (peak < 0.5 * 0.1); // at least 20 dB of side reduction at 40 Hz
}

TEST_CASE ("Limiter true peak stays at or below the ceiling + 0.05 dB with +24 dB drive", "[dsp][limiter]")
{
    const double fs = 48000.0;
    const int n = (int) (fs * 6.0);
    juce::Random random (7);

    // pink noise (Paul Kellet filter), a sine sweep and a synthetic kick
    juce::AudioBuffer<float> pink (2, n), sweep (2, n), kick (2, n);
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = random.nextDouble() * 2.0 - 1.0;
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double pk = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362) * 0.11;
        b6 = w * 0.115926;
        pink.setSample (0, i, (float) pk);
        pink.setSample (1, i, (float) (pk * 0.9));
        const double t = (double) i / fs;
        const double fsw = 20.0 * std::pow (1000.0, t / 6.0);
        const double ph = twoPi * 20.0 * 6.0 / std::log (1000.0) * (std::pow (1000.0, t / 6.0) - 1.0);
        juce::ignoreUnused (fsw);
        sweep.setSample (0, i, (float) (0.5 * std::sin (ph)));
        sweep.setSample (1, i, (float) (0.5 * std::sin (ph + 0.3)));
        const double tk = std::fmod (t, 0.5);
        const double kk = std::exp (-tk * 30.0) * std::sin (twoPi * (50.0 + 150.0 * std::exp (-tk * 40.0)) * tk) * 0.7
                          + std::exp (-tk * 300.0) * (random.nextDouble() - 0.5) * 0.6;
        kick.setSample (0, i, (float) kk);
        kick.setSample (1, i, (float) kk);
    }

    for (auto* sig : { &pink, &sweep, &kick })
        for (int mode : { 0, 1, 2 })
        {
            Host h;
            h.set ("lim_on", 1.0f);
            h.set ("lim_drive", 24.0f);
            h.set ("lim_ceiling", -1.0f);
            h.set ("lim_mode", (float) mode);
            h.chain.prepare (fs, 512, false);
            juce::AudioBuffer<float> b;
            b.makeCopyOf (*sig);
            mmtest::runChain (h, b, 512);
            int where = 0;
            const double tp = independentTruePeakDb (b, &where);
            double sp = 0.0;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    sp = std::max (sp, (double) std::abs (b.getSample (ch, i)));
            INFO ("signal " << (sig == &pink ? "pink" : sig == &sweep ? "sweep" : "kick") << " mode " << mode
                            << " true peak " << tp << " at " << where << " sample peak " << 20.0 * std::log10 (sp));
            CHECK (tp <= -1.0 + 0.05);
        }
}

namespace
{
/** Amplitude of `freq` in channel 0 over [from, from + len) (Goertzel, Hann window). */
double toneAmp (const juce::AudioBuffer<float>& b, double fs, double freq, int from, int len)
{
    double re = 0.0, im = 0.0, wsum = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (twoPi * i / (len - 1));
        const double x = b.getSample (0, from + i) * w;
        re += x * std::cos (twoPi * freq * i / fs);
        im -= x * std::sin (twoPi * freq * i / fs);
        wsum += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / wsum;
}

juce::AudioBuffer<float> limitSine (int mode, double freq, double amp, float driveDb, double fs, int n)
{
    Host h;
    h.set ("lim_on", 1.0f);
    h.set ("lim_drive", driveDb);
    h.set ("lim_ceiling", -1.0f);
    h.set ("lim_mode", (float) mode);
    h.chain.prepare (fs, 512, false);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = (float) (amp * std::sin (twoPi * freq * i / fs));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    mmtest::runChain (h, b, 512);
    return b;
}
} // namespace

TEST_CASE ("Limiter keeps low distortion on a bass sine with deep gain reduction", "[dsp][limiter]")
{
    // A 55 Hz sine at -6 dBFS with +12 dB drive needs about 7 dB of gain reduction. A limiter whose
    // gain follows the waveform modulates the bass and produces harmonics ("crackle"); the hold
    // stage keeps the gain steady over the bass period (D-036).
    const double fs = 48000.0;
    const int n = (int) fs * 4;
    for (int mode : { 0, 1, 2 })
    {
        const auto b = limitSine (mode, 55.0, 0.5, 12.0f, fs, n);
        const int from = n / 2, len = n / 2;
        const double fund = toneAmp (b, fs, 55.0, from, len);
        double harm = 0.0;
        for (int k = 2; k <= 12; ++k)
            harm += std::pow (toneAmp (b, fs, 55.0 * k, from, len), 2.0);
        const double thd = std::sqrt (harm) / fund;
        INFO ("mode " << mode << " THD " << 100.0 * thd << " % fundamental " << 20.0 * std::log10 (fund));
        CHECK (thd < 0.003); // below 0.3 %
        CHECK (20.0 * std::log10 (fund) > -1.6); // the sine is limited to the ceiling, not below it
    }
}

TEST_CASE ("Limiter does not modulate a guitar tone with the bass", "[dsp][limiter]")
{
    // 50 Hz bass plus a 1 kHz tone, 6 dB of gain reduction: gain that follows the bass waveform
    // puts sidebands at 1 kHz +- 100 Hz on the tone (intermodulation, heard as crackle on guitars)
    const double fs = 48000.0;
    const int n = (int) fs * 4;
    for (int mode : { 0, 1, 2 })
    {
        Host h;
        h.set ("lim_on", 1.0f);
        h.set ("lim_drive", 10.0f);
        h.set ("lim_ceiling", -1.0f);
        h.set ("lim_mode", (float) mode);
        h.chain.prepare (fs, 512, false);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float v = (float) (0.45 * std::sin (twoPi * 50.0 * i / fs) + 0.15 * std::sin (twoPi * 1000.0 * i / fs));
            b.setSample (0, i, v);
            b.setSample (1, i, v);
        }
        mmtest::runChain (h, b, 512);
        const int from = n / 2, len = n / 2;
        const double tone = toneAmp (b, fs, 1000.0, from, len);
        double side = 0.0;
        for (double f : { 900.0, 1100.0, 800.0, 1200.0 })
            side += std::pow (toneAmp (b, fs, f, from, len), 2.0);
        const double imd = std::sqrt (side) / tone;
        INFO ("mode " << mode << " IMD " << 100.0 * imd << " %");
        CHECK (imd < 0.003);
    }
}

TEST_CASE ("Limiter recovers from a single short peak without a long hole", "[dsp][limiter]")
{
    // a quiet 1 kHz tone with one 3 ms burst 12 dB over the ceiling: the slow stage must not hold
    // the reduction long after a single drum hit
    const double fs = 48000.0;
    const int n = (int) fs * 2;
    for (int mode : { 0, 1, 2 })
    {
        Host h;
        h.set ("lim_on", 1.0f);
        h.set ("lim_ceiling", -1.0f);
        h.set ("lim_mode", (float) mode);
        h.chain.prepare (fs, 512, false);
        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            double v = 0.1 * std::sin (twoPi * 1000.0 * i / fs);
            if (i >= (int) fs / 2 && i < (int) fs / 2 + 144)
                v *= 36.0;
            b.setSample (0, i, (float) v);
            b.setSample (1, i, (float) v);
        }
        mmtest::runChain (h, b, 512);
        const double before = toneAmp (b, fs, 1000.0, (int) (0.2 * fs), 4800);
        const double after = toneAmp (b, fs, 1000.0, (int) (1.2 * fs), 4800); // 700 ms after the peak
        INFO ("mode " << mode << " level after the peak " << 20.0 * std::log10 (after / before) << " dB");
        CHECK (after / before > std::pow (10.0, -1.0 / 20.0));
    }
}

TEST_CASE ("Inflator curve keeps 0 and full scale and rises in between", "[dsp][inflator]")
{
    for (double curve : { -50.0, -25.0, 0.0, 25.0, 50.0 })
    {
        CHECK (mm::dsp::Inflator::shape (0.0, curve) == Approx (0.0).margin (1e-12));
        CHECK (mm::dsp::Inflator::shape (1.0, curve) == Approx (1.0).margin (1e-12));
        CHECK (mm::dsp::Inflator::shape (-1.0, curve) == Approx (-1.0).margin (1e-12));
        double last = 0.0;
        for (int k = 1; k <= 100; ++k)
        {
            const double x = k / 100.0, y = mm::dsp::Inflator::shape (x, curve);
            CHECK (y >= x - 1e-12); // quiet parts never get quieter
            CHECK (y >= last - 1e-12); // monotonic
            last = y;
        }
    }
    // the JS Inflator values at Curve 0: 0.5 -> 0.6875 - 0.0625 * (0.25 - 0.25 + 0.0625)
    CHECK (mm::dsp::Inflator::shape (0.5, 0.0) == Approx (0.75 - 0.0625 - 0.0625 * 0.0625).margin (1e-12));
}

TEST_CASE ("Inflator with Effect 0 is a pure delay and adds loudness with Effect", "[dsp][inflator]")
{
    const double fs = 48000.0;
    const int n = 48000;
    juce::AudioBuffer<float> in (2, n);
    for (int i = 0; i < n; ++i)
    {
        // tones only: the oversampling filters have a fractional delay that broadband noise would show
        const float v = (float) (0.25 * std::sin (twoPi * 110.0 * i / fs) + 0.1 * std::sin (twoPi * 330.0 * i / fs + 0.4));
        in.setSample (0, i, v);
        in.setSample (1, i, v * 0.8f);
    }
    double rmsByEffect[2] = {};
    for (int k = 0; k < 2; ++k)
    {
        Host h;
        h.set ("clip_on", 1.0f);
        h.set ("infl_effect", k == 0 ? 0.0f : 100.0f);
        h.set ("infl_clip", 0.0f);
        h.chain.prepare (fs, 512, false);
        const int latency = h.chain.latencySamples();
        juce::AudioBuffer<float> b;
        b.makeCopyOf (in);
        mmtest::runChain (h, b, 512);
        double sum = 0.0, err = 0.0;
        for (int i = n / 2; i < n; ++i)
        {
            sum += (double) b.getSample (0, i) * b.getSample (0, i);
            err = std::max (err, (double) std::abs (b.getSample (0, i) - in.getSample (0, i - latency)));
        }
        rmsByEffect[k] = std::sqrt (sum / (n / 2));
        if (k == 0)
            CHECK (err < 6.0e-3); // only the oversampling filters remain (sub-sample delay)
    }
    const double gainDb = 20.0 * std::log10 (rmsByEffect[1] / rmsByEffect[0]);
    INFO ("Inflator gain at Effect 100 %: " << gainDb << " dB");
    CHECK (gainDb > 1.0);
}

TEST_CASE ("Automating every continuous parameter causes no clicks", "[dsp][automation]")
{
    const double fs = 48000.0;
    const int n = 256 * 94;
    for (const auto& spec : mm::params::all())
    {
        if (spec.kind != mm::params::Kind::floating || ! spec.automatable)
            continue;
        std::fprintf (stderr, "automation %s\n", spec.id.toRawUTF8());
        std::fflush (stderr);
        Host h;
        // enable the module that owns the parameter
        const auto prefix = spec.id.upToFirstOccurrenceOf ("_", false, false);
        static const std::map<juce::String, juce::String> onIds { { "in", "in_on" }, { "hpf", "hpf_on" }, { "eq", "eq_on" },
                                                                 { "mb", "mb_on" }, { "lem", "lem_on" }, { "bus", "bus_on" },
                                                                 { "sat", "sat_on" }, { "st", "st_on" }, { "clip", "clip_on" },
                                                                 { "lim", "lim_on" }, { "out", "out_on" } };
        if (auto it = onIds.find (prefix); it != onIds.end())
            h.set (it->second, 1.0f);
        if (prefix == "hpf")
            h.set ("in_on", 1.0f);
        if (prefix == "eq")
        {
            const auto band = spec.id.fromFirstOccurrenceOf ("eq_b", false, false).upToFirstOccurrenceOf ("_", false, false);
            h.set ("eq_b" + band + "_on", 1.0f);
            h.set ("eq_b" + band + "_gain", 6.0f);
            h.set ("eq_b" + band + "_dyn", 1.0f);
        }
        if (prefix == "lem")
        {
            h.set ("lem_rot_on", 1.0f);
            h.set ("lem_tr_on", 1.0f);
        }
        h.set (spec.id, spec.minValue);
        h.chain.prepare (fs, 256, false);

        juce::AudioBuffer<float> b (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float x = (float) (0.25 * std::sin (twoPi * 220.0 * i / fs));
            b.setSample (0, i, x);
            b.setSample (1, i, x * 0.8f);
        }
        for (int start = 0; start < n; start += 256)
        {
            const float t = (float) start / (float) n;
            h.set (spec.id, spec.minValue + t * (spec.maxValue - spec.minValue));
            float* chans[2] = { b.getWritePointer (0) + start, b.getWritePointer (1) + start };
            juce::AudioBuffer<float> view (chans, 2, 256);
            h.chain.process (view);
        }
        double worst = 0.0;
        int worstAt = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 2; i < n; ++i)
            {
                const double d2 = b.getSample (ch, i) - 2.0 * b.getSample (ch, i - 1) + b.getSample (ch, i - 2);
                if (std::abs (d2) > worst)
                {
                    worst = std::abs (d2);
                    worstAt = i;
                }
            }
        INFO (spec.id << " worst second difference " << worst << " at sample " << worstAt << " value " << b.getSample (0, worstAt));
        CHECK (worst < 0.1);
    }
}

TEST_CASE ("Resonance suppressor cuts a sticking-out tone and keeps its latency", "[dsp][resonance]")
{
    const double fs = 48000.0;
    const int n = 96000;
    juce::Random random (3);
    juce::AudioBuffer<float> src (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = (float) (0.02 * (random.nextDouble() * 2.0 - 1.0) + 0.3 * std::sin (twoPi * 2500.0 * i / fs));
        src.setSample (0, i, v);
        src.setSample (1, i, v);
    }
    const auto toneLevel = [&] (const juce::AudioBuffer<float>& b)
    {
        double re = 0, im = 0;
        for (int i = n / 2; i < n; ++i)
        {
            re += b.getSample (0, i) * std::cos (twoPi * 2500.0 * i / fs);
            im += b.getSample (0, i) * std::sin (twoPi * 2500.0 * i / fs);
        }
        return 20.0 * std::log10 (std::sqrt (re * re + im * im) / (n / 4));
    };
    Host h;
    h.set ("eq_on", 1.0f);
    h.set ("rs_on", 1.0f);
    h.set ("rs_depth", 12.0f);
    h.chain.prepare (fs, 512, false);
    CHECK (h.chain.latencySamples() >= 2048);
    juce::AudioBuffer<float> b;
    b.makeCopyOf (src);
    mmtest::runChain (h, b, 512);
    const double before = toneLevel (src), after = toneLevel (b);
    INFO ("tone before " << before << " dB, after " << after << " dB");
    CHECK (after < before - 6.0);

    // impulse latency with the suppressor on and nothing to suppress
    Host h2;
    h2.set ("eq_on", 1.0f);
    h2.set ("rs_on", 1.0f);
    h2.set ("rs_depth", 0.0f);
    h2.chain.prepare (fs, 512, false);
    const int latency = h2.chain.latencySamples();
    juce::AudioBuffer<float> imp (2, latency + 4096);
    imp.clear();
    imp.setSample (0, 100, 0.5f);
    imp.setSample (1, 100, 0.5f);
    mmtest::runChain (h2, imp, 512);
    int peakPos = 0;
    for (int i = 0; i < imp.getNumSamples(); ++i)
        if (std::abs (imp.getSample (0, i)) > std::abs (imp.getSample (0, peakPos)))
            peakPos = i;
    CHECK (peakPos == 100 + latency);
    CHECK (imp.getSample (0, peakPos) == Catch::Approx (0.5f).margin (0.01f));
}
