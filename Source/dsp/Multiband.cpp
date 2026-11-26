#include "dsp/Multiband.h"

namespace mm::dsp
{
namespace
{
int firLengthFor (double fs) { return fs <= 50000.0 ? 4096 : (fs <= 100000.0 ? 8192 : 16384); }
} // namespace

std::array<double, 4> Multiband::sanitise (const std::array<float, 4>& xo, double fs)
{
    std::array<double, 4> out {};
    double prev = 20.0;
    for (int i = 0; i < 4; ++i)
    {
        out[(size_t) i] = std::clamp ((double) xo[(size_t) i], prev * 1.25, fs * 0.45);
        prev = out[(size_t) i];
    }
    return out;
}

void Multiband::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    maxBlock = ctx.maxBlock;
    firLength = firLengthFor (fs);
    bandBuf.setSize (2 * numBands, maxBlock, false, true, false);
    scratch.setSize (3, maxBlock, false, true, false);
    const juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 1 };
    for (auto& c : conv)
        c.prepare (spec);
    linDelay.prepare (2, firLength / 2 + 1);
    linDelay.setDelay (firLength / 2);
    for (auto& d : dyn)
        for (auto& s : d.smooth)
            s.prepare (fs);
    reset();
}

void Multiband::reset()
{
    const auto xo = sanitise (settings.crossovers, fs);
    for (int i = 0; i < 4; ++i)
        xoRamp[(size_t) i].reset (xo[(size_t) i]);
    updateCrossovers (xo);
    for (auto& c : chans)
    {
        for (auto& x : c.xo)
            x.reset();
        for (auto& a : c.ap)
            a.reset();
    }
    for (int b = 0; b < numBands; ++b)
    {
        auto& d = dyn[(size_t) b];
        for (auto& s : d.smooth)
            s.reset();
        d.makeup.reset (dbToGain (settings.bands[(size_t) b].makeupDb));
        d.trim.reset (dbToGain (settings.bands[(size_t) b].trimDb));
        d.mix.reset (settings.bands[(size_t) b].mix);
        grMeters[(size_t) b].set (0.0f);
    }
    for (auto& c : conv)
        c.reset();
    linDelay.reset();
}

void Multiband::updateCrossovers (const std::array<double, 4>& xo) noexcept
{
    for (auto& c : chans)
    {
        for (int i = 0; i < 4; ++i)
            c.xo[(size_t) i].setup (4, fs, xo[(size_t) i]);
        // allpass compensation: b1 gets xo2..xo4, b2 gets xo3..xo4, b3 gets xo4
        c.xo[1].makeAllpass (c.ap[0]);
        c.xo[2].makeAllpass (c.ap[1]);
        c.xo[3].makeAllpass (c.ap[2]);
        c.xo[2].makeAllpass (c.ap[3]);
        c.xo[3].makeAllpass (c.ap[4]);
        c.xo[3].makeAllpass (c.ap[5]);
    }
}

void Multiband::splitMinimumPhase (int ch, float* x, int len) noexcept
{
    auto& c = chans[(size_t) ch];
    float* b[numBands];
    for (int k = 0; k < numBands; ++k)
        b[k] = bandBuf.getWritePointer (ch * numBands + k);

    for (int i = 0; i < len; ++i)
    {
        double lo, hi, lo2, hi2, lo3, hi3, lo4, hi4;
        c.xo[0].process (x[i], lo, hi);
        c.xo[1].process (hi, lo2, hi2);
        c.xo[2].process (hi2, lo3, hi3);
        c.xo[3].process (hi3, lo4, hi4);
        b[0][i] = (float) c.ap[2].process (c.ap[1].process (c.ap[0].process (lo)));
        b[1][i] = (float) c.ap[4].process (c.ap[3].process (lo2));
        b[2][i] = (float) c.ap[5].process (lo3);
        b[3][i] = (float) lo4;
        b[4][i] = (float) hi4;
    }
}

