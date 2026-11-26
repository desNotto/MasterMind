#pragma once

#include <juce_core/juce_core.h>

#include <atomic>

namespace mm::dsp
{
/** Measures the share of the available block time spent in processBlock.
    Written only by the audio thread, read by the UI through an atomic.
    Uses no locks and no allocation. */
class LoadMeter
{
public:
    void prepare (double sampleRate) noexcept
    {
        secondsPerSample = sampleRate > 0.0 ? 1.0 / sampleRate : 1.0 / 44100.0;
        load.store (0.0f, std::memory_order_relaxed);
    }

    /** Proportion of real time used, smoothed. 1.0 means 100 percent. */
    float getLoadProportion() const noexcept { return load.load (std::memory_order_relaxed); }

    class Scope
    {
    public:
        Scope (LoadMeter& meterToUse, int numSamplesInBlock) noexcept
            : meter (meterToUse), numSamples (numSamplesInBlock), start (juce::Time::getHighResolutionTicks())
        {
        }

        ~Scope() { meter.registerBlock (juce::Time::getHighResolutionTicks() - start, numSamples); }

        Scope (const Scope&) = delete;
        Scope& operator= (const Scope&) = delete;

    private:
        LoadMeter& meter;
        int numSamples;
        juce::int64 start;
    };

private:
    void registerBlock (juce::int64 elapsedTicks, int numSamples) noexcept
    {
        if (numSamples <= 0)
            return;

        const double used = juce::Time::highResolutionTicksToSeconds (elapsedTicks);
        const double available = numSamples * secondsPerSample;
        const auto instant = static_cast<float> (used / available);
        const float previous = load.load (std::memory_order_relaxed);
        load.store (previous + 0.05f * (instant - previous), std::memory_order_relaxed);
    }

    std::atomic<float> load { 0.0f };
    double secondsPerSample = 1.0 / 44100.0;
};
} // namespace mm::dsp
