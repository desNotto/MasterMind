#include "dsp/BusComp.h"

namespace mm::dsp
{
double BusComp::makeupDb (const Settings& s)
{
    if (! s.autoMakeup)
        return 0.0;
    // half of the static reduction of a signal 6 dB above threshold
    return std::clamp (-0.5 * compressorGainDb (s.thresholdDb + 6.0, s.thresholdDb, s.ratio, s.hardKnee ? 0.0 : 6.0)
                           - 0.25 * s.thresholdDb * (1.0 - 1.0 / s.ratio),
                       0.0, 12.0);
}

double BusComp::staticCurveDb (const Settings& s, double inDb)
{
    return inDb + compressorGainDb (inDb, s.thresholdDb, s.ratio, s.hardKnee ? 0.0 : 6.0) + makeupDb (s);
}

void BusComp::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    for (int ch = 0; ch < 2; ++ch)
    {
        det[(size_t) ch].prepare (fs);
        smooth[(size_t) ch].prepare (fs);
    }
    reset();
}

void BusComp::reset()
{
    scFreq.reset (settings.scHpf);
    for (int ch = 0; ch < 2; ++ch)
    {
        sc[(size_t) ch].setCoeffs (Coeffs::highpass (fs, settings.scHpf, 0.71));
        sc[(size_t) ch].reset();
        det[(size_t) ch].reset();
        smooth[(size_t) ch].reset();
        fbOut[(size_t) ch] = 0.0;
    }
    mix.reset (settings.mix);
    makeup.reset (dbToGain (makeupDb (settings)));
    grMeter.set (0.0f);
}

void BusComp::process (float* left, float* right, int numSamples) noexcept
{
    const bool punch = settings.model == 1;
    const double attack = punch ? settings.attackMs * 0.5 : settings.attackMs;
    for (auto& s : smooth)
        s.setTimes (attack, punch ? settings.releaseMs * 0.7 : settings.releaseMs, settings.releaseAuto);
    scFreq.setTarget (settings.scHpf, (int) (0.05 * fs));
    mix.setTarget (settings.mix, 1200);
    makeup.setTarget (dbToGain (makeupDb (settings)), 2400);
    const double knee = settings.hardKnee ? 0.0 : 6.0;
    const double link = settings.link;

    float maxGr = 0.0f;
    double maxLevel = -100.0;
    float* io[2] = { left, right };
    for (int start = 0; start < numSamples; start += controlBlock)
    {
        const int end = std::min (numSamples, start + controlBlock);
        if (scFreq.isRamping())
        {
            const auto c = Coeffs::highpass (fs, scFreq.next(), 0.71);
            for (auto& s : sc)
                s.setCoeffs (c);
        }
        for (int i = start; i < end; ++i)
        {
            double lvl[2];
            for (int ch = 0; ch < 2; ++ch)
            {
                // Punch: detector listens to the compressed output (feedback); VCA: to the input
                const double src = punch ? fbOut[(size_t) ch] : (double) io[ch][i];
                lvl[ch] = det[(size_t) ch].process (sc[(size_t) ch].process (src), settings.rmsAmount);
            }
            const double linked = std::max (lvl[0], lvl[1]);
            maxLevel = std::max (maxLevel, linked);
            const double mk = makeup.next(), mx = mix.next();
            double grMin = 0.0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const double l = link * linked + (1.0 - link) * lvl[ch];
                const double target = compressorGainDb (l, settings.thresholdDb, punch ? settings.ratio * 1.2 : settings.ratio, knee);
                const double gr = smooth[(size_t) ch].process (target);
                grMin = std::min (grMin, gr);
                const double x = io[ch][i];
                const double wet = x * dbToGain (gr) * mk;
                fbOut[(size_t) ch] = wet;
                io[ch][i] = (float) (x + mx * (wet - x));
            }
            maxGr = std::min (maxGr, (float) grMin);
        }
    }
    grMeter.set (maxGr);
    levelMeter.set ((float) maxLevel);
}
} // namespace mm::dsp
