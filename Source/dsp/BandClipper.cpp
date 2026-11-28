#include "dsp/BandClipper.h"

#include <cmath>

namespace mm::dsp
{
namespace
{
/** Soft clip at threshold t: linear up to 0.7 t, then a tanh knee that reaches t (smooth slope). */
inline double softClip (double x, double t) noexcept
{
    const double a = std::abs (x), knee = 0.7 * t;
    if (a <= knee)
        return x;
    const double y = knee + (t - knee) * std::tanh ((a - knee) / (t - knee));
    return x < 0.0 ? -y : y;
}
} // namespace

void BandClipper::prepare (double sampleRate, int maxBlock)
{
    fs = sampleRate;
    os.prepare (2, 2, maxBlock); // 4x: less aliasing above 20 kHz, which the true-peak detection misses
    os.setUseHigh (false);
    const double rate = 4.0 * fs;
    for (auto& c : ch)
    {
        c.split1k.setup (4, rate, 1000.0);
        c.split120.setup (4, rate, 120.0);
        c.split5k.setup (4, rate, 5000.0);
        c.split5k.makeAllpass (c.ap[0]); // the low branch gets the phase of the 5 kHz split
        c.split5k.makeAllpass (c.ap[1]);
        c.split120.makeAllpass (c.ap[2]); // the high branch gets the phase of the 120 Hz split
        c.split120.makeAllpass (c.ap[3]);
        // 4th-order Butterworth low-pass at 18 kHz for the clipping residual: the products above it
        // are inaudible and the true-peak detection of the limiter would underestimate them
        const Coeffs lp[2] = { Coeffs::lowpass (rate, 18000.0, 0.5412), Coeffs::lowpass (rate, 18000.0, 1.3066) };
        c.residualLp.setSections (lp, 2);
    }
    envRelease = std::exp (-1.0 / (1.5 * rate));
    msCoeff = std::exp (-1.0 / (0.3 * rate));
    dry.prepare (2, os.latency() + 4);
    work.setSize (2, maxBlock);
    mixStep = 1.0 / (0.02 * fs);
    reset();
}

void BandClipper::reset()
{
    os.reset();
    for (auto& c : ch)
    {
        c.split1k.reset();
        c.split120.reset();
        c.split5k.reset();
        for (auto& a : c.ap)
            a.reset();
        c.residualLp.reset();
    }
    env.fill (0.0);
    ms.fill (0.0);
    dry.setDelay (os.latency());
    dry.reset();
    mix = amountDb > 0.01f ? 1.0 : 0.0;
    meter.set (0.0f);
}

void BandClipper::process (float* left, float* right, int numSamples) noexcept
{
    const double wantMix = amountDb > 0.01f ? 1.0 : 0.0;
    if (mix <= 0.0 && wantMix <= 0.0)
    {
        // bypassed: only the latency
        float* io[2] = { left, right };
        dry.process (io, 2, numSamples);
        meter.set (0.0f);
        return;
    }
    // processed copy through the oversampled band clipper
    work.copyFrom (0, 0, left, numSamples);
    work.copyFrom (1, 0, right, numSamples);
    float* chans[2] = { work.getWritePointer (0), work.getWritePointer (1) };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) numSamples);
    auto up = os.up (block);
    const int n2 = (int) up.getNumSamples();
    const double gain = std::pow (10.0, -std::clamp ((double) amountDb, 0.0, 12.0) / 20.0);
    float* u[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
    double maxIn = 0.0, maxOut = 0.0;
    for (int i = 0; i < n2; ++i)
    {
        std::array<std::array<double, numBands>, 2> b {};
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[(size_t) c];
            double lo, hi, b0, b1, b2, b3;
            s.split1k.process (u[c][i], lo, hi);
            s.split120.process (lo, b0, b1);
            s.split5k.process (hi, b2, b3);
            b[(size_t) c] = { s.ap[0].process (b0), s.ap[1].process (b1), s.ap[2].process (b2), s.ap[3].process (b3) };
        }
        double y[2] = { 0.0, 0.0 }, r[2] = { 0.0, 0.0 };
        for (int k = 0; k < numBands; ++k)
        {
            const double pk = std::max (std::abs (b[0][(size_t) k]), std::abs (b[1][(size_t) k]));
            auto& e = env[(size_t) k];
            e = pk > e ? pk : pk + envRelease * (e - pk);
            // only peaks that stand 6 dB above the band's own level are clipped (D-044): a steady
            // tone (crest 3 dB) passes clean, drum hits lose up to the amount
            auto& m = ms[(size_t) k];
            const double x2 = 0.5 * (b[0][(size_t) k] * b[0][(size_t) k] + b[1][(size_t) k] * b[1][(size_t) k]);
            m = x2 + msCoeff * (m - x2);
            const double t = std::max ({ 1.0e-6, e * gain, 2.0 * std::sqrt (m) });
            for (int c = 0; c < 2; ++c)
            {
                const double x = b[(size_t) c][(size_t) k];
                const double v = softClip (x, t);
                y[c] += x;     // the unclipped bands (an allpass of the input)
                r[c] += v - x; // the clipping residual
                maxIn = std::max (maxIn, std::abs (x));
                maxOut = std::max (maxOut, std::abs (v));
            }
        }
        u[0][i] = (float) (y[0] + ch[0].residualLp.process (r[0]));
        u[1][i] = (float) (y[1] + ch[1].residualLp.process (r[1]));
    }
    os.down (block);
    // the direct path with the same latency, then the crossfade (20 ms) when the amount crosses zero
    float* io[2] = { left, right };
    dry.process (io, 2, numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        mix = wantMix > mix ? std::min (1.0, mix + mixStep) : std::max (0.0, mix - mixStep);
        left[i] += (float) mix * (chans[0][i] - left[i]);
        right[i] += (float) mix * (chans[1][i] - right[i]);
    }
    meter.set (maxIn > 1.0e-9 && maxOut > 1.0e-9 ? (float) (20.0 * std::log10 (maxIn / maxOut)) : 0.0f);
}
} // namespace mm::dsp