void Multiband::splitLinearPhase (int ch, float* x, int len) noexcept
{
    float* b[numBands];
    for (int k = 0; k < numBands; ++k)
        b[k] = bandBuf.getWritePointer (ch * numBands + k);

    float* src[1] = { x };
    juce::dsp::AudioBlock<float> in (src, 1, (size_t) len);
    for (int k = 0; k < 4; ++k)
    {
        float* dst[1] = { b[k] };
        juce::dsp::AudioBlock<float> out (dst, 1, (size_t) len);
        conv[(size_t) (ch * 4 + k)].process (juce::dsp::ProcessContextNonReplacing<float> (in, out));
    }
    // b[k] holds low-pass k; convert to bands: b5 = delayed x - lp4, bk = lpk - lp(k-1)
    for (int i = 0; i < len; ++i)
    {
        const float lp1 = b[0][i], lp2 = b[1][i], lp3 = b[2][i], lp4 = b[3][i];
        b[4][i] = x[i] - lp4; // x is already delayed by the caller
        b[3][i] = lp4 - lp3;
        b[2][i] = lp3 - lp2;
        b[1][i] = lp2 - lp1;
        b[0][i] = lp1;
    }
}

void Multiband::process (float* left, float* right, int numSamples) noexcept
{
    const auto target = sanitise (settings.crossovers, fs);
    for (int i = 0; i < 4; ++i)
        xoRamp[(size_t) i].setTarget (target[(size_t) i], (int) (0.05 * fs));
    for (int b = 0; b < numBands; ++b)
    {
        const auto& s = settings.bands[(size_t) b];
        auto& d = dyn[(size_t) b];
        for (auto& sm : d.smooth)
            sm.setTimes (s.attackMs, s.releaseMs, s.releaseAuto);
        d.makeup.setTarget (dbToGain (s.makeupDb), 1200);
        d.trim.setTarget (dbToGain (s.trimDb), 1200);
        d.mix.setTarget (s.mix, 1200);
    }

    for (int start = 0; start < numSamples; start += controlBlock)
    {
        const int len = std::min (controlBlock, numSamples - start);
        float* l = left + start;
        float* r = right + start;

        if (settings.linearPhase)
        {
            float* lr[2] = { l, r };
            // the subtraction for the top band needs the input delayed like the FIR outputs
            float* inL = scratch.getWritePointer (0);
            float* inR = scratch.getWritePointer (1);
            std::copy (l, l + len, inL);
            std::copy (r, r + len, inR);
            splitLinearPhase (0, inL, len); // uses undelayed copy for convolution input
            splitLinearPhase (1, inR, len);
            // fix the top band: needs delayed input instead of the undelayed copy
            linDelay.process (lr, 2, len);
            for (int ch = 0; ch < 2; ++ch)
            {
                float* top = bandBuf.getWritePointer (ch * numBands + 4);
                const float* inp = ch == 0 ? inL : inR;
                const float* del = ch == 0 ? l : r;
                for (int i = 0; i < len; ++i)
                    top[i] += del[i] - inp[i];
            }
        }
        else
        {
            bool ramping = false;
            std::array<double, 4> xo {};
            for (int i = 0; i < 4; ++i)
            {
                ramping = ramping || xoRamp[(size_t) i].isRamping();
                xo[(size_t) i] = xoRamp[(size_t) i].next();
            }
            if (ramping)
                updateCrossovers (xo);
            splitMinimumPhase (0, l, len);
            splitMinimumPhase (1, r, len);
        }

        compressBands (len);

        bool anySolo = false;
        for (const auto& b : settings.bands)
            anySolo = anySolo || b.solo;
        for (int i = 0; i < len; ++i)
        {
            double sl = 0.0, sr = 0.0;
            for (int b = 0; b < numBands; ++b)
            {
                if (anySolo && ! settings.bands[(size_t) b].solo)
                    continue;
                sl += bandBuf.getSample (b, i);
                sr += bandBuf.getSample (numBands + b, i);
            }
            l[i] = (float) sl;
            r[i] = (float) sr;
        }
    }
}

