#include "analysis/Loudness.h"

#include <algorithm>
#include <cmath>

namespace mm::analysis
{
namespace
{
constexpr double silence = -1000.0;

double energyToLufs (double meanSquareSum)
{
    return meanSquareSum <= 1.0e-20 ? silence : LoudnessMeter::kWeightedOffset() + 10.0 * std::log10 (meanSquareSum);
}

double lufsToEnergy (double lufs) { return std::pow (10.0, (lufs - LoudnessMeter::kWeightedOffset()) / 10.0); }
} // namespace

// ---- K-weighting (coefficient formulas valid for any sample rate) -------------------------------
dsp::Coeffs kWeightingShelf (double fs)
{
    const double f0 = 1681.974450955533, gDb = 3.999843853973347, q = 0.7071752369554196;
    const double k = std::tan (dsp::pi * f0 / fs);
    const double vh = std::pow (10.0, gDb / 20.0);
    const double vb = std::pow (vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    return { (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
             2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
}

dsp::Coeffs kWeightingHighpass (double fs)
{
    const double f0 = 38.13547087602444, q = 0.5003270373238773;
    const double k = std::tan (dsp::pi * f0 / fs);
    const double a0 = 1.0 + k / q + k * k;
    return { 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0 };
}

// ---- Histogram ------------------------------------------------------------------------------------
void LoudnessHistogram::add (double lufs)
{
    if (lufs < minDb)
        return;
    const int i = std::min (bins - 1, (int) std::lround ((lufs - minDb) / step));
    ++counts[(size_t) i];
    ++total;
}

double LoudnessHistogram::gatedMean (double relGate) const
{
    if (total == 0)
        return silence;

    // mean energy of all blocks above the absolute gate
    double sum = 0.0;
    uint64_t n = 0;
    for (int i = 0; i < bins; ++i)
        if (counts[(size_t) i] != 0)
        {
            sum += counts[(size_t) i] * lufsToEnergy (binDb (i));
            n += counts[(size_t) i];
        }
    const double relThreshold = energyToLufs (sum / (double) n) + relGate;

    sum = 0.0;
    n = 0;
    for (int i = std::max (0, (int) std::ceil ((relThreshold - minDb) / step - 1.0e-9)); i < bins; ++i)
        if (counts[(size_t) i] != 0 && binDb (i) > relThreshold)
        {
            sum += counts[(size_t) i] * lufsToEnergy (binDb (i));
            n += counts[(size_t) i];
        }
    return n == 0 ? silence : energyToLufs (sum / (double) n);
}

double LoudnessHistogram::range() const
{
    if (total == 0)
        return 0.0;

    double sum = 0.0;
    uint64_t n = 0;
    for (int i = 0; i < bins; ++i)
        if (counts[(size_t) i] != 0)
        {
            sum += counts[(size_t) i] * lufsToEnergy (binDb (i));
            n += counts[(size_t) i];
        }
    const double relThreshold = energyToLufs (sum / (double) n) - 20.0;

    uint64_t gated = 0;
    for (int i = 0; i < bins; ++i)
        if (binDb (i) > relThreshold)
            gated += counts[(size_t) i];
    if (gated == 0)
        return 0.0;

    const auto percentile = [&] (double p)
    {
        const auto target = (uint64_t) std::ceil (p * (double) gated);
        uint64_t acc = 0;
        for (int i = 0; i < bins; ++i)
            if (binDb (i) > relThreshold)
            {
                acc += counts[(size_t) i];
                if (acc >= std::max<uint64_t> (1, target))
                    return binDb (i);
            }
        return binDb (bins - 1);
    };
    return percentile (0.95) - percentile (0.10);
}

// ---- Meter ----------------------------------------------------------------------------------------
void LoudnessMeter::prepare (double sampleRate)
{
    fs = sampleRate;
    subBlockLength = std::max (1, (int) std::lround (fs * 0.1));
    for (int ch = 0; ch < 2; ++ch)
    {
        pre[(size_t) ch].setCoeffs (kWeightingShelf (fs));
        rlb[(size_t) ch].setCoeffs (kWeightingHighpass (fs));
    }
    reset();
}

void LoudnessMeter::reset()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        pre[(size_t) ch].reset();
        rlb[(size_t) ch].reset();
        tp[(size_t) ch].reset();
    }
    subBlockFill = 0;
    subBlockSum = 0.0;
    subBlocks.fill (0.0);
    subIndex = subCount = 0;
    momentaryLufs = shortTermLufs = maxM = maxS = silence;
    blockHistogram.clear();
    stHistogram.clear();
    tpMax = sampleMax = tpSubCurrent = 0.0f;
    tpSub.fill (0.0f);
    sumSquares = 0.0;
    samples = overs = tpOvers = 0;
    history.clear();
    fullHistory.clear();
}

void LoudnessMeter::process (const float* left, const float* right, int numSamples)
{
    const float* in[2] = { left, right };
    for (int i = 0; i < numSamples; ++i)
    {
        double e = 0.0;
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = in[ch][i];
            const double y = rlb[(size_t) ch].process (pre[(size_t) ch].process (x));
            e += y * y; // channel weights for L and R are 1.0

            const float a = std::abs (x);
            sampleMax = std::max (sampleMax, a);
            if (a > 1.0f)
                ++overs;
            sumSquares += (double) x * x;

            const float t = tp[(size_t) ch].process (x);
            tpSubCurrent = std::max (tpSubCurrent, t);
            if (t > tpCeiling)
                ++tpOvers;
        }
        ++samples;
        subBlockSum += e;

        if (++subBlockFill == subBlockLength)
            finishSubBlock();
    }
}

void LoudnessMeter::finishSubBlock()
{
    subIndex = (subIndex + 1) % (int) subBlocks.size();
    subBlocks[(size_t) subIndex] = subBlockSum / subBlockLength;
    tpSub[(size_t) subIndex] = tpSubCurrent;
    tpMax = std::max (tpMax, tpSubCurrent);
    tpSubCurrent = 0.0f;
    subBlockSum = 0.0;
    subBlockFill = 0;
    subCount = std::min (subCount + 1, 1 << 30);

    const auto windowMean = [this] (int blocks)
    {
        double s = 0.0;
        for (int k = 0; k < blocks; ++k)
            s += subBlocks[(size_t) ((subIndex - k + (int) subBlocks.size()) % (int) subBlocks.size())];
        return s / blocks;
    };

    if (subCount >= 4)
    {
        momentaryLufs = energyToLufs (windowMean (4));
        maxM = std::max (maxM, momentaryLufs);
        blockHistogram.add (momentaryLufs); // 400 ms gating blocks with 75 % overlap
    }
    if (subCount >= 30)
    {
        shortTermLufs = energyToLufs (windowMean (30));
        maxS = std::max (maxS, shortTermLufs);
        stHistogram.add (shortTermLufs); // LRA uses short-term values at 10 Hz
    }
    else if (subCount >= 4)
    {
        shortTermLufs = energyToLufs (windowMean (subCount)); // partial window until 3 s have passed
    }

    history.push_back ((float) shortTermLufs);
    if (keepFull)
        fullHistory.push_back ((float) shortTermLufs);
    while (history.size() > 600) // 60 s at 10 Hz
        history.pop_front();
}

double LoudnessMeter::integrated() const { return blockHistogram.gatedMean (-10.0); }

double LoudnessMeter::truePeakDb() const { return dsp::gainToDb (std::max (tpMax, tpSubCurrent)); }

double LoudnessMeter::recentTruePeakDb() const
{
    float m = tpSubCurrent;
    for (auto v : tpSub)
        m = std::max (m, v);
    return dsp::gainToDb (m);
}

double LoudnessMeter::samplePeakDb() const { return dsp::gainToDb (sampleMax); }

double LoudnessMeter::rmsDb() const
{
    return samples == 0 ? silence : 10.0 * std::log10 (std::max (1.0e-24, sumSquares / (2.0 * (double) samples)));
}
} // namespace mm::analysis
