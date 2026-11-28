#include "dsp/SpectralBank.h"

namespace mm::dsp
{
namespace
{
constexpr double centres26[26] = { 30, 60, 90, 125, 165, 215, 280, 360, 450, 560, 700, 860, 1050,
                                   1300, 1600, 1950, 2400, 2950, 3600, 4400, 5400, 6600, 8100, 10000, 12500, 16000 };

/** Lower band index and its weight at frequency f: log-frequency triangles that sum to one. */
void triangleAt (const std::vector<double>& fc, double f, int& low, float& w)
{
    const int n = (int) fc.size();
    if (f <= fc[0])
    {
        low = 0;
        w = 1.0f;
        return;
    }
    if (f >= fc[(size_t) n - 1])
    {
        low = n - 1;
        w = 1.0f;
        return;
    }
    int j = 0;
    while (j < n - 2 && f >= fc[(size_t) j + 1])
        ++j;
    const double t = (std::log (f) - std::log (fc[(size_t) j])) / (std::log (fc[(size_t) j + 1]) - std::log (fc[(size_t) j]));
    low = j;
    w = (float) (1.0 - t);
}

double coeffFor (double frameSec, double seconds) { return seconds <= 0.0 ? 0.0 : std::exp (-frameSec / seconds); }
} // namespace

std::vector<double> SpectralBank::centres (int bandSet)
{
    const int n = bandCount (bandSet);
    std::vector<double> fc;
    for (int i = 0; i < n; ++i)
        fc.push_back (centres26[(size_t) juce::roundToInt (i * 25.0 / (n - 1))]);
    return fc;
}

void SpectralBank::prepare (double sampleRate)
{
    fs = sampleRate;
    const int scale = fs <= 50000.0 ? 1 : (fs <= 100000.0 ? 2 : 4);
    blockSize = 256 * scale;
    fftSize = 2 * blockSize;
    filterLength = 4096 * scale;
    partitions = filterLength / blockSize;
    bins = fftSize / 2 + 1;
    fftBlock = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (fftSize)));
    fftLong = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (filterLength)));
    fftAna = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (filterLength / 2)));

    work.assign ((size_t) 2 * filterLength, 0.0f);
    work2.assign ((size_t) 2 * filterLength, 0.0f);
    analysisBuf.assign ((size_t) 2 * filterLength, 0.0f);
    windowLong.resize ((size_t) filterLength);
    for (int i = 0; i < filterLength; ++i)
        windowLong[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / filterLength));
    windowShort.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        windowShort[(size_t) i] = (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * i / fftSize));

    for (int s = 0; s < 3; ++s)
        designSet (sets[(size_t) s], s);

    const size_t spec = (size_t) (fftSize + 2);
    for (int ch = 0; ch < 2; ++ch)
    {
        history[(size_t) ch].assign ((size_t) filterLength, 0.0f);
        fdl[(size_t) ch].assign ((size_t) partitions * spec, 0.0f);
        inFrame[(size_t) ch].assign ((size_t) fftSize, 0.0f);
        outShaped[(size_t) ch].assign ((size_t) blockSize, 0.0f);
        outFull[(size_t) ch].assign ((size_t) blockSize, 0.0f);
        for (auto& kind : hg[(size_t) ch])
            for (auto& v : kind)
                v.assign ((size_t) partitions * spec, 0.0f);
    }
    reset();
}

