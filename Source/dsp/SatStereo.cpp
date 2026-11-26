#include "dsp/SatStereo.h"

namespace mm::dsp
{
// ---- Saturation --------------------------------------------------------------------------------------
double Saturation::shape (int type, double x, double bias) noexcept
{
    switch (type)
    {
        case 0: // Tube: asymmetric tanh, offset removed
            return std::tanh (x + bias) - std::tanh (bias);
        case 1: // Tape: arctangent, softer knee
            return (2.0 / pi) * std::atan ((pi / 2.0) * (x + bias * 0.5)) - (2.0 / pi) * std::atan ((pi / 2.0) * bias * 0.5);
        case 2: // Transformer: cubic soft clip with gentle even harmonics from bias
        {
            const double v = std::clamp (x + bias * 0.5, -1.5, 1.5);
            const double b0 = std::clamp (bias * 0.5, -1.5, 1.5);
            return (v - v * v * v * (4.0 / 27.0)) - (b0 - b0 * b0 * b0 * (4.0 / 27.0));
        }
        default: // Diode: hard-ish positive, soft negative
        {
            const double v = x + bias;
            const double y = v >= 0.0 ? (1.0 - std::exp (-v)) : -(1.0 - std::exp (v * 0.6)) / 0.6;
            const double y0 = bias >= 0.0 ? (1.0 - std::exp (-bias)) : -(1.0 - std::exp (bias * 0.6)) / 0.6;
            return y - y0;
        }
    }
}

void Saturation::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    maxBlock = ctx.maxBlock;
    os.prepare (1, 2, maxBlock); // 2x normal, 4x high
    reset();
}

void Saturation::reset()
{
    os.reset();
    lastRate = 0.0;
    for (auto& c : ch)
    {
        c.xo1.reset();
        c.xo2.reset();
        c.ap2.reset();
        c.dc.reset();
    }
    for (int b = 0; b < 3; ++b)
    {
        const auto& s = settings.bands[(size_t) b];
        ramps[(size_t) b].drive.reset (dbToGain (s.driveDb));
        ramps[(size_t) b].mix.reset (s.mix);
        ramps[(size_t) b].out.reset (dbToGain (s.outDb));
        ramps[(size_t) b].bias.reset (s.bias);
    }
}

void Saturation::updateSplit (double rate) noexcept
{
    const double f1 = std::clamp ((double) settings.xo1, 40.0, 600.0);
    const double f2 = std::clamp ((double) settings.xo2, f1 * 2.0, std::min (12000.0, fs * 0.45));
    for (auto& c : ch)
    {
        c.xo1.setup (4, rate, f1);
        c.xo2.setup (4, rate, f2);
        c.xo2.makeAllpass (c.ap2);
        c.dc.setCoeffs (Coeffs::highpass1 (rate, 5.0));
    }
    lastRate = rate;
}

void Saturation::process (float* left, float* right, int numSamples) noexcept
{
    float* chans[2] = { left, right };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) numSamples);
    auto upBlock = os.up (block);
    const double rate = fs * os.factor();
    if (rate != lastRate)
        updateSplit (rate);

    const int n = (int) upBlock.getNumSamples();
    double peaks[3] = { 0.0, 0.0, 0.0 };
    for (int b = 0; b < 3; ++b)
    {
        const auto& s = settings.bands[(size_t) b];
        // drive and output are compensated so that a low-level signal keeps unity gain at mix 100 %
        ramps[(size_t) b].drive.setTarget (dbToGain (s.driveDb), n);
        ramps[(size_t) b].mix.setTarget (s.mix, n);
        ramps[(size_t) b].out.setTarget (dbToGain (s.outDb), n);
        ramps[(size_t) b].bias.setTarget (s.bias * 0.5, n);
    }

    for (int i = 0; i < n; ++i)
    {
        double drive[3], mixv[3], out[3], bias[3];
        for (int b = 0; b < 3; ++b)
        {
            drive[b] = ramps[(size_t) b].drive.next();
            mixv[b] = ramps[(size_t) b].mix.next();
            out[b] = ramps[(size_t) b].out.next();
            bias[b] = ramps[(size_t) b].bias.next();
        }
        for (int c = 0; c < 2; ++c)
        {
            auto& st = ch[(size_t) c];
            float* d = upBlock.getChannelPointer ((size_t) c);
            double lo, rest, mid, hi;
            st.xo1.process (d[i], lo, rest);
            st.xo2.process (rest, mid, hi);
            lo = st.ap2.process (lo);
            const double bands[3] = { lo, mid, hi };
            double sum = 0.0;
            for (int b = 0; b < 3; ++b)
            {
                const int type = settings.bands[(size_t) b].type;
                const double x = bands[b];
                peaks[b] = std::max (peaks[b], std::abs (x));
                // drive into the shaper, then divide by the drive so the level stays comparable
                const double wet = shape (type, x * drive[b], bias[b]) / drive[b];
                sum += (x + mixv[b] * (wet - x)) * out[b];
            }
            d[i] = (float) st.dc.process (sum);
        }
    }
    os.down (block);
    for (int b = 0; b < 3; ++b)
        peakMeters[(size_t) b].set ((float) peaks[b]);
}

