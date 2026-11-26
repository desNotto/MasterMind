#include "analysis/TapAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace mm::analysis
{
namespace
{
std::vector<float> blackmanHarris (int size, double& power)
{
    std::vector<float> w ((size_t) size);
    power = 0.0;
    for (int i = 0; i < size; ++i)
    {
        const double x = 2.0 * dsp::pi * i / size;
        const double v = 0.35875 - 0.48829 * std::cos (x) + 0.14128 * std::cos (2 * x) - 0.01168 * std::cos (3 * x);
        w[(size_t) i] = (float) v;
        power += v * v;
    }
    return w;
}

double safeDb (double e) { return 10.0 * std::log10 (std::max (e, 1.0e-20)); }

TapAnalyzer::StereoBand bandFrom (double ll, double rr, double lr, double mm, double ss)
{
    TapAnalyzer::StereoBand b;
    const double denom = std::sqrt (ll * rr);
    b.correlation = denom > 1.0e-18 ? std::clamp (lr / denom, -1.0, 1.0) : 1.0;
    b.widthPercent = mm > 1.0e-18 ? 100.0 * ss / mm : 0.0;
    b.monoLossDb = (ll + rr) > 1.0e-18 ? std::min (0.0, safeDb (mm / (0.5 * (ll + rr)))) : 0.0;
    return b;
}

double slopeEnergy (double f0, double f1)
{
    // integral of f^-a df with per-bin power falling 4.5 dB/oct: a = 4.5 / (10 log10 2)
    const double a = 4.5 / (10.0 * std::log10 (2.0));
    return (std::pow (f1, 1.0 - a) - std::pow (f0, 1.0 - a)) / (1.0 - a);
}
} // namespace

std::array<double, numTonalBands> defaultTonalTarget()
{
    std::array<double, numTonalBands> t {};
    const double total = slopeEnergy (tonalEdges[0], tonalEdges[numTonalBands]);
    for (int b = 0; b < numTonalBands; ++b)
        t[(size_t) b] = safeDb (slopeEnergy (tonalEdges[b], tonalEdges[b + 1]) / total);
    return t;
}

double defaultHarshnessTarget() { return safeDb (slopeEnergy (3000.0, 8000.0) / slopeEnergy (500.0, 2000.0)); }

TapAnalyzer::TapAnalyzer() = default;
TapAnalyzer::~TapAnalyzer() = default;

void TapAnalyzer::prepare (double sampleRate, int fftOrder)
{
    fs = sampleRate;
    order = std::clamp (fftOrder, 11, 15);
    n = 1 << order;
    hop = n / 4;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window = blackmanHarris (n, windowPower);
    ringL.assign ((size_t) n, 0.0f);
    ringR.assign ((size_t) n, 0.0f);
    work.assign ((size_t) (4 * n), 0.0f);
    for (int c = 0; c < numSpecChannels; ++c)
    {
        avg[(size_t) c].assign ((size_t) (n / 2 + 1), 0.0f);
        peak[(size_t) c].assign ((size_t) (n / 2 + 1), 0.0f);
        frame[(size_t) c].assign ((size_t) (n / 2 + 1), 0.0f);
        sum[(size_t) c].assign ((size_t) (n / 2 + 1), 0.0);
    }

    meter.prepare (fs);

    for (int b = 0; b < numTonalBands; ++b)
    {
        crest[(size_t) b].hp.setSections (dsp::butterworth (4, true, fs, tonalEdges[b]));
        crest[(size_t) b].lp.setSections (dsp::butterworth (4, false, fs, std::min (tonalEdges[b + 1], fs * 0.45)));
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int k = 0; k < numStereoBands - 1; ++k)
            stereoSplit[(size_t) ch][(size_t) k].setup (4, fs, stereoEdges[k]);
        // band 0 gets the allpass of splits 1 and 2, band 1 of split 2 (summed into one cascade each)
        std::vector<dsp::Coeffs> ap0 = dsp::butterworthAllpass (2, fs, stereoEdges[1]);
        const auto ap2 = dsp::butterworthAllpass (2, fs, stereoEdges[2]);
        ap0.insert (ap0.end(), ap2.begin(), ap2.end());
        stereoAp[(size_t) ch][0].setSections (ap0);
        stereoAp[(size_t) ch][1].setSections (ap2);
    }
    shortCoeff = std::exp (-1.0 / (0.3 * fs));
    scopeDecim = std::max (1, (int) std::lround (fs / 12000.0));
    scope.assign (1024, { 0.0f, 0.0f });

    decim = std::max (16, (int) std::lround (fs / 1500.0));
    lowFs = fs / decim;
    lowN = 2048;
    lowFft = std::make_unique<juce::dsp::FFT> (11);
    lowWindow = blackmanHarris (lowN, lowWindowPower);
    lowRingL.assign ((size_t) lowN, 0.0f);
    lowRingR.assign ((size_t) lowN, 0.0f);
    lowWork.assign ((size_t) (4 * lowN), 0.0f);
    for (int c = 0; c < numSpecChannels; ++c)
    {
        lowAvg[(size_t) c].assign ((size_t) (lowN / 2 + 1), 0.0f);
        lowFrame[(size_t) c].assign ((size_t) (lowN / 2 + 1), 0.0f);
        lowSum[(size_t) c].assign ((size_t) (lowN / 2 + 1), 0.0);
    }
    for (auto& a : antiAlias)
        a.setSections (dsp::butterworth (8, false, fs, std::min (0.3 * lowFs, 450.0)));
    const dsp::Coeffs wb[] = { dsp::Coeffs::highpass (lowFs, 20.0, 0.71), dsp::Coeffs::lowpass (lowFs, 150.0, 0.71) };
    waveBand.setSections (wb, 2);
    wave.assign ((size_t) std::lround (2.0 * lowFs), 0.0f);
    monoForFilter = 0.0;

    sgN = 1 << sgOrder;
    sgHop = std::max (256, (int) std::lround (fs / 25.0));
    sgFft = std::make_unique<juce::dsp::FFT> (sgOrder);
    double unused = 0.0;
    sgWindow = blackmanHarris (sgN, unused);
    sgRing.assign ((size_t) sgN, 0.0f);
    sgWork.assign ((size_t) (2 * sgN), 0.0f);
    for (int b = 0; b < spectrogramBins; ++b)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) b / spectrogramBins);
        const double f1 = 20.0 * std::pow (1000.0, (double) (b + 1) / spectrogramBins);
        const int k0 = std::clamp ((int) std::floor (f0 * sgN / fs), 1, sgN / 2);
        const int k1 = std::clamp ((int) std::ceil (f1 * sgN / fs), k0 + 1, sgN / 2 + 1);
        sgBinRanges[(size_t) b] = { k0, k1 };
    }

    reset();
}