void SpectralBank::designSet (BandSetData& set, int bandSet)
{
    set.fc = centres (bandSet);
    for (auto& f : set.fc)
        f = std::min (f, 0.45 * fs);
    set.bands = (int) set.fc.size();
    const int L = filterLength, K = partitions, B = blockSize;
    const size_t spec = (size_t) (fftSize + 2);
    set.spectra.assign ((size_t) set.bands * (size_t) K * spec, 0.0f);

    std::vector<int> low ((size_t) L / 2 + 1);
    std::vector<float> w ((size_t) L / 2 + 1);
    for (int k = 0; k <= L / 2; ++k)
        triangleAt (set.fc, k * fs / L, low[(size_t) k], w[(size_t) k]);

    for (int b = 0; b < set.bands; ++b)
    {
        // linear-phase band: triangle magnitude, delay L/2 ((-1)^k), Hann window (w[L/2] = 1)
        std::fill (work.begin(), work.end(), 0.0f);
        for (int k = 0; k <= L / 2; ++k)
        {
            float m = 0.0f;
            if (low[(size_t) k] == b)
                m = w[(size_t) k];
            else if (low[(size_t) k] + 1 == b && w[(size_t) k] < 1.0f)
                m = 1.0f - w[(size_t) k];
            work[(size_t) 2 * k] = (k % 2 == 0) ? m : -m;
        }
        fftLong->performRealOnlyInverseTransform (work.data());
        for (int i = 0; i < L; ++i)
            work[(size_t) i] *= windowLong[(size_t) i];
        for (int p = 0; p < K; ++p)
        {
            std::fill (work2.begin(), work2.begin() + 2 * fftSize, 0.0f);
            std::copy (work.begin() + p * B, work.begin() + (p + 1) * B, work2.begin());
            fftBlock->performRealOnlyForwardTransform (work2.data(), true);
            std::copy (work2.begin(), work2.begin() + (long) spec, set.spectra.begin() + (long) (((size_t) b * (size_t) K + (size_t) p) * spec));
        }
    }

    // analysis weights: the long analysis uses half the filter length, the short one the frame FFT
    const int La = L / 2;
    set.lowBandLong.resize ((size_t) La / 2 + 1);
    set.wLong.resize ((size_t) La / 2 + 1);
    for (int k = 0; k <= La / 2; ++k)
        triangleAt (set.fc, k * fs / La, set.lowBandLong[(size_t) k], set.wLong[(size_t) k]);
    set.lowBandShort.resize ((size_t) bins);
    set.wShort.resize ((size_t) bins);
    for (int k = 0; k < bins; ++k)
        triangleAt (set.fc, k * fs / fftSize, set.lowBandShort[(size_t) k], set.wShort[(size_t) k]);
}

void SpectralBank::reset()
{
    activeSet = std::clamp (settings.bandSet, 0, 2);
    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (history[(size_t) ch].begin(), history[(size_t) ch].end(), 0.0f);
        std::fill (fdl[(size_t) ch].begin(), fdl[(size_t) ch].end(), 0.0f);
        std::fill (inFrame[(size_t) ch].begin(), inFrame[(size_t) ch].end(), 0.0f);
        std::fill (outShaped[(size_t) ch].begin(), outShaped[(size_t) ch].end(), 0.0f);
        std::fill (outFull[(size_t) ch].begin(), outFull[(size_t) ch].end(), 0.0f);
        histPos[(size_t) ch] = 0;
        framePeak[(size_t) ch] = 0.0;
        auto& st = state[(size_t) ch];
        st = GainState {};
        st.program.fill (-300.0);
        st.peak.fill (-300.0);
        st.fast.fill (-300.0);
        st.slow.fill (-300.0);
        st.gShaped.fill (1.0f);
        st.gFull.fill (1.0f);
    }
    std::array<float, maxBands> ones;
    ones.fill (1.0f);
    for (int ch = 0; ch < 2; ++ch)
        for (auto& kind : hg[(size_t) ch])
            for (auto& v : kind)
                buildFilter (sets[(size_t) activeSet], ones.data(), v.data());
    fdlPos = 0;
    framePos = 0;
    cur = 0;
    frameCount = 0;
    for (auto& c : builtGains)
        for (auto& k : c)
            k.fill (1.0f);
    for (auto& c : sameAsPrev)
        c = { true, true };
    fullEq = { true, true };
    fullEqPrev = { true, true };
    pressure = 0.0;
    meterBands.store (sets[(size_t) activeSet].bands, std::memory_order_relaxed);
    for (int b = 0; b < maxBands; ++b)
    {
        bandGrDb[(size_t) b].set (0.0f);
        bandEmphDb[(size_t) b].set (0.0f);
    }
}

void SpectralBank::process (const float* inL, const float* inR, float* shapedL, float* shapedR, float* fullL, float* fullR, int n) noexcept
{
    const float* in[2] = { inL, inR };
    float* sh[2] = { shapedL, shapedR };
    float* fu[2] = { fullL, fullR };
    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            const float x = in[ch][i];
            inFrame[(size_t) ch][(size_t) (blockSize + framePos)] = x;
            auto& h = history[(size_t) ch];
            h[(size_t) histPos[(size_t) ch]] = x;
            if (++histPos[(size_t) ch] == filterLength)
                histPos[(size_t) ch] = 0;
            framePeak[(size_t) ch] = std::max (framePeak[(size_t) ch], (double) std::abs (x));
            sh[ch][i] = outShaped[(size_t) ch][(size_t) framePos];
            fu[ch][i] = outFull[(size_t) ch][(size_t) framePos];
        }
        if (++framePos == blockSize)
        {
            runFrame();
            framePos = 0;
        }
    }
}

