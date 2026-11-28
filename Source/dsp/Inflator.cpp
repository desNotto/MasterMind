// MasterMind Inflator module (D-038). The curve, its coefficients, the clip stages and the band
// split follow JS Inflator by yg331 (Kiriki-liszt), Copyright (c) 2024 yg331, GPL-3.0.
#include "dsp/Inflator.h"

namespace mm::dsp
{
double Inflator::shape (double x, double curveValue) noexcept
{
    // JS Inflator: curvepct = Curve - 0.5 (here -50 .. +50 -> -0.5 .. +0.5)
    const double c = std::clamp (curveValue, -50.0, 50.0) * 0.01;
    const double A = 1.5 + c;
    const double B = -(c + c);
    const double C = c - 0.5;
    const double D = 0.0625 - c * 0.25 + (c * c) * 0.25;

    const double sign = x > 0.0 ? 1.0 : -1.0;
    const double s1 = std::abs (x);
    const double s2 = s1 * s1;
    const double s3 = s2 * s1;
    const double s4 = s2 * s2;

    double y;
    if (s1 >= 2.0)
        y = 0.0;
    else if (s1 > 1.0)
        y = (2.0 * s1) - s2;
    else
        y = (A * s1) + (B * s2) + (C * s3) - (D * (s2 - (2.0 * s3) + s4));
    return y * sign;
}

double Inflator::transfer (double x, double curveValue, double effectAmount, bool clipOn) noexcept
{
    double v = clipOn ? std::clamp (x, -1.0, 1.0) : std::clamp (x, -2.0, 2.0);
    double y = shape (v, curveValue);
    if (clipOn)
        y = std::clamp (y, -1.0, 1.0);
    return (1.0 - effectAmount) * v + effectAmount * y;
}

void Inflator::BandSplit::set (double fcLow, double fcHigh, double rate) noexcept
{
    LP.C = 0.5 * std::tan (juce::MathConstants<double>::pi * ((fcLow / rate) - 0.25)) + 0.5;
    HP.C = 0.5 * std::tan (juce::MathConstants<double>::pi * ((fcHigh / rate) - 0.25)) + 0.5;
    G = HP.C * (1.0 - LP.C) / (HP.C - LP.C);
    GR = 1.0 / G;
    clear();
}

void Inflator::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    os.prepare (2, 3, ctx.maxBlock); // 4x normal, 8x high (as the Clipper: the latency does not change)
    capIn.assign ((size_t) captureSize, 0.0f);
    capOut.assign ((size_t) captureSize, 0.0f);
    scratchIn.assign ((size_t) std::max (1, ctx.maxBlock), 0.0f);
    energyCoeff = std::exp (-1.0 / (0.3 * fs));
    reset();
}

void Inflator::reset()
{
    os.reset();
    lastRate = 0.0;
    for (auto& s : split)
        s.clear();
    inGain.reset (dbToGain (settings.inputDb));
    outGain.reset (dbToGain (settings.outputDb));
    effect.reset (settings.effect);
    curve.reset (settings.curve);
    energyIn = energyOut = 0.0;
    inMeter.set (-100.0f);
    inflMeter.set (0.0f);
    clipMeter.set (0.0f);
}

int Inflator::readCapture (float* in, float* out, int maxSamples) noexcept
{
    const auto scope = capture.read (std::min (maxSamples, capture.getNumReady()));
    int n = 0;
    for (int k = 0; k < scope.blockSize1; ++k, ++n)
    {
        in[n] = capIn[(size_t) (scope.startIndex1 + k)];
        out[n] = capOut[(size_t) (scope.startIndex1 + k)];
    }
    for (int k = 0; k < scope.blockSize2; ++k, ++n)
    {
        in[n] = capIn[(size_t) (scope.startIndex2 + k)];
        out[n] = capOut[(size_t) (scope.startIndex2 + k)];
    }
    return n;
}