void TapAnalyzer::reset()
{
    meter.reset();
    std::fill (ringL.begin(), ringL.end(), 0.0f);
    std::fill (ringR.begin(), ringR.end(), 0.0f);
    ringPos = fill = 0;
    for (int c = 0; c < numSpecChannels; ++c)
    {
        std::fill (avg[(size_t) c].begin(), avg[(size_t) c].end(), 0.0f);
        std::fill (peak[(size_t) c].begin(), peak[(size_t) c].end(), 0.0f);
        std::fill (frame[(size_t) c].begin(), frame[(size_t) c].end(), 0.0f);
        std::fill (sum[(size_t) c].begin(), sum[(size_t) c].end(), 0.0);
        std::fill (lowAvg[(size_t) c].begin(), lowAvg[(size_t) c].end(), 0.0f);
        std::fill (lowFrame[(size_t) c].begin(), lowFrame[(size_t) c].end(), 0.0f);
        std::fill (lowSum[(size_t) c].begin(), lowSum[(size_t) c].end(), 0.0);
    }
    frames = lowFrames = 0;
    tonalSum.fill (0.0);
    tonalTotal = harshHiSum = harshLoSum = 0.0;
    recent.clear();
    currentSecond = {};
    samplesThisSecond = 0;
    harshHistory.clear();
    for (auto& c : crest)
    {
        c.hp.reset();
        c.lp.reset();
        c.peak = c.sumSq = 0.0;
        c.count = 0;
    }
    for (auto& ch : stereoSplit)
        for (auto& s : ch)
            s.reset();
    for (auto& ch : stereoAp)
        for (auto& a : ch)
            a.reset();
    for (auto& a : stereoAcc)
        a = {};
    broad = {};
    lowAcc = {};
    sumL2 = sumR2 = 0.0;
    std::fill (scope.begin(), scope.end(), std::pair<float, float> { 0.0f, 0.0f });
    scopePos = scopeCounter = 0;
    decimCounter = lowFill = lowRingPos = 0;
    for (auto& a : antiAlias)
        a.reset();
    std::fill (lowRingL.begin(), lowRingL.end(), 0.0f);
    std::fill (lowRingR.begin(), lowRingR.end(), 0.0f);
    waveBand.reset();
    std::fill (wave.begin(), wave.end(), 0.0f);
    wavePos = 0;
    for (auto& m : monoLp)
        m.reset();
    sgFill = sgRingPos = 0;
    std::fill (sgRing.begin(), sgRing.end(), 0.0f);
    specgram.clear();
}

