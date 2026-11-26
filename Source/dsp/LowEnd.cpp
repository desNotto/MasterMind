#include "dsp/LowEnd.h"

namespace mm::dsp
{
void LowEnd::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    const Coeffs band[] = { Coeffs::highpass (fs, 30.0, 0.71), Coeffs::highpass (fs, 30.0, 0.71),
                            Coeffs::lowpass (fs, 90.0, 0.71), Coeffs::lowpass (fs, 90.0, 0.71) };
    trBand.setSections (band, 4);
    const Coeffs hp[] = { Coeffs::highpass (fs, 55.0, 0.71), Coeffs::highpass (fs, 55.0, 0.71) };
    trHighpass.setSections (hp, 2);
    trEnv.setup (fs, 5.0, 80.0);
    reset();
}

void LowEnd::reset()
{
    monoFreq.reset (settings.monoFreq);
    monoAmount.reset (settings.monoAmount);
    monoSlope = -1;
    updateMono (settings.monoFreq);
    sideSplit.reset();
    midAllpass.reset();
    rotFreq.reset (settings.rotFreq);
    rotAngle.reset (settings.rotAngle);
    updateRotator (settings.rotFreq, settings.rotAngle);
    for (auto& ch : rot)
        for (auto& s : ch)
            s.reset();
    rotWasOn = settings.rotOn;
    trBand.reset();
    trHighpass.reset();
    trEnv.reset();
    trAmount.reset (settings.trAmount);
    trMeter.set (-100.0f);
}

void LowEnd::updateMono (double f) noexcept
{
    static constexpr int orders[] = { 2, 4, 8 };
    monoSlope = std::clamp (settings.monoSlope, 0, 2);
    sideSplit.setup (orders[monoSlope], fs, f);
    sideSplit.makeAllpass (midAllpass);
}

void LowEnd::updateRotator (double f, double angleDeg) noexcept
{
    // First-order allpass H = (a + z^-1) / (1 + a z^-1) with phase -phi at f, solved for a directly so
    // that a goes continuously to 1 (identity) as the angle goes to 0 (no clamping, no clicks).
    const double phi = std::clamp (angleDeg, 0.0, 360.0) * pi / 180.0 / rotStages;
    const double w = 2.0 * pi * std::clamp (f, 10.0, fs * 0.45) / fs;
    const double r = std::tan (phi * 0.5) / std::tan (w * 0.5);
    const double a = (1.0 - r) / (1.0 + r);
    Coeffs c;
    c.b0 = a;
    c.b1 = 1.0;
    c.b2 = 0.0;
    c.a1 = a;
    c.a2 = 0.0;
    for (auto& ch : rot)
        for (auto& s : ch)
            s.setCoeffs (c);
}

void LowEnd::process (float* left, float* right, int numSamples) noexcept
{
    const int ramp = (int) (0.05 * fs);
    monoFreq.setTarget (settings.monoFreq, ramp);
    monoAmount.setTarget (settings.monoOn ? settings.monoAmount : 0.0, ramp);
    rotFreq.setTarget (settings.rotFreq, ramp);
    rotAngle.setTarget (settings.rotAngle, ramp / controlBlock + 1);
    trAmount.setTarget (settings.trOn ? settings.trAmount : 0.0, ramp);
    if (settings.monoSlope != monoSlope)
        updateMono (monoFreq.value());
    if (settings.rotOn && ! rotWasOn)
        for (auto& ch : rot)
            for (auto& s : ch)
                s.reset();
    rotWasOn = settings.rotOn;

    double harmPeak = 0.0, lowPeak = 0.0;
    for (int start = 0; start < numSamples; start += controlBlock)
    {
        const int end = std::min (numSamples, start + controlBlock);
        if (monoFreq.isRamping())
            updateMono (monoFreq.next());
        const bool rotRamping = rotFreq.isRamping() || rotAngle.isRamping();
        const double rf = rotFreq.next(), ra = rotAngle.next();
        if (rotRamping && settings.rotOn)
            updateRotator (rf, ra);

        for (int i = start; i < end; ++i)
        {
            double m = 0.5 * (left[i] + right[i]);
            double s = 0.5 * (left[i] - right[i]);

            // Mono maker
            const double amount = monoAmount.next();
            double sl, sh;
            sideSplit.process (s, sl, sh);
            m = midAllpass.process (m);
            s = sh + (1.0 - amount) * sl;

            // Phase rotator (full band allpass on mid and side)
            if (settings.rotOn)
                for (int k = 0; k < rotStages; ++k)
                {
                    m = rot[0][(size_t) k].process (m);
                    s = rot[1][(size_t) k].process (s);
                }

            // Low-End Translator: 2nd and 3rd harmonics of the 30-90 Hz band (Chebyshev shaping)
            const double tra = trAmount.next();
            if (tra > 0.0)
            {
                const double x = trBand.process (m);
                const double env = trEnv.process (std::abs (x)) + 1.0e-6;
                const double u = std::clamp (x / env, -1.0, 1.0);
                const double h2 = (2.0 * u * u - 1.0) * env;
                const double h3 = (4.0 * u * u * u - 3.0 * u) * env;
                const double harm = (1.0 - settings.trMix) * h2 + settings.trMix * h3;
                // less harmonic content on loud passages (env around -12 dBFS and above)
                const double levelScale = 1.0 / (1.0 + env * 4.0);
                const double out = trHighpass.process (harm) * tra * 0.8 * levelScale;
                m += out;
                harmPeak = std::max (harmPeak, std::abs (out));
                lowPeak = std::max (lowPeak, std::abs (x));
            }

            left[i] = (float) (m + s);
            right[i] = (float) (m - s);
        }
    }
    trMeter.set (lowPeak > 0.0 ? (float) gainToDb (harmPeak / lowPeak) : -100.0f);
}
} // namespace mm::dsp