void SpectralBank::analyse (int ch) noexcept
{
    const auto& set = sets[(size_t) activeSet];
    const auto& h = history[(size_t) ch];
    const int La = filterLength / 2;
    // long analysis: the newest La samples with a Hann window (about 21 ms ahead of the output)
    std::fill (analysisBuf.begin(), analysisBuf.begin() + 2 * La, 0.0f);
    for (int i = 0; i < La; ++i)
    {
        const int idx = (histPos[(size_t) ch] - La + i + filterLength) % filterLength;
        analysisBuf[(size_t) i] = h[(size_t) idx] * windowLong[(size_t) (2 * i)];
    }
    fftAna->performRealOnlyForwardTransform (analysisBuf.data(), true);
    auto& el = eLong[(size_t) ch];
    std::fill (el.begin(), el.end(), 0.0f);
    for (int k = 0; k <= La / 2; ++k)
    {
        const float re = analysisBuf[(size_t) (2 * k)], im = analysisBuf[(size_t) (2 * k + 1)];
        const float p = re * re + im * im;
        const int j = set.lowBandLong[(size_t) k];
        const float w = set.wLong[(size_t) k];
        el[(size_t) j] += p * w;
        if (j + 1 < set.bands)
            el[(size_t) j + 1] += p * (1.0f - w);
    }
}

void SpectralBank::analyseShort (int ch) noexcept
{
    // short analysis: the spectrum of the newest frame pair, already computed for the convolution
    const auto& set = sets[(size_t) activeSet];
    const int spec = fftSize + 2;
    const float* X = fdl[(size_t) ch].data() + (size_t) (fdlPos * spec);
    auto& es = eShort[(size_t) ch];
    std::fill (es.begin(), es.end(), 0.0f);
    for (int k = 0; k < bins; ++k)
    {
        const float re = X[2 * k], im = X[2 * k + 1];
        const float p = re * re + im * im;
        const int j = set.lowBandShort[(size_t) k];
        const float w = set.wShort[(size_t) k];
        es[(size_t) j] += p * w;
        if (j + 1 < set.bands)
            es[(size_t) j + 1] += p * (1.0f - w);
    }
}

void SpectralBank::computeGains (GainState& st, const float* el, const float* es, double pressureDb) noexcept
{
    const auto& set = sets[(size_t) activeSet];
    const int nb = set.bands;
    const double frameSec = blockSize / fs;
    static constexpr double baseRelease[] = { 0.45, 0.25, 0.12 };
    const double rel0 = baseRelease[std::clamp (settings.mode, 0, 2)];
    const double cProgram = coeffFor (frameSec, 1.5), cAttack = coeffFor (frameSec, 0.015);
    const double cFastRel = coeffFor (frameSec, 0.04), cSlow = coeffFor (frameSec, 0.25);
    const double as = std::clamp ((double) settings.adaptSpeed, 0.0, 1.0);
    const double ag = std::clamp ((double) settings.adaptGainDb, 0.0, 24.0);

    std::array<double, maxBands> excess {}, emph {};
    double sumE = 0.0, sumEX = 0.0, sumS = 0.0, sumSE = 0.0;
    for (int b = 0; b < nb; ++b)
    {
        const double L = 10.0 * std::log10 ((double) el[b] + 1.0e-20);
        auto& prog = st.program[(size_t) b];
        prog = prog < -250.0 ? L : L + cProgram * (prog - L);
        const double relB = rel0 * std::clamp (std::pow (150.0 / set.fc[(size_t) b], 0.6 * as), 0.35, 3.0);
        auto& pk = st.peak[(size_t) b];
        pk = L > pk ? L : L + coeffFor (frameSec, relB) * (pk - L);
        excess[(size_t) b] = std::max (0.0, pk - prog);
        sumE += el[b];
        sumEX += el[b] * excess[(size_t) b];

        // attack emphasis from the short analysis: fast envelope above the slow one
        const double S = 10.0 * std::log10 ((double) es[b] + 1.0e-20);
        auto& fa = st.fast[(size_t) b];
        auto& sl = st.slow[(size_t) b];
        fa = (S > fa || fa < -250.0) ? S : S + cFastRel * (fa - S);
        sl = sl < -250.0 ? S : S + cSlow * (sl - S);
        emph[(size_t) b] = settings.emphasis * std::clamp (fa - sl - 1.0, 0.0, 6.0);
        sumS += es[b];
        sumSE += es[b] * emph[(size_t) b];
    }
    const double meanExcess = sumE > 0.0 ? sumEX / sumE : 0.0;
    const double meanEmph = sumS > 0.0 ? sumSE / sumS : 0.0;
    const double w = std::clamp (pressureDb / 2.0, 0.0, 1.0); // only near the limit
    const double at = std::clamp ((double) settings.adaptTrans, 0.0, 1.0);
    const int delayFrames = std::clamp ((filterLength / 2 - blockSize) / blockSize, 0, 15);
    for (int b = 0; b < nb; ++b)
    {
        double target = -std::min (ag, std::max (0.0, excess[(size_t) b] - meanExcess)) * w;
        target = std::max (target, -pressureDb);
        auto& off = st.offset[(size_t) b];
        const double relB = rel0 * std::clamp (std::pow (150.0 / set.fc[(size_t) b], 0.6 * as), 0.35, 3.0);
        off = target < off ? target + cAttack * (off - target) : target + coeffFor (frameSec, relB) * (off - target);
        st.gShaped[(size_t) b] = (float) std::pow (10.0, off / 20.0);

        // emphasis: blend with the mean (Adaptive Trans), delayed to line up with the output
        st.emphDelay[(size_t) st.emphPos][(size_t) b] = (float) (at * emph[(size_t) b] + (1.0 - at) * meanEmph);
        const int rd = (st.emphPos - delayFrames + 16) % 16;
        const float e = st.emphDelay[(size_t) rd][(size_t) b];
        st.gFull[(size_t) b] = st.gShaped[(size_t) b] * (float) std::pow (10.0, e / 20.0);
    }
    st.emphPos = (st.emphPos + 1) % 16;
}