void TapAnalyzer::process (const float* left, const float* right, int numSamples)
{
    meter.process (left, right, numSamples);

    if (monoForFilter != monoFreq)
    {
        for (auto& m : monoLp)
            m.setSections (dsp::butterworth (4, false, lowFs, std::min (monoFreq, lowFs * 0.4)));
        monoForFilter = monoFreq;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];
        ringL[(size_t) ringPos] = l;
        ringR[(size_t) ringPos] = r;
        if (++ringPos == n)
            ringPos = 0;
        if (++fill >= hop)
        {
            fill = 0;
            analyseFrame();
        }

        const double m = 0.5 * (l + r), s = 0.5 * (l - r);
        sumL2 += (double) l * l;
        sumR2 += (double) r * r;

        // crest per tonal band
        for (auto& c : crest)
        {
            const double y = c.lp.process (c.hp.process (m));
            c.peak = std::max (c.peak, std::abs (y));
            c.sumSq += y * y;
            ++c.count;
        }

        // stereo bands
        double bl[numStereoBands], br[numStereoBands];
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& sp = stereoSplit[(size_t) ch];
            auto& ap = stereoAp[(size_t) ch];
            double* out = ch == 0 ? bl : br;
            double lo, rest, lo2, rest2, lo3, hi3;
            sp[0].process (ch == 0 ? l : r, lo, rest);
            sp[1].process (rest, lo2, rest2);
            sp[2].process (rest2, lo3, hi3);
            out[0] = ap[0].process (lo);
            out[1] = ap[1].process (lo2);
            out[2] = lo3;
            out[3] = hi3;
        }
        const auto accumulate = [this] (StereoAcc& a, double x, double y)
        {
            const double mm = 0.25 * (x + y) * (x + y), ss = 0.25 * (x - y) * (x - y);
            a.ll += x * x;
            a.rr += y * y;
            a.lr += x * y;
            a.mm += mm;
            a.ss += ss;
            const double k = shortCoeff;
            a.sll = x * x + k * (a.sll - x * x);
            a.srr = y * y + k * (a.srr - y * y);
            a.slr = x * y + k * (a.slr - x * y);
            a.smm = mm + k * (a.smm - mm);
            a.sss = ss + k * (a.sss - ss);
        };
        for (int b = 0; b < numStereoBands; ++b)
            accumulate (stereoAcc[(size_t) b], bl[b], br[b]);
        accumulate (broad, l, r);

        if (! lightweight && ++scopeCounter >= scopeDecim)
        {
            scopeCounter = 0;
            scope[(size_t) scopePos] = { (float) m, (float) s };
            scopePos = (scopePos + 1) % (int) scope.size();
        }

        // low end (decimated)
        const double al = antiAlias[0].process (l), ar = antiAlias[1].process (r);
        if (++decimCounter >= decim)
        {
            decimCounter = 0;
            lowRingL[(size_t) lowRingPos] = (float) al;
            lowRingR[(size_t) lowRingPos] = (float) ar;
            lowRingPos = (lowRingPos + 1) % lowN;
            if (++lowFill >= lowN / 4)
            {
                lowFill = 0;
                analyseLowFrame();
            }
            wave[(size_t) wavePos] = (float) waveBand.process (0.5 * (al + ar));
            wavePos = (wavePos + 1) % (int) wave.size();
            const double ml = monoLp[0].process (al), mr = monoLp[1].process (ar);
            lowAcc.ll += ml * ml;
            lowAcc.rr += mr * mr;
            lowAcc.lr += ml * mr;
            lowAcc.mm += 0.25 * (ml + mr) * (ml + mr);
            lowAcc.ss += 0.25 * (ml - mr) * (ml - mr);
        }

        // spectrogram (mid)
        if (! lightweight)
        {
            sgRing[(size_t) sgRingPos] = (float) m;
            sgRingPos = (sgRingPos + 1) % sgN;
            if (++sgFill >= sgHop)
            {
                sgFill = 0;
                analyseSpectrogramFrame();
            }
        }

        // one-second buckets for "last 10 s" and the harshness history
        if (++samplesThisSecond >= (int) fs)
        {
            samplesThisSecond = 0;
            if (currentSecond.frames > 0)
            {
                recent.push_back (currentSecond);
                while (recent.size() > 10)
                    recent.pop_front();
                harshHistory.push_back ((float) safeDb (currentSecond.harshHi / std::max (1.0e-20, currentSecond.harshLo)));
                while (harshHistory.size() > 30)
                    harshHistory.pop_front();
            }
            currentSecond = {};
        }
    }
}

