#include "dsp/DynamicEq.h"

namespace mm::dsp
{
namespace
{
constexpr int cutOrders[] = { 1, 2, 3, 4, 6, 8 };

bool hasGain (EqKind k) { return k == EqKind::bell || k == EqKind::lowShelf || k == EqKind::highShelf; }

int firLengthFor (double fs)
{
    if (fs <= 50000.0)
        return 4096;
    if (fs <= 100000.0)
        return 8192;
    return 16384;
}

/** Sections for one band; returns the number of sections written. */
int designSections (EqKind kind, int slope, double fs, double f, double q, double g, Coeffs* out)
{
    switch (kind)
    {
        case EqKind::bell: out[0] = Coeffs::peak (fs, f, q, g); return 1;
        case EqKind::lowShelf: out[0] = Coeffs::lowShelf (fs, f, std::min (q, 2.0), g); return 1;
        case EqKind::highShelf: out[0] = Coeffs::highShelf (fs, f, std::min (q, 2.0), g); return 1;
        case EqKind::notch: out[0] = Coeffs::notch (fs, f, q); return 1;
        case EqKind::lowCut:
        case EqKind::highCut:
        {
            const auto s = butterworth (cutOrders[std::clamp (slope, 0, 5)], kind == EqKind::lowCut, fs, f);
            for (size_t i = 0; i < s.size(); ++i)
                out[i] = s[i];
            return (int) s.size();
        }
    }
    return 0;
}
} // namespace

EqKind DynamicEq::kindOf (int bandIndex, int typeChoice) noexcept
{
    if (bandIndex == 0)
        return typeChoice == 1 ? EqKind::lowCut : EqKind::lowShelf;
    if (bandIndex == numBands - 1)
        return typeChoice == 1 ? EqKind::highCut : EqKind::highShelf;
    switch (typeChoice)
    {
        case 1: return EqKind::lowShelf;
        case 2: return EqKind::highShelf;
        case 3: return EqKind::notch;
        default: return EqKind::bell;
    }
}

double DynamicEq::bandResponseDb (const Band& b, int bandIndex, double fs, double f, float gainDb)
{
    Coeffs c[Cascade::maxSections];
    const int n = designSections (kindOf (bandIndex, b.type), b.slope, fs, b.freq, b.q, gainDb, c);
    double db = 0.0;
    for (int i = 0; i < n; ++i)
        db += c[i].magnitudeDb (fs, f);
    return db;
}

double DynamicEq::responseDb (const Settings& s, double fs, double f, const float* dynamicGainsDb)
{
    double db = 0.0;
    for (int b = 0; b < numBands; ++b)
        if (s.bands[(size_t) b].on)
            db += bandResponseDb (s.bands[(size_t) b], b, fs, f, dynamicGainsDb != nullptr ? dynamicGainsDb[b] : s.bands[(size_t) b].gainDb);
    return db;
}

void DynamicEq::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    maxBlock = ctx.maxBlock;
    firLength = firLengthFor (fs);
    scratch.setSize (3, maxBlock, false, true, false);
    const juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 1 };
    for (auto& c : conv)
        c.prepare (spec);
    convPrepared = true;
    reset();
}

void DynamicEq::reset()
{
    for (int b = 0; b < numBands; ++b)
    {
        auto& st = state[(size_t) b];
        const auto& band = settings.bands[(size_t) b];
        st.freq.reset (band.freq);
        st.gain.reset (band.gainDb);
        st.q.reset (band.q);
        st.env.reset (0.0);
        st.dynAmount = 0.0;
        st.wasOn = false;
        st.lastType = st.lastSlope = -1;
        for (int ch = 0; ch < 2; ++ch)
        {
            st.filter[(size_t) ch].reset();
            st.detector[(size_t) ch].reset();
            st.delta[(size_t) ch].reset();
        }
        meters[(size_t) b].set (band.gainDb);
    }
    for (auto& c : conv)
        c.reset();
}

void DynamicEq::designBand (int index, BandState& st, double f, double g, double q, bool deltaOnly) noexcept
{
    const auto& b = settings.bands[(size_t) index];
    const auto kind = kindOf (index, b.type);
    Coeffs c[Cascade::maxSections];
    const int n = designSections (kind, b.slope, fs, f, q, g, c);
    auto& target = deltaOnly ? st.delta : st.filter;
    for (auto& cas : target)
        cas.setSections (c, n);

    Coeffs d;
    switch (kind)
    {
        case EqKind::lowShelf: d = Coeffs::lowpass (fs, f, 0.71); break;
        case EqKind::highShelf: d = Coeffs::highpass (fs, f, 0.71); break;
        default: d = Coeffs::bandpass (fs, f, std::max (0.3, q)); break;
    }
    for (auto& det : st.detector)
        det.setCoeffs (d);
}

