#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace mm::dsp
{
/** Polyphase interpolator (4x or 8x) for true-peak detection (ITU-R BS.1770 Annex 2 method).

    The standard's example filter is replaced by a Kaiser-windowed sinc centred on an input sample, so
    phase 0 reproduces the input exactly and the other phases interpolate (see docs/DECISIONS.md,
    D-015). For every input sample it returns the largest absolute value of the interpolated points
    between that sample and the next one. The result is delayed by latency() input samples. */
template <int Factor, int Taps, int BetaTimes10 = 80>
class TruePeakDetectorT
{
public:
    static constexpr int factor = Factor;
    static constexpr int tapsPerPhase = Taps;

    explicit TruePeakDetectorT (double cutoffOfInputRate = 0.5) { design (cutoffOfInputRate); }

    void reset() noexcept
    {
        history.fill (0.0f);
        pos = 0;
    }

    static constexpr int latency() noexcept { return tapsPerPhase / 2; }

    inline float process (float x) noexcept
    {
        // circular history, duplicated so that the newest tapsPerPhase samples are contiguous
        pos = (pos == 0 ? tapsPerPhase : pos) - 1;
        history[(size_t) pos] = x;
        history[(size_t) pos + tapsPerPhase] = x;
        const float* h = history.data() + pos;

        float peak = std::abs (h[tapsPerPhase / 2]); // phase 0 is the input sample itself
        for (int p = 1; p < factor; ++p)
        {
            const float* c = coeffs[(size_t) p].data();
            float acc = 0.0f;
            for (int k = 0; k < tapsPerPhase; ++k)
                acc += c[k] * h[k];
            peak = std::max (peak, std::abs (acc));
        }
        return peak;
    }

private:
    void design (double cutoff)
    {
        constexpr int total = factor * tapsPerPhase;
        constexpr double beta = BetaTimes10 / 10.0;
        // centre on a multiple of the factor: with cutoff 0.5 phase 0 passes the input samples exactly
        const double centre = total / 2.0;
        std::array<double, total> h {};
        for (int n = 0; n < total; ++n)
        {
            const double t = (n - centre) / factor;
            const double x = 2.0 * cutoff * t;
            const double sinc = std::abs (x) < 1.0e-12 ? 1.0 : std::sin (3.14159265358979323846 * x) / (3.14159265358979323846 * x);
            const double r = (n - centre) / centre;
            const double w = besselI0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / besselI0 (beta);
            h[(size_t) n] = 2.0 * cutoff * sinc * w;
        }
        for (int p = 0; p < factor; ++p)
        {
            double sum = 0.0;
            for (int k = 0; k < tapsPerPhase; ++k)
                sum += h[(size_t) (k * factor + p)];
            for (int k = 0; k < tapsPerPhase; ++k)
                coeffs[(size_t) p][(size_t) k] = (float) (h[(size_t) (k * factor + p)] / sum); // unity DC gain per phase
        }
    }

    static double besselI0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    std::array<std::array<float, tapsPerPhase>, factor> coeffs {};
    std::array<float, 2 * tapsPerPhase> history {};
    int pos = 0;
};

using TruePeakDetector = TruePeakDetectorT<4, 16>;  // meters (BS.1770 uses 4x)
using TruePeakDetector8 = TruePeakDetectorT<8, 96>; // limiter detection: 768 taps, flat to ~0.47 fs
} // namespace mm::dsp