void TapAnalyzer::analyseFrame()
{
    float* xl = work.data();
    float* xr = work.data() + 2 * n;
    for (int i = 0; i < n; ++i)
    {
        const int idx = (ringPos + i) % n;
        xl[i] = ringL[(size_t) idx] * window[(size_t) i];
        xr[i] = ringR[(size_t) idx] * window[(size_t) i];
    }
    std::fill (xl + n, xl + 2 * n, 0.0f);
    std::fill (xr + n, xr + 2 * n, 0.0f);
    fft->performRealOnlyForwardTransform (xl, true);
    fft->performRealOnlyForwardTransform (xr, true);

    ++frames;
    const double norm = 1.0 / ((double) n * windowPower);
    std::array<double, numTonalBands> bands {};
    double total = 0.0, hi = 0.0, lo = 0.0;
    for (int k = 0; k <= n / 2; ++k)
    {
        const double c = (k == 0 || k == n / 2) ? norm : 2.0 * norm;
        const double lr = xl[2 * k], li = xl[2 * k + 1], rr = xr[2 * k], ri = xr[2 * k + 1];
        const double pl = c * (lr * lr + li * li), pr = c * (rr * rr + ri * ri);
        const double mr = 0.5 * (lr + rr), mi = 0.5 * (li + ri), sr = 0.5 * (lr - rr), si = 0.5 * (li - ri);
        const double p[numSpecChannels] = { pl, pr, c * (mr * mr + mi * mi), c * (sr * sr + si * si), 0.5 * (pl + pr) };
        for (int ch = 0; ch < numSpecChannels; ++ch)
        {
            const auto pc = (float) p[ch];
            frame[(size_t) ch][(size_t) k] = pc;
            sum[(size_t) ch][(size_t) k] += p[ch];
            avg[(size_t) ch][(size_t) k] = (float) (sum[(size_t) ch][(size_t) k] / (double) frames);
            peak[(size_t) ch][(size_t) k] = std::max (peak[(size_t) ch][(size_t) k], pc);
        }

        const double f = k * fs / n;
        const double ps = p[4];
        if (f >= tonalEdges[0] && f < tonalEdges[numTonalBands])
        {
            total += ps;
            for (int b = 0; b < numTonalBands; ++b)
                if (f < tonalEdges[b + 1])
                {
                    bands[(size_t) b] += ps;
                    break;
                }
        }
        if (f >= 3000.0 && f < 8000.0)
            hi += ps;
        else if (f >= 500.0 && f < 2000.0)
            lo += ps;
    }
    for (int b = 0; b < numTonalBands; ++b)
    {
        tonalSum[(size_t) b] += bands[(size_t) b];
        currentSecond.bands[(size_t) b] += bands[(size_t) b];
    }
    tonalTotal += total;
    currentSecond.total += total;
    harshHiSum += hi;
    harshLoSum += lo;
    currentSecond.harshHi += hi;
    currentSecond.harshLo += lo;
    ++currentSecond.frames;
}

void TapAnalyzer::analyseLowFrame()
{
    float* xl = lowWork.data();
    float* xr = lowWork.data() + 2 * lowN;
    for (int i = 0; i < lowN; ++i)
    {
        const int idx = (lowRingPos + i) % lowN;
        xl[i] = lowRingL[(size_t) idx] * lowWindow[(size_t) i];
        xr[i] = lowRingR[(size_t) idx] * lowWindow[(size_t) i];
    }
    std::fill (xl + lowN, xl + 2 * lowN, 0.0f);
    std::fill (xr + lowN, xr + 2 * lowN, 0.0f);
    lowFft->performRealOnlyForwardTransform (xl, true);
    lowFft->performRealOnlyForwardTransform (xr, true);
    ++lowFrames;
    const double norm = 1.0 / ((double) lowN * lowWindowPower);
    for (int k = 0; k <= lowN / 2; ++k)
    {
        const double c = (k == 0 || k == lowN / 2) ? norm : 2.0 * norm;
        const double lr = xl[2 * k], li = xl[2 * k + 1], rr = xr[2 * k], ri = xr[2 * k + 1];
        const double pl = c * (lr * lr + li * li), pr = c * (rr * rr + ri * ri);
        const double mr = 0.5 * (lr + rr), mi = 0.5 * (li + ri), sr = 0.5 * (lr - rr), si = 0.5 * (li - ri);
        const double p[numSpecChannels] = { pl, pr, c * (mr * mr + mi * mi), c * (sr * sr + si * si), 0.5 * (pl + pr) };
        for (int ch = 0; ch < numSpecChannels; ++ch)
        {
            lowFrame[(size_t) ch][(size_t) k] = (float) p[ch];
            lowSum[(size_t) ch][(size_t) k] += p[ch];
            lowAvg[(size_t) ch][(size_t) k] = (float) (lowSum[(size_t) ch][(size_t) k] / (double) lowFrames);
        }
    }
}