void DynamicEq::process (float* left, float* right, int numSamples) noexcept
{
    if (settings.linearPhase)
        processLinearPhase (left, right, numSamples);
    else
        processMinimumPhase (left, right, numSamples);
}

void DynamicEq::processMinimumPhase (float* left, float* right, int numSamples) noexcept
{
    runBands (left, right, numSamples, false);
}

void DynamicEq::runBands (float* left, float* right, int numSamples, bool deltaOnly) noexcept
{
    const int rampSamples = (int) (0.03 * fs);
    for (int b = 0; b < numBands; ++b)
    {
        auto& st = state[(size_t) b];
        const auto& band = settings.bands[(size_t) b];
        if (! band.on)
        {
            st.wasOn = false;
            meters[(size_t) b].set (band.gainDb);
            continue;
        }
        if (! st.wasOn)
        {
            st.freq.reset (band.freq);
            st.gain.reset (band.gainDb);
            st.q.reset (band.q);
            st.env.reset (0.0);
            for (int ch = 0; ch < 2; ++ch)
            {
                st.filter[(size_t) ch].reset();
                st.delta[(size_t) ch].reset();
                st.detector[(size_t) ch].reset();
            }
            st.lastType = -1;
            st.wasOn = true;
        }
        st.freq.setTarget (band.freq, rampSamples);
        st.gain.setTarget (band.gainDb, rampSamples / controlBlock + 1);
        st.q.setTarget (band.q, rampSamples / controlBlock + 1);
        st.env.setup (fs, band.attackMs, band.releaseMs);
    }

    float m[controlBlock], s[controlBlock];
    for (int start = 0; start < numSamples; start += controlBlock)
    {
        const int len = std::min (controlBlock, numSamples - start);
        float* l = left + start;
        float* r = right + start;

        for (int b = 0; b < numBands; ++b)
        {
            const auto& band = settings.bands[(size_t) b];
            if (! band.on)
                continue;
            auto& st = state[(size_t) b];
            const auto kind = kindOf (b, band.type);
            const bool dyn = band.dynamic && hasGain (kind);
            if (deltaOnly && ! dyn)
                continue;

            const bool ramping = st.freq.isRamping() || st.gain.isRamping() || st.q.isRamping();
            const double f = st.freq.next(), g = st.gain.next(), q = st.q.next();
            double gEff = g;
            if (dyn)
            {
                const double level = gainToDb (st.env.value() + 1.0e-9);
                const double amount = std::clamp (level - band.thresholdDb, 0.0, (double) band.rangeDb);
                gEff = band.mode == 0 ? g - amount : g + amount;
            }
            meters[(size_t) b].set ((float) (hasGain (kind) ? gEff : g));

            if (ramping || dyn || st.lastType != band.type || st.lastSlope != band.slope)
            {
                designBand (b, st, f, deltaOnly ? gEff - g : gEff, q, deltaOnly);
                st.lastType = band.type;
                st.lastSlope = band.slope;
            }

            auto& filt = deltaOnly ? st.delta : st.filter;
            const auto run = [&] (float* x, int ch, bool detect)
            {
                auto& f0 = filt[(size_t) ch];
                auto& det = st.detector[(size_t) ch];
                double peak = 0.0;
                for (int i = 0; i < len; ++i)
                {
                    if (detect)
                        peak = std::max (peak, std::abs (det.process (x[i])));
                    x[i] = (float) f0.process (x[i]);
                }
                return peak;
            };

            double peak = 0.0;
            switch (band.placement)
            {
                case 0: // stereo
                    peak = std::max (run (l, 0, dyn), run (r, 1, dyn));
                    break;
                case 3: peak = run (l, 0, dyn); break;
                case 4: peak = run (r, 1, dyn); break;
                default: // mid or side
                {
                    for (int i = 0; i < len; ++i)
                    {
                        m[i] = 0.5f * (l[i] + r[i]);
                        s[i] = 0.5f * (l[i] - r[i]);
                    }
                    peak = run (band.placement == 1 ? m : s, 0, dyn);
                    for (int i = 0; i < len; ++i)
                    {
                        l[i] = m[i] + s[i];
                        r[i] = m[i] - s[i];
                    }
                    break;
                }
            }
            if (dyn)
                for (int i = 0; i < len; ++i) // block peak drives the envelope at the sample rate
                    st.env.process (peak);
        }
    }
}

