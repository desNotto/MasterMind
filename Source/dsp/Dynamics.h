#pragma once

#include "dsp/Filters.h"

#include <algorithm>
#include <cmath>

namespace mm::dsp
{
/** Static gain computer with a soft knee (log domain). Returns gain change in dB (<= 0). */
inline double compressorGainDb (double levelDb, double thresholdDb, double ratio, double kneeDb) noexcept
{
    const double over = levelDb - thresholdDb;
    const double slope = 1.0 / std::max (1.0, ratio) - 1.0;
    if (kneeDb > 0.0 && std::abs (over) <= kneeDb * 0.5)
    {
        const double t = over + kneeDb * 0.5;
        return slope * t * t / (2.0 * kneeDb);
    }
    return over > 0.0 ? slope * over : 0.0;
}

/** Level detector with a peak/RMS blend, working in dB. */
class LevelDetector
{
public:
    void prepare (double fs) noexcept
    {
        rmsCoeff = Envelope::coeff (fs, 10.0);
        reset();
    }

    void reset() noexcept { meanSquare = 0.0; }

    /** rmsAmount: 0 = peak, 1 = RMS. Returns level in dB. */
    inline double process (double x, double rmsAmount) noexcept
    {
        const double sq = x * x;
        meanSquare = sq + rmsCoeff * (meanSquare - sq);
        const double peak = std::abs (x);
        const double rms = std::sqrt (meanSquare);
        return gainToDb (peak + rmsAmount * (rms - peak) + 1.0e-9);
    }

private:
    double rmsCoeff = 0.0, meanSquare = 0.0;
};

/** Smooths a gain change (dB, <= 0) with attack/release; optional program-dependent release. */
class GainSmoother
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = sampleRate;
        avgCoeff = Envelope::coeff (fs, 1000.0);
        reset();
    }

    void setTimes (double attackMs, double releaseMs, bool autoRelease) noexcept
    {
        att = Envelope::coeff (fs, attackMs);
        relMs = releaseMs;
        autoRel = autoRelease;
        if (! autoRel)
            rel = Envelope::coeff (fs, releaseMs);
    }

    void reset() noexcept { state = 0.0; avg = 0.0; counter = 0; }

    inline double process (double targetDb) noexcept
    {
        if (autoRel && (counter++ & 15) == 0)
        {
            // longer release when the reduction has been deep for a while (dual-stage behaviour)
            const double ms = std::clamp (60.0 + 120.0 * (-avg), 60.0, 1200.0);
            rel = Envelope::coeff (fs, ms);
        }
        const double c = targetDb < state ? att : rel;
        state = targetDb + c * (state - targetDb);
        avg = state + avgCoeff * (avg - state);
        return state;
    }

    double value() const noexcept { return state; }

private:
    double fs = 48000.0, att = 0.0, rel = 0.0, relMs = 100.0, state = 0.0, avg = 0.0, avgCoeff = 0.0;
    bool autoRel = false;
    unsigned counter = 0;
};

/** Linear interpolation ramp for a parameter over one block (no allocation). */
class Ramp
{
public:
    void reset (double v) noexcept { current = target = v; step = 0.0; remaining = 0; }

    void setTarget (double v, int samples) noexcept
    {
        if (v == target && remaining == 0)
            return;
        target = v;
        remaining = std::max (1, samples);
        step = (target - current) / remaining;
    }

    inline double next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0)
                current = target;
        }
        return current;
    }

    bool isRamping() const noexcept { return remaining > 0; }
    double value() const noexcept { return current; }
    double targetValue() const noexcept { return target; }

private:
    double current = 0.0, target = 0.0, step = 0.0;
    int remaining = 0;
};
} // namespace mm::dsp