void SpectralBank::buildFilter (const BandSetData& set, const float* gains, float* dest) noexcept
{
    const int spec = fftSize + 2;
    juce::FloatVectorOperations::clear (dest, partitions * spec);
    for (int b = 0; b < set.bands; ++b)
    {
        const float* src = set.spectra.data() + (size_t) b * (size_t) partitions * (size_t) spec;
        juce::FloatVectorOperations::addWithMultiply (dest, src, gains[b], partitions * spec);
    }
}

void SpectralBank::convolve (int ch, const float* filter, float* out) noexcept
{
    const int spec = fftSize + 2;
    std::fill (work.begin(), work.begin() + 2 * fftSize, 0.0f);
    float* y = work.data();
    const auto& x = fdl[(size_t) ch];
    for (int p = 0; p < partitions; ++p)
    {
        const float* X = x.data() + (size_t) (((fdlPos - p + partitions) % partitions) * spec);
        const float* H = filter + (size_t) (p * spec);
        for (int k = 0; k < bins; ++k)
        {
            const float xr = X[2 * k], xi = X[2 * k + 1], hr = H[2 * k], hi = H[2 * k + 1];
            y[2 * k] += xr * hr - xi * hi;
            y[2 * k + 1] += xr * hi + xi * hr;
        }
    }
    fftBlock->performRealOnlyInverseTransform (y);
    std::copy (y + blockSize, y + 2 * blockSize, out);
}