void DynamicEq::processLinearPhase (float* left, float* right, int numSamples) noexcept
{
    for (int start = 0; start < numSamples; start += maxBlock)
    {
        const int len = std::min (maxBlock, numSamples - start);
        float* l = left + start;
        float* r = right + start;
        float* inL = scratch.getWritePointer (0);
        float* inR = scratch.getWritePointer (1);
        float* tmp = scratch.getWritePointer (2);
        std::copy (l, l + len, inL);
        std::copy (r, r + len, inR);

        const auto convolve = [&] (int k, float* src, float* dst)
        {
            float* srcPtr[1] = { src };
            float* dstPtr[1] = { dst };
            juce::dsp::AudioBlock<float> in (srcPtr, 1, (size_t) len);
            juce::dsp::AudioBlock<float> out (dstPtr, 1, (size_t) len);
            conv[(size_t) k].process (juce::dsp::ProcessContextNonReplacing<float> (in, out));
        };

        convolve (0, inL, l);
        convolve (1, inR, tmp);
        for (int i = 0; i < len; ++i)
            l[i] += tmp[i];
        convolve (2, inL, r);
        convolve (3, inR, tmp);
        for (int i = 0; i < len; ++i)
            r[i] += tmp[i];
    }

    runBands (left, right, numSamples, true); // dynamic part as minimum-phase deltas
}

void DynamicEq::buildLinearPhase (const Settings& snap)
{
    const int n = firLength;
    const int order = juce::roundToInt (std::log2 ((double) n));
    juce::dsp::FFT fft (order);

    // Determine the inverse transform scaling once (JUCE versions differ).
    std::vector<float> probe ((size_t) (2 * n), 0.0f);
    for (int k = 0; k <= n / 2; ++k)
        probe[(size_t) (2 * k)] = 1.0f;
    fft.performRealOnlyInverseTransform (probe.data());
    const double scale = 1.0 / std::max (1.0e-9, (double) probe[0]);

    // Magnitudes per placement group on the FFT grid
    std::vector<double> hl ((size_t) (n / 2 + 1), 0.0), hr (hl), hm (hl), hs (hl);
    for (int b = 0; b < numBands; ++b)
    {
        const auto& band = snap.bands[(size_t) b];
        if (! band.on)
            continue;
        for (int k = 0; k <= n / 2; ++k)
        {
            const double f = std::max (1.0e-3, k * fs / n);
            const double db = bandResponseDb (band, b, fs, f, band.gainDb);
            switch (band.placement)
            {
                case 0: hl[(size_t) k] += db; hr[(size_t) k] += db; break;
                case 1: hm[(size_t) k] += db; break;
                case 2: hs[(size_t) k] += db; break;
                case 3: hl[(size_t) k] += db; break;
                default: hr[(size_t) k] += db; break;
            }
        }
    }

    for (int kernel = 0; kernel < 4; ++kernel)
    {
        std::vector<float> spec ((size_t) (2 * n), 0.0f);
        for (int k = 0; k <= n / 2; ++k)
        {
            const double l = std::pow (10.0, hl[(size_t) k] / 20.0), r = std::pow (10.0, hr[(size_t) k] / 20.0);
            const double mm = std::pow (10.0, hm[(size_t) k] / 20.0), ss = std::pow (10.0, hs[(size_t) k] / 20.0);
            double h = 0.0;
            switch (kernel)
            {
                case 0: h = l * (mm + ss) * 0.5; break; // L -> L
                case 1: h = r * (mm - ss) * 0.5; break; // R -> L
                case 2: h = l * (mm - ss) * 0.5; break; // L -> R
                default: h = r * (mm + ss) * 0.5; break; // R -> R
            }
            // linear phase: delay of n/2 samples -> phase factor (-1)^k
            spec[(size_t) (2 * k)] = (float) (h * ((k & 1) ? -1.0 : 1.0));
        }
        fft.performRealOnlyInverseTransform (spec.data());

        juce::AudioBuffer<float> ir (1, n);
        auto* d = ir.getWritePointer (0);
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.42 - 0.5 * std::cos (2.0 * pi * i / (n - 1)) + 0.08 * std::cos (4.0 * pi * i / (n - 1));
            d[i] = (float) (spec[(size_t) i] * scale * w);
        }
        conv[(size_t) kernel].loadImpulseResponse (std::move (ir), fs, juce::dsp::Convolution::Stereo::no,
                                                    juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    }
}
} // namespace mm::dsp