void TapAnalyzer::analyseSpectrogramFrame()
{
    for (int i = 0; i < sgN; ++i)
        sgWork[(size_t) i] = sgRing[(size_t) ((sgRingPos + i) % sgN)] * sgWindow[(size_t) i];
    std::fill (sgWork.begin() + sgN, sgWork.end(), 0.0f);
    sgFft->performRealOnlyForwardTransform (sgWork.data(), true);
    std::array<float, spectrogramBins> col {};
    const double norm = 4.0 / ((double) sgN * sgN * 0.36 * 0.36);
    for (int b = 0; b < spectrogramBins; ++b)
    {
        double mx = 0.0;
        for (int k = sgBinRanges[(size_t) b].first; k < sgBinRanges[(size_t) b].second; ++k)
        {
            const double re = sgWork[(size_t) (2 * k)], im = sgWork[(size_t) (2 * k + 1)];
            mx = std::max (mx, re * re + im * im);
        }
        col[(size_t) b] = (float) safeDb (mx * norm);
    }
    specgram.push_back (col);
    ++sgTotal;
    const auto maxCols = (size_t) std::lround (sgSeconds * fs / sgHop);
    while (specgram.size() > maxCols)
        specgram.pop_front();
}

std::array<double, numTonalBands> TapAnalyzer::tonalIntegrated() const
{
    std::array<double, numTonalBands> out {};
    for (int b = 0; b < numTonalBands; ++b)
        out[(size_t) b] = tonalTotal > 0.0 ? safeDb (tonalSum[(size_t) b] / tonalTotal) : -100.0;
    return out;
}

std::array<double, numTonalBands> TapAnalyzer::tonalRecent() const
{
    std::array<double, numTonalBands> bands {};
    double total = 0.0;
    for (const auto& s : recent)
    {
        for (int b = 0; b < numTonalBands; ++b)
            bands[(size_t) b] += s.bands[(size_t) b];
        total += s.total;
    }
    for (int b = 0; b < numTonalBands; ++b)
        bands[(size_t) b] += currentSecond.bands[(size_t) b];
    total += currentSecond.total;
    std::array<double, numTonalBands> out {};
    for (int b = 0; b < numTonalBands; ++b)
        out[(size_t) b] = total > 0.0 ? safeDb (bands[(size_t) b] / total) : -100.0;
    return out;
}

double TapAnalyzer::harshnessDb() const
{
    return harshLoSum > 0.0 ? safeDb (harshHiSum / harshLoSum) : -100.0;
}

std::array<double, numTonalBands> TapAnalyzer::crestDb() const
{
    std::array<double, numTonalBands> out {};
    for (int b = 0; b < numTonalBands; ++b)
    {
        const auto& c = crest[(size_t) b];
        out[(size_t) b] = c.count > 0 && c.sumSq > 1.0e-20 ? dsp::gainToDb (c.peak) - safeDb (c.sumSq / (double) c.count) : 0.0;
    }
    return out;
}

TapAnalyzer::StereoBand TapAnalyzer::stereoBroadband (bool shortTerm) const
{
    return shortTerm ? bandFrom (broad.sll, broad.srr, broad.slr, broad.smm, broad.sss)
                     : bandFrom (broad.ll, broad.rr, broad.lr, broad.mm, broad.ss);
}

TapAnalyzer::StereoBand TapAnalyzer::stereoBand (int band, bool shortTerm) const
{
    const auto& a = stereoAcc[(size_t) std::clamp (band, 0, numStereoBands - 1)];
    return shortTerm ? bandFrom (a.sll, a.srr, a.slr, a.smm, a.sss) : bandFrom (a.ll, a.rr, a.lr, a.mm, a.ss);
}