void Multiband::compressBands (int len) noexcept
{
    for (int b = 0; b < numBands; ++b)
    {
        const auto& s = settings.bands[(size_t) b];
        auto& d = dyn[(size_t) b];
        float* x0 = bandBuf.getWritePointer (b);
        float* x1 = bandBuf.getWritePointer (numBands + b);
        if (s.bypass)
        {
            grMeters[(size_t) b].set (0.0f);
            continue;
        }

        const bool ms = s.mode == 1;
        float maxGr = 0.0f;
        for (int i = 0; i < len; ++i)
        {
            double a = x0[i], c = x1[i];
            if (ms)
            {
                const double m = 0.5 * (a + c), sd = 0.5 * (a - c);
                a = m;
                c = sd;
            }
            const double la = gainToDb (std::abs (a) + 1.0e-9), lc = gainToDb (std::abs (c) + 1.0e-9);
            const double linked = std::max (la, lc);
            const double link = ms ? 0.0 : s.link;
            const double ga = d.smooth[0].process (compressorGainDb (link * linked + (1.0 - link) * la, s.thresholdDb, s.ratio, s.kneeDb));
            const double gc = d.smooth[1].process (compressorGainDb (link * linked + (1.0 - link) * lc, s.thresholdDb, s.ratio, s.kneeDb));
            maxGr = std::min (maxGr, (float) std::min (ga, gc));

            const double mk = d.makeup.next(), mixv = d.mix.next(), tr = d.trim.next();
            double wa = a * dbToGain (ga) * mk, wc = c * dbToGain (gc) * mk;
            wa = a + mixv * (wa - a);
            wc = c + mixv * (wc - c);
            if (ms)
            {
                const double l = wa + wc, r = wa - wc;
                wa = l;
                wc = r;
            }
            x0[i] = (float) (wa * tr);
            x1[i] = (float) (wc * tr);
        }
        grMeters[(size_t) b].set (maxGr);
    }
}

void Multiband::buildLinearPhase (const Settings& snap)
{
    const int n = firLength;
    juce::dsp::FFT fft (juce::roundToInt (std::log2 ((double) n)));
    std::vector<float> probe ((size_t) (2 * n), 0.0f);
    for (int k = 0; k <= n / 2; ++k)
        probe[(size_t) (2 * k)] = 1.0f;
    fft.performRealOnlyInverseTransform (probe.data());
    const double scale = 1.0 / std::max (1.0e-9, (double) probe[0]);

    const auto xo = sanitise (snap.crossovers, fs);
    for (int k = 0; k < 4; ++k)
    {
        LinkwitzRiley lr;
        lr.setup (4, fs, xo[(size_t) k]);
        std::vector<float> spec ((size_t) (2 * n), 0.0f);
        for (int bin = 0; bin <= n / 2; ++bin)
        {
            const double f = std::max (1.0e-3, bin * fs / n);
            const double h = std::abs (lr.lowResponse (fs, f));
            spec[(size_t) (2 * bin)] = (float) (h * ((bin & 1) ? -1.0 : 1.0));
        }
        fft.performRealOnlyInverseTransform (spec.data());
        for (int ch = 0; ch < 2; ++ch)
        {
            juce::AudioBuffer<float> ir (1, n);
            auto* d = ir.getWritePointer (0);
            for (int i = 0; i < n; ++i)
            {
                const double w = 0.42 - 0.5 * std::cos (2.0 * pi * i / (n - 1)) + 0.08 * std::cos (4.0 * pi * i / (n - 1));
                d[i] = (float) (spec[(size_t) i] * scale * w);
            }
            conv[(size_t) (ch * 4 + k)].loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::no,
                                                             juce::dsp::Convolution::Trim::no,
                                                             juce::dsp::Convolution::Normalise::no);
        }
    }
}
} // namespace mm::dsp