void Inflator::process (float* left, float* right, int numSamples) noexcept
{
    // Input gain (ramped); the input clip of JS Inflator is applied at the oversampled rate below
    inGain.setTarget (dbToGain (settings.inputDb), numSamples);
    const int cap = std::min (numSamples, (int) scratchIn.size());
    double inPeak = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = (float) inGain.next();
        left[i] *= g;
        right[i] *= g;
        inPeak = std::max (inPeak, (double) std::max (std::abs (left[i]), std::abs (right[i])));
        if (i < cap)
            scratchIn[(size_t) i] = 0.5f * (left[i] + right[i]);
    }

    float* chans[2] = { left, right };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) numSamples);
    auto up = os.up (block);
    const double rate = fs * os.factor();
    if (rate != lastRate)
    {
        for (auto& s : split)
            s.set (240.0, 2400.0, rate);
        lastRate = rate;
    }

    const int n = (int) up.getNumSamples();
    effect.setTarget (settings.effect, n);
    curve.setTarget (settings.curve, n);
    const bool clipOn = settings.clip, splitOn = settings.split;
    double sumIn = 0.0, sumOut = 0.0, prePeak = 0.0, postPeak = 0.0;
    float* d[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
    for (int i = 0; i < n; ++i)
    {
        const double e = effect.next(), cv = curve.next();
        for (int c = 0; c < 2; ++c)
        {
            double x = d[c][i];
            prePeak = std::max (prePeak, std::abs (x));
            if (clipOn)
                x = std::clamp (x, -1.0, 1.0);
            x = std::clamp (x, -2.0, 2.0);

            double y;
            if (splitOn)
            {
                auto& b = split[(size_t) c];
                b.LP.R = b.LP.I + b.LP.C * (x - b.LP.I);
                b.LP.I = 2.0 * b.LP.R - b.LP.I;
                b.HP.R = (1.0 - b.HP.C) * b.HP.I + b.HP.C * x;
                b.HP.I = 2.0 * b.HP.R - b.HP.I;
                const double lo = b.LP.R, hi = x - b.HP.R, mid = b.HP.R - b.LP.R;
                y = shape (lo, cv) + shape (mid * b.G, cv) * b.GR + shape (hi, cv);
            }
            else
                y = shape (x, cv);
            if (clipOn)
                y = std::clamp (y, -1.0, 1.0);

            const double out = (1.0 - e) * x + e * y; // Effect: dry and curve mix
            postPeak = std::max (postPeak, std::abs (out));
            sumIn += x * x;
            sumOut += out * out;
            d[c][i] = (float) out;
        }
    }
    os.down (block);

    // loudness the module adds (smoothed energies), clip reduction, input peak
    const double k = std::pow (energyCoeff, (double) numSamples);
    energyIn = sumIn / std::max (1, n) + k * (energyIn - sumIn / std::max (1, n));
    energyOut = sumOut / std::max (1, n) + k * (energyOut - sumOut / std::max (1, n));
    inflMeter.set (energyIn > 1.0e-10 ? (float) (10.0 * std::log10 ((energyOut + 1.0e-12) / energyIn)) : 0.0f);
    clipMeter.set (clipOn && prePeak > 1.0 ? (float) -gainToDb (prePeak) : 0.0f);
    juce::ignoreUnused (postPeak);
    inMeter.set ((float) gainToDb (inPeak + 1.0e-9));

    {
        const auto scope = capture.write (std::min (cap, capture.getFreeSpace()));
        int j = 0;
        for (int q = 0; q < scope.blockSize1; ++q, ++j)
        {
            capIn[(size_t) (scope.startIndex1 + q)] = scratchIn[(size_t) j];
            capOut[(size_t) (scope.startIndex1 + q)] = 0.5f * (left[j] + right[j]);
        }
        for (int q = 0; q < scope.blockSize2; ++q, ++j)
        {
            capIn[(size_t) (scope.startIndex2 + q)] = scratchIn[(size_t) j];
            capOut[(size_t) (scope.startIndex2 + q)] = 0.5f * (left[j] + right[j]);
        }
    }

    outGain.setTarget (dbToGain (settings.outputDb), numSamples);
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = (float) outGain.next();
        left[i] *= g;
        right[i] *= g;
    }
}
} // namespace mm::dsp