void SpectralBank::runFrame() noexcept
{
    const int spec = fftSize + 2;
    const auto& set = sets[(size_t) activeSet];

    // pressure: how far the input peaks rise above the ceiling (feed-forward, frame rate)
    const double frameSec = blockSize / fs;
    const double peak = std::max (framePeak[0], framePeak[1]);
    framePeak = { 0.0, 0.0 };
    const double need = peak > settings.ceiling ? 20.0 * std::log10 (peak / settings.ceiling) : 0.0;
    pressure = need > pressure ? need + coeffFor (frameSec, 0.01) * (pressure - need) : need + coeffFor (frameSec, 0.4) * (pressure - need);

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (work2.begin(), work2.begin() + 2 * fftSize, 0.0f);
        std::copy (inFrame[(size_t) ch].begin(), inFrame[(size_t) ch].end(), work2.begin());
        fftBlock->performRealOnlyForwardTransform (work2.data(), true);
        std::copy (work2.begin(), work2.begin() + spec, fdl[(size_t) ch].begin() + (long) (fdlPos * spec));
        if (frameCount % 2 == 0)
            analyse (ch); // the long analysis every second frame (about 11 ms)
        analyseShort (ch);
    }
    ++frameCount;

    const bool linked = settings.link >= 0.999f;
    std::array<float, maxBands> a {}, s {};
    for (int ch = 0; ch < (linked ? 1 : 2); ++ch)
    {
        for (int b = 0; b < set.bands; ++b)
        {
            const float maxL = std::max (eLong[0][(size_t) b], eLong[1][(size_t) b]);
            const float maxS = std::max (eShort[0][(size_t) b], eShort[1][(size_t) b]);
            a[(size_t) b] = linked ? maxL : settings.link * maxL + (1.0f - settings.link) * eLong[(size_t) ch][(size_t) b];
            s[(size_t) b] = linked ? maxS : settings.link * maxS + (1.0f - settings.link) * eShort[(size_t) ch][(size_t) b];
        }
        computeGains (state[(size_t) ch], a.data(), s.data(), pressure);
        // full = shaped when there is no attack emphasis: copy instead of building
        const auto& gs = state[(size_t) ch].gShaped;
        const auto& gf = state[(size_t) ch].gFull;
        bool eq = true;
        for (int b = 0; b < set.bands && eq; ++b)
            eq = std::abs (gf[(size_t) b] - gs[(size_t) b]) < 1.0e-6f;
        fullEqPrev[(size_t) ch] = fullEq[(size_t) ch];
        fullEq[(size_t) ch] = eq;
        for (int kind = 0; kind < 2; ++kind)
        {
            const auto& g = kind == 0 ? gs : gf;
            auto& last = builtGains[(size_t) ch][(size_t) kind];
            bool same = true; // changes below 0.01 dB do not rebuild the filter
            for (int b = 0; b < set.bands && same; ++b)
                same = std::abs (g[(size_t) b] - last[(size_t) b]) < 1.2e-3f * last[(size_t) b];
            auto& dst = hg[(size_t) ch][(size_t) kind][(size_t) cur];
            const auto& prev = hg[(size_t) ch][(size_t) kind][(size_t) (1 - cur)];
            if (same && sameAsPrev[(size_t) ch][(size_t) kind])
            {
                // both slots already hold this filter; nothing to build
            }
            else if (same)
                std::copy (prev.begin(), prev.end(), dst.begin());
            else if (kind == 1 && eq)
            {
                const auto& shapedNow = hg[(size_t) ch][0][(size_t) cur];
                std::copy (shapedNow.begin(), shapedNow.end(), dst.begin());
                last = builtGains[(size_t) ch][0];
            }
            else
            {
                buildFilter (set, g.data(), dst.data());
                last = g;
            }
            sameAsPrev[(size_t) ch][(size_t) kind] = same;
        }
    }

    float* prevOut = work2.data();
    float* curOut = work2.data() + blockSize;
    for (int ch = 0; ch < 2; ++ch)
    {
        const int src = linked ? 0 : ch;
        for (int kind = 0; kind < 2; ++kind)
        {
            auto& out = kind == 0 ? outShaped[(size_t) ch] : outFull[(size_t) ch];
            if (kind == 1 && fullEq[(size_t) src] && fullEqPrev[(size_t) src])
            {
                // no emphasis: the full output equals the shaped one
                std::copy (outShaped[(size_t) ch].begin(), outShaped[(size_t) ch].end(), out.begin());
                continue;
            }
            if (sameAsPrev[(size_t) src][(size_t) kind])
            {
                convolve (ch, hg[(size_t) src][(size_t) kind][(size_t) cur].data(), out.data());
                continue;
            }
            convolve (ch, hg[(size_t) src][(size_t) kind][(size_t) (1 - cur)].data(), prevOut);
            convolve (ch, hg[(size_t) src][(size_t) kind][(size_t) cur].data(), curOut);
            for (int i = 0; i < blockSize; ++i)
            {
                const float t = (float) (i + 1) / (float) blockSize;
                out[(size_t) i] = prevOut[i] + (curOut[i] - prevOut[i]) * t;
            }
        }
        // the second half of the frame becomes the first half of the next one
        auto& f = inFrame[(size_t) ch];
        std::copy (f.begin() + blockSize, f.end(), f.begin());
    }
    fdlPos = (fdlPos + 1) % partitions;
    cur = 1 - cur;

    // meters: the deeper reduction and the stronger emphasis of the two channels
    meterBands.store (set.bands, std::memory_order_relaxed);
    for (int b = 0; b < set.bands; ++b)
    {
        const int n = linked ? 1 : 2;
        double gr = 0.0, em = 0.0;
        for (int ch = 0; ch < n; ++ch)
        {
            gr = std::min (gr, state[(size_t) ch].offset[(size_t) b]);
            em = std::max (em, 20.0 * std::log10 (std::max (1.0e-6f, state[(size_t) ch].gFull[(size_t) b] / state[(size_t) ch].gShaped[(size_t) b])));
        }
        bandGrDb[(size_t) b].set ((float) gr);
        bandEmphDb[(size_t) b].set ((float) em);
    }
}
} // namespace mm::dsp