double TapAnalyzer::balanceDb() const
{
    return (sumL2 > 1.0e-18 && sumR2 > 1.0e-18) ? 10.0 * std::log10 (sumR2 / sumL2) : 0.0;
}

void TapAnalyzer::copyScope (std::vector<std::pair<float, float>>& out) const
{
    out.resize (scope.size());
    for (size_t i = 0; i < scope.size(); ++i)
        out[i] = scope[(i + (size_t) scopePos) % scope.size()];
}

void TapAnalyzer::copyLowWave (std::vector<float>& out) const
{
    out.resize (wave.size());
    for (size_t i = 0; i < wave.size(); ++i)
        out[i] = wave[(i + (size_t) wavePos) % wave.size()];
}

double TapAnalyzer::peakAsymmetryPercent() const
{
    float pos = 0.0f, neg = 0.0f;
    for (auto v : wave)
    {
        pos = std::max (pos, v);
        neg = std::max (neg, -v);
    }
    const float mx = std::max (pos, neg);
    return mx > 1.0e-6f ? 100.0 * (pos - neg) / mx : 0.0;
}

TapAnalyzer::StereoBand TapAnalyzer::lowBelowMono() const
{
    return bandFrom (lowAcc.ll, lowAcc.rr, lowAcc.lr, lowAcc.mm, lowAcc.ss);
}

double TapAnalyzer::subToBassDb() const
{
    const auto& p = lowAvg[(size_t) SpecChannel::stereo];
    double sub = 0.0, bass = 0.0;
    for (int k = 1; k <= lowN / 2; ++k)
    {
        const double f = k * lowFs / lowN;
        if (f >= 20.0 && f < 60.0)
            sub += p[(size_t) k];
        else if (f >= 60.0 && f < 120.0)
            bass += p[(size_t) k];
    }
    return bass > 0.0 ? safeDb (sub / bass) : 0.0;
}

void powerToDisplay (const std::vector<float>& power, double fs, int fftSize, const std::vector<double>& freqs,
                     int smoothing, double tiltDbPerOct, std::vector<float>& outDb)
{
    static constexpr double octaves[] = { 0.0, 1.0 / 12.0, 1.0 / 6.0, 1.0 / 3.0, 1.0 };
    const double width = octaves[std::clamp (smoothing, 0, 4)];
    const int bins = (int) power.size();
    outDb.resize (freqs.size());

    // prefix sums for fast band averages
    static thread_local std::vector<double> prefix;
    prefix.resize ((size_t) bins + 1);
    prefix[0] = 0.0;
    for (int k = 0; k < bins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + power[(size_t) k];

    // Smoothed modes show band power (centred, unbiased peaks) converted to a density slope:
    // band power of pink noise is flat, density falls 3 dB/oct. Adding (tilt - 3) dB/oct to band power
    // equals adding the tilt to the density, so the spec's tilt values keep their meaning
    // (3 dB/oct: pink noise flat, 4.5 dB/oct: a typical mix flat). Levels pivot at 1 kHz.
    const double binHz = fs / fftSize;
    const double densityOffset = 10.0 * std::log10 (std::max (1.0, 1000.0 * (std::pow (2.0, 1.0 / 6.0) - std::pow (2.0, -1.0 / 6.0)) / binHz));
    for (size_t i = 0; i < freqs.size(); ++i)
    {
        const double f = freqs[i];
        const double oct = std::log2 (f / 1000.0);
        if (width <= 0.0)
        {
            const double pos = f / binHz;
            const int k = std::clamp ((int) pos, 0, bins - 2);
            const double t = pos - k;
            const double e = power[(size_t) k] * (1.0 - t) + power[(size_t) k + 1] * t;
            outDb[i] = (float) (safeDb (e) + densityOffset + tiltDbPerOct * oct);
        }
        else
        {
            const double f0 = f * std::pow (2.0, -width * 0.5), f1 = f * std::pow (2.0, width * 0.5);
            const int k0 = std::clamp ((int) std::floor (f0 / binHz + 0.5), 0, bins - 1);
            const int k1 = std::clamp ((int) std::floor (f1 / binHz + 0.5), k0 + 1, bins);
            const double e = (prefix[(size_t) k1] - prefix[(size_t) k0]) * (1.0 / 3.0) / width; // per 1/3 octave
            outDb[i] = (float) (safeDb (e) + (tiltDbPerOct - 3.0103) * oct);
        }
    }
}
} // namespace mm::analysis