// ---- Stereo ------------------------------------------------------------------------------------------
void StereoStage::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    corrCoeff = Envelope::coeff (fs, 300.0);
    guardCoeff = Envelope::coeff (fs, 150.0);
    reset();
}

void StereoStage::updateSplits (double lo, double hi) noexcept
{
    hi = std::max (hi, lo * 2.0);
    for (auto& s : split)
    {
        s.lo.setup (4, fs, lo);
        s.hi.setup (4, fs, hi);
        s.hi.makeAllpass (s.ap);
    }
}

void StereoStage::reset()
{
    lowFreq.reset (settings.lowSplit);
    highFreq.reset (settings.split);
    updateSplits (settings.lowSplit, settings.split);
    for (auto& s : split)
    {
        s.lo.reset();
        s.hi.reset();
        s.ap.reset();
    }
    midW.reset (settings.midWidth);
    highW.reset (settings.highWidth);
    bal.reset (settings.balanceDb);
    corrLR.fill (0.0);
    corrLL.fill (0.0);
    corrRR.fill (0.0);
    guard = { 1.0, 1.0, 1.0 };
    for (auto& m : guardMeters)
        m.set (1.0f);
}

void StereoStage::process (float* left, float* right, int numSamples) noexcept
{
    const int ramp = (int) (0.05 * fs);
    lowFreq.setTarget (settings.lowSplit, ramp);
    highFreq.setTarget (settings.split, ramp);
    midW.setTarget (settings.midWidth, ramp);
    highW.setTarget (settings.highWidth, ramp);
    bal.setTarget (settings.balanceDb, ramp);

    for (int start = 0; start < numSamples; start += controlBlock)
    {
        const int end = std::min (numSamples, start + controlBlock);
        if (lowFreq.isRamping() || highFreq.isRamping())
            updateSplits (lowFreq.next(), highFreq.next());

        for (int i = start; i < end; ++i)
        {
            const double m = 0.5 * (left[i] + right[i]);
            const double s = 0.5 * (left[i] - right[i]);
            double mb[3], sb[3];
            for (int k = 0; k < 2; ++k)
            {
                auto& sp = split[(size_t) k];
                double lo, rest, mid, hi;
                sp.lo.process (k == 0 ? m : s, lo, rest);
                sp.hi.process (rest, mid, hi);
                lo = sp.ap.process (lo);
                double* out = k == 0 ? mb : sb;
                out[0] = lo;
                out[1] = mid;
                out[2] = hi;
            }

            const double widths[3] = { 1.0, midW.next(), highW.next() };
            const double balance = bal.next();
            const double gm = dbToGain (-balance * 0.5), gs = dbToGain (balance * 0.5);
            double mo = 0.0, so = 0.0;
            for (int b = 0; b < 3; ++b)
            {
                if (settings.guardOn && b > 0)
                {
                    const double l = mb[b] + sb[b], r = mb[b] - sb[b];
                    corrLR[(size_t) b] = l * r + corrCoeff * (corrLR[(size_t) b] - l * r);
                    corrLL[(size_t) b] = l * l + corrCoeff * (corrLL[(size_t) b] - l * l);
                    corrRR[(size_t) b] = r * r + corrCoeff * (corrRR[(size_t) b] - r * r);
                    const double denom = std::sqrt (corrLL[(size_t) b] * corrRR[(size_t) b]);
                    const double corr = denom > 1.0e-12 ? corrLR[(size_t) b] / denom : 1.0;
                    const double target = std::clamp (1.0 - (settings.guardThreshold - corr) / 0.5, 0.0, 1.0);
                    guard[(size_t) b] = target + guardCoeff * (guard[(size_t) b] - target);
                }
                else
                {
                    guard[(size_t) b] = 1.0;
                }
                mo += mb[b];
                so += sb[b] * widths[b] * guard[(size_t) b];
            }
            mo *= gm;
            so *= gs;
            left[i] = (float) (mo + so);
            right[i] = (float) (mo - so);
        }
    }
    for (int b = 0; b < 3; ++b)
        guardMeters[(size_t) b].set ((float) guard[(size_t) b]);
    double corrMin = 2.0;
    if (settings.guardOn)
        for (int b = 1; b < 3; ++b)
        {
            const double denom = std::sqrt (corrLL[(size_t) b] * corrRR[(size_t) b]);
            corrMin = std::min (corrMin, denom > 1.0e-12 ? corrLR[(size_t) b] / denom : 1.0);
        }
    corrMeter.set ((float) corrMin);
}
} // namespace mm::dsp
