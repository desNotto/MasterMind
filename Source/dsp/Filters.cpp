#include "dsp/Filters.h"

#include <algorithm>

namespace mm::dsp
{
namespace
{
double clampFreq (double fs, double f) { return std::clamp (f, 1.0, fs * 0.49); }

Coeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
{
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

struct Rbj
{
    double cw, sw, alpha;
    Rbj (double fs, double f, double q)
    {
        const double w = 2.0 * pi * clampFreq (fs, f) / fs;
        cw = std::cos (w);
        sw = std::sin (w);
        alpha = sw / (2.0 * std::max (q, 1.0e-4));
    }
};
} // namespace

Coeffs Coeffs::lowpass (double fs, double f, double q)
{
    const Rbj r (fs, f, q);
    return normalise ((1 - r.cw) / 2, 1 - r.cw, (1 - r.cw) / 2, 1 + r.alpha, -2 * r.cw, 1 - r.alpha);
}

Coeffs Coeffs::highpass (double fs, double f, double q)
{
    const Rbj r (fs, f, q);
    return normalise ((1 + r.cw) / 2, -(1 + r.cw), (1 + r.cw) / 2, 1 + r.alpha, -2 * r.cw, 1 - r.alpha);
}

Coeffs Coeffs::bandpass (double fs, double f, double q)
{
    const Rbj r (fs, f, q);
    return normalise (r.alpha, 0, -r.alpha, 1 + r.alpha, -2 * r.cw, 1 - r.alpha);
}

Coeffs Coeffs::peak (double fs, double f, double q, double gainDb)
{
    const Rbj r (fs, f, q);
    const double a = std::pow (10.0, gainDb / 40.0);
    return normalise (1 + r.alpha * a, -2 * r.cw, 1 - r.alpha * a, 1 + r.alpha / a, -2 * r.cw, 1 - r.alpha / a);
}

Coeffs Coeffs::lowShelf (double fs, double f, double q, double gainDb)
{
    const Rbj r (fs, f, q);
    const double a = std::pow (10.0, gainDb / 40.0);
    const double sa = 2.0 * std::sqrt (a) * r.alpha;
    return normalise (a * ((a + 1) - (a - 1) * r.cw + sa),
                      2 * a * ((a - 1) - (a + 1) * r.cw),
                      a * ((a + 1) - (a - 1) * r.cw - sa),
                      (a + 1) + (a - 1) * r.cw + sa,
                      -2 * ((a - 1) + (a + 1) * r.cw),
                      (a + 1) + (a - 1) * r.cw - sa);
}

Coeffs Coeffs::highShelf (double fs, double f, double q, double gainDb)
{
    const Rbj r (fs, f, q);
    const double a = std::pow (10.0, gainDb / 40.0);
    const double sa = 2.0 * std::sqrt (a) * r.alpha;
    return normalise (a * ((a + 1) + (a - 1) * r.cw + sa),
                      -2 * a * ((a - 1) + (a + 1) * r.cw),
                      a * ((a + 1) + (a - 1) * r.cw - sa),
                      (a + 1) - (a - 1) * r.cw + sa,
                      2 * ((a - 1) - (a + 1) * r.cw),
                      (a + 1) - (a - 1) * r.cw - sa);
}

Coeffs Coeffs::notch (double fs, double f, double q)
{
    const Rbj r (fs, f, q);
    return normalise (1, -2 * r.cw, 1, 1 + r.alpha, -2 * r.cw, 1 - r.alpha);
}

Coeffs Coeffs::allpass (double fs, double f, double q)
{
    const Rbj r (fs, f, q);
    return normalise (1 - r.alpha, -2 * r.cw, 1 + r.alpha, 1 + r.alpha, -2 * r.cw, 1 - r.alpha);
}

Coeffs Coeffs::lowpass1 (double fs, double f)
{
    const double k = std::tan (pi * clampFreq (fs, f) / fs);
    return normalise (k, k, 0, k + 1, k - 1, 0);
}

Coeffs Coeffs::highpass1 (double fs, double f)
{
    const double k = std::tan (pi * clampFreq (fs, f) / fs);
    return normalise (1, -1, 0, k + 1, k - 1, 0);
}

Coeffs Coeffs::allpass1 (double fs, double f)
{
    const double k = std::tan (pi * clampFreq (fs, f) / fs);
    return normalise (k - 1, k + 1, 0, k + 1, k - 1, 0);
}

std::complex<double> Coeffs::response (double fs, double f) const
{
    const double w = 2.0 * pi * f / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
    return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
}

std::vector<Coeffs> butterworth (int order, bool highpass, double fs, double f)
{
    std::vector<Coeffs> out;
    order = std::clamp (order, 1, 16);
    for (int k = 0; k < order / 2; ++k)
    {
        const double q = 1.0 / (2.0 * std::cos ((2.0 * k + 1.0) * pi / (2.0 * order)));
        out.push_back (highpass ? Coeffs::highpass (fs, f, q) : Coeffs::lowpass (fs, f, q));
    }
    if (order % 2 == 1)
        out.push_back (highpass ? Coeffs::highpass1 (fs, f) : Coeffs::lowpass1 (fs, f));
    return out;
}

std::vector<Coeffs> butterworthAllpass (int order, double fs, double f)
{
    std::vector<Coeffs> out;
    for (int k = 0; k < order / 2; ++k)
    {
        const double q = 1.0 / (2.0 * std::cos ((2.0 * k + 1.0) * pi / (2.0 * order)));
        out.push_back (Coeffs::allpass (fs, f, q));
    }
    if (order % 2 == 1)
        out.push_back (Coeffs::allpass1 (fs, f));
    return out;
}

void LinkwitzRiley::setup (int order, double fs, double f)
{
    const int bw = std::max (1, order / 2);
    auto lo = butterworth (bw, false, fs, f);
    auto hi = butterworth (bw, true, fs, f);
    std::vector<Coeffs> lo2 (lo), hi2 (hi);
    lo2.insert (lo2.end(), lo.begin(), lo.end());
    hi2.insert (hi2.end(), hi.begin(), hi.end());
    lp.setSections (lo2);
    hp.setSections (hi2);
    apSections = butterworthAllpass (bw, fs, f);
    // LR2 (two first-order Butterworth sections) needs an inverted high band to sum to an allpass.
    hpSign = (bw % 2 == 1) ? -1.0 : 1.0;
}

void LinkwitzRiley::reset() noexcept
{
    lp.reset();
    hp.reset();
}
} // namespace mm::dsp
