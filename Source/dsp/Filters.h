#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <vector>

/*  Basic filter building blocks. All states are double precision so that low-frequency filters at
    high sample rates stay accurate. Nothing here allocates after construction. */
namespace mm::dsp
{
inline constexpr double pi = 3.14159265358979323846;

/** Normalised biquad: y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2. A first-order section has b2 = a2 = 0. */
struct Coeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    static Coeffs identity() { return {}; }

    // RBJ audio EQ cookbook designs
    static Coeffs lowpass (double fs, double f, double q);
    static Coeffs highpass (double fs, double f, double q);
    static Coeffs bandpass (double fs, double f, double q); // constant 0 dB peak gain
    static Coeffs peak (double fs, double f, double q, double gainDb);
    static Coeffs lowShelf (double fs, double f, double q, double gainDb);
    static Coeffs highShelf (double fs, double f, double q, double gainDb);
    static Coeffs notch (double fs, double f, double q);
    static Coeffs allpass (double fs, double f, double q);

    // First-order sections (bilinear transform)
    static Coeffs lowpass1 (double fs, double f);
    static Coeffs highpass1 (double fs, double f);
    static Coeffs allpass1 (double fs, double f);

    /** Complex response at frequency f. */
    std::complex<double> response (double fs, double f) const;
    double magnitudeDb (double fs, double f) const { return 20.0 * std::log10 (std::max (1.0e-12, std::abs (response (fs, f)))); }
};

/** Transposed direct form II biquad. */
class Biquad
{
public:
    void setCoeffs (const Coeffs& c) noexcept { k = c; }
    const Coeffs& coeffs() const noexcept { return k; }
    void reset() noexcept { s1 = s2 = 0.0; }

    inline double process (double x) noexcept
    {
        const double y = k.b0 * x + s1;
        s1 = k.b1 * x - k.a1 * y + s2;
        s2 = k.b2 * x - k.a2 * y;
        return y;
    }

private:
    Coeffs k;
    double s1 = 0.0, s2 = 0.0;
};

/** Up to 8 cascaded sections (enough for 48 dB/oct). */
class Cascade
{
public:
    static constexpr int maxSections = 8;

    void setSections (const Coeffs* c, int count) noexcept
    {
        num = std::min (count, maxSections);
        for (int i = 0; i < num; ++i)
            sections[(size_t) i].setCoeffs (c[i]);
    }

    void setSections (const std::vector<Coeffs>& c) noexcept { setSections (c.data(), (int) c.size()); }

    void reset() noexcept
    {
        for (auto& s : sections)
            s.reset();
    }

    inline double process (double x) noexcept
    {
        for (int i = 0; i < num; ++i)
            x = sections[(size_t) i].process (x);
        return x;
    }

    int numSections() const noexcept { return num; }
    const Coeffs& section (int i) const noexcept { return sections[(size_t) i].coeffs(); }

    std::complex<double> response (double fs, double f) const
    {
        std::complex<double> h { 1.0, 0.0 };
        for (int i = 0; i < num; ++i)
            h *= sections[(size_t) i].coeffs().response (fs, f);
        return h;
    }

private:
    std::array<Biquad, maxSections> sections;
    int num = 0;
};

/** Butterworth design of the given order (1..8) as cascaded sections. */
std::vector<Coeffs> butterworth (int order, bool highpass, double fs, double f);

/** Butterworth allpass with the same poles as butterworth(order): B(-s)/B(s). */
std::vector<Coeffs> butterworthAllpass (int order, double fs, double f);

/** Linkwitz-Riley two-way split of order 2, 4 or 8 (12, 24, 48 dB/oct). lo + hi equals an allpass,
    whose sections are available through makeAllpass() for phase compensation of other bands. */
class LinkwitzRiley
{
public:
    void setup (int order, double fs, double f);
    void reset() noexcept;

    inline void process (double x, double& lo, double& hi) noexcept
    {
        lo = lp.process (x);
        hi = hp.process (x) * hpSign;
    }

    /** The allpass equivalent of lo + hi (same order and frequency). */
    void makeAllpass (Cascade& ap) const { ap.setSections (apSections); }

    std::complex<double> lowResponse (double fs, double f) const { return lp.response (fs, f); }
    std::complex<double> highResponse (double fs, double f) const { return hp.response (fs, f) * hpSign; }

private:
    Cascade lp, hp;
    std::vector<Coeffs> apSections;
    double hpSign = 1.0;
};

/** Single-pole smoother of a gain or level with separate attack and release times. */
class Envelope
{
public:
    void setup (double fs, double attackMs, double releaseMs) noexcept
    {
        att = coeff (fs, attackMs);
        rel = coeff (fs, releaseMs);
    }

    inline double process (double x) noexcept
    {
        const double c = x > state ? att : rel;
        state = x + c * (state - x);
        return state;
    }

    void reset (double v = 0.0) noexcept { state = v; }
    double value() const noexcept { return state; }

    static double coeff (double fs, double ms) noexcept
    {
        return ms <= 0.0 ? 0.0 : std::exp (-1.0 / (0.001 * ms * fs));
    }

private:
    double att = 0.0, rel = 0.0, state = 0.0;
};

/** Fixed-capacity multichannel delay line (latency compensation). Allocates only in prepare. */
class DelayLine
{
public:
    void prepare (int channels, int maxDelay)
    {
        capacity = std::max (1, maxDelay + 1);
        data.assign ((size_t) channels, std::vector<float> ((size_t) capacity, 0.0f));
        pos = 0;
    }

    void setDelay (int samples) noexcept { delay = std::min (std::max (0, samples), capacity - 1); }
    int getDelay() const noexcept { return delay; }

    void reset() noexcept
    {
        for (auto& c : data)
            std::fill (c.begin(), c.end(), 0.0f);
        pos = 0;
    }

    /** Delays numSamples of each channel in place. */
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (delay == 0)
            return;

        int p = pos;
        const int nc = std::min (numChannels, (int) data.size());
        for (int i = 0; i < numSamples; ++i)
        {
            int r = p - delay;
            if (r < 0)
                r += capacity;
            for (int ch = 0; ch < nc; ++ch)
            {
                auto& line = data[(size_t) ch];
                line[(size_t) p] = channels[ch][i];
                channels[ch][i] = line[(size_t) r];
            }
            if (++p == capacity)
                p = 0;
        }
        pos = p;
    }

private:
    std::vector<std::vector<float>> data;
    int capacity = 1, delay = 0, pos = 0;
};

inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
inline double gainToDb (double g) noexcept { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }
} // namespace mm::dsp
