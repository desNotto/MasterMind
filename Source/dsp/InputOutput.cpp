#include "dsp/InputOutput.h"

namespace mm::dsp
{
// ---- Input ------------------------------------------------------------------------------------------
void InputStage::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    dcR = std::exp (-2.0 * pi * 5.0 / fs); // first-order DC blocker at 5 Hz
    reset();
}

void InputStage::reset()
{
    gain.reset (dbToGain (settings.trimDb));
    freq.reset (settings.hpfFreq);
    slope = -1;
    updateFilters (settings.hpfFreq);
    for (auto& c : hpf)
        c.reset();
    dcX.fill (0.0);
    dcY.fill (0.0);
    hpfWasOn = settings.hpfOn;
}

void InputStage::updateFilters (double f) noexcept
{
    static constexpr int orders[] = { 2, 4, 8 };
    const int order = orders[std::clamp (settings.hpfSlope, 0, 2)];
    Coeffs sections[Cascade::maxSections];
    int n = 0;
    for (int k = 0; k < order / 2; ++k)
        sections[n++] = Coeffs::highpass (fs, f, 1.0 / (2.0 * std::cos ((2.0 * k + 1.0) * pi / (2.0 * order))));
    for (auto& c : hpf)
        c.setSections (sections, n);
    slope = settings.hpfSlope;
}

double InputStage::hpfMagnitudeDb (double f) const
{
    if (! settings.hpfOn)
        return 0.0;
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (hpf[0].response (fs, f))));
}

void InputStage::process (float* left, float* right, int numSamples) noexcept
{
    gain.setTarget (dbToGain (settings.trimDb), (int) (0.02 * fs));
    const double pl = settings.polarityL ? -1.0 : 1.0, pr = settings.polarityR ? -1.0 : 1.0;

    // Trim and polarity. A unity gain with no ramp is skipped so the default state is bit-exact.
    if (gain.isRamping() || gain.value() != 1.0 || pl < 0.0 || pr < 0.0)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const double g = gain.next();
            left[i] = (float) (left[i] * g * pl);
            right[i] = (float) (right[i] * g * pr);
        }
    }

    if (! settings.hpfOn)
    {
        hpfWasOn = false;
        return;
    }
    if (! hpfWasOn)
    {
        for (auto& c : hpf)
            c.reset();
        dcX.fill (0.0);
        dcY.fill (0.0);
        freq.reset (settings.hpfFreq);
        updateFilters (settings.hpfFreq);
        hpfWasOn = true;
    }

    freq.setTarget (settings.hpfFreq, (int) (0.05 * fs));
    if (slope != settings.hpfSlope)
        updateFilters (freq.value());

    float* io[2] = { left, right };
    for (int start = 0; start < numSamples; start += controlBlock)
    {
        if (freq.isRamping())
            updateFilters (freq.next());
        const int end = std::min (numSamples, start + controlBlock);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = hpf[(size_t) ch];
            double& x1 = dcX[(size_t) ch];
            double& y1 = dcY[(size_t) ch];
            float* d = io[ch];
            for (int i = start; i < end; ++i)
            {
                const double x = d[i];
                const double y = x - x1 + dcR * y1; // DC blocker
                x1 = x;
                y1 = y;
                d[i] = (float) c.process (y);
            }
        }
    }
}

// ---- Output -----------------------------------------------------------------------------------------
void OutputStage::prepare (const Context& ctx)
{
    (void) ctx;
    reset();
}

void OutputStage::reset()
{
    gain.reset (dbToGain (settings.gainDb));
    for (auto& e : err)
        e.fill (0.0);
}

void OutputStage::process (float* left, float* right, int numSamples) noexcept
{
    gain.setTarget (dbToGain (settings.gainDb), 960);
    if (gain.isRamping() || gain.value() != 1.0)
        for (int i = 0; i < numSamples; ++i)
        {
            const double g = gain.next();
            left[i] = (float) (left[i] * g);
            right[i] = (float) (right[i] * g);
        }

    if (settings.dither == 0)
        return;

    const double levels = settings.bits == 0 ? 32768.0 : 8388608.0;
    const double lsb = 1.0 / levels;
    std::uniform_real_distribution<double> uni (-0.5, 0.5);
    float* io[2] = { left, right };
    for (int ch = 0; ch < 2; ++ch)
    {
        auto& e = err[(size_t) ch];
        float* d = io[ch];
        for (int i = 0; i < numSamples; ++i)
        {
            double x = d[i];
            if (settings.dither == 2)
                x -= 2.0 * e[0] - e[1]; // second-order error feedback, (1 - z^-1)^2 noise shaping
            const double tpdf = (uni (rng) + uni (rng)) * lsb;
            const double q = std::round ((x + tpdf) * levels) / levels;
            if (settings.dither == 2)
            {
                e[1] = e[0];
                e[0] = std::clamp (q - x, -4.0 * lsb, 4.0 * lsb);
            }
            d[i] = (float) std::clamp (q, -1.0, 1.0 - lsb);
        }
    }
}
} // namespace mm::dsp
