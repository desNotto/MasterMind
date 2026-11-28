#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace mm::dsp
{
/** Low-End phase Auto (mockup v18): finds the rotator angle that makes the low band most symmetric.

    The rotator is an allpass, so it commutes with the low-band filter of the analysis: rotating the
    analysed low-band waveform (20-150 Hz of the mid signal at the low analysis rate) by a copy of
    the rotator at that rate gives the low band of the rotated signal. The search tries every 5
    degrees and keeps the angle with the smallest mean peak asymmetry over the segments. */
namespace phaseauto
{
/** Peak asymmetry in percent: (positive peak - negative peak) / larger peak, as the analysis. */
inline double asymmetry (const float* x, int n) noexcept
{
    float pos = 0.0f, neg = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        pos = std::max (pos, x[i]);
        neg = std::max (neg, -x[i]);
    }
    const float mx = std::max (pos, neg);
    return mx > 1.0e-6f ? 100.0 * (pos - neg) / mx : 0.0;
}

/** The rotator of LowEnd (four first-order allpasses) applied to x at the given rate. */
inline void rotate (const std::vector<float>& x, std::vector<float>& y, double rate, double freq, double angleDeg)
{
    constexpr int stages = 4;
    constexpr double kPi = 3.14159265358979323846;
    const double phi = std::clamp (angleDeg, 0.0, 360.0) * kPi / 180.0 / stages;
    const double w = 2.0 * kPi * std::clamp (freq, 10.0, rate * 0.45) / rate;
    const double r = std::tan (phi * 0.5) / std::tan (w * 0.5);
    const double a = (1.0 - r) / (1.0 + r);
    y.resize (x.size());
    double z[stages] {};
    for (size_t i = 0; i < x.size(); ++i)
    {
        double v = x[i];
        for (int k = 0; k < stages; ++k)
        {
            // y = a x + z; z = x - a y (transposed direct form II of (a + z^-1) / (1 + a z^-1))
            const double out = a * v + z[k];
            z[k] = v - a * out;
            v = out;
        }
        y[i] = (float) v;
    }
}

struct Result
{
    float angle = 0.0f;   // best angle, degrees
    double before = 0.0;  // mean |asymmetry| without rotation, percent
    double after = 0.0;   // mean |asymmetry| at the best angle
    bool valid = false;   // false without signal
};

/** segments: low-band waveforms (same rate); settle: samples skipped at the start of each. */
inline Result search (const std::vector<std::vector<float>>& segments, double rate, double freq, int settle)
{
    Result res;
    std::vector<float> y;
    const auto cost = [&] (double angle)
    {
        double sum = 0.0;
        int count = 0;
        for (const auto& s : segments)
        {
            if ((int) s.size() <= settle + 16)
                continue;
            rotate (s, y, rate, freq, angle);
            sum += std::abs (asymmetry (y.data() + settle, (int) y.size() - settle));
            ++count;
        }
        return count > 0 ? sum / count : -1.0;
    };
    float peak = 0.0f;
    for (const auto& s : segments)
        for (auto v : s)
            peak = std::max (peak, std::abs (v));
    if (peak < 1.0e-5f)
        return res;
    res.before = cost (0.0);
    if (res.before < 0.0)
        return res;
    res.valid = true;
    res.after = res.before;
    for (int a = 5; a < 360; a += 5)
    {
        const double c = cost ((double) a);
        if (c < res.after - 1.0e-9)
        {
            res.after = c;
            res.angle = (float) a;
        }
    }
    return res;
}
} // namespace phaseauto
} // namespace mm::dsp
