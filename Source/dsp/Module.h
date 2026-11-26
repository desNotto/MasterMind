#pragma once

#include "dsp/Dynamics.h"
#include "dsp/Filters.h"

#include <atomic>

namespace mm::dsp
{
struct Context
{
    double sampleRate = 48000.0;
    int maxBlock = 512;
};

/** Common interface of the ten processing modules. Settings are plain structs written by the
    chain before each block; process() is real-time safe. */
class Module
{
public:
    virtual ~Module() = default;
    virtual void prepare (const Context& ctx) = 0;
    virtual void reset() = 0;
    virtual void process (float* left, float* right, int numSamples) noexcept = 0;

    /** Processing latency in samples for the current structural settings. */
    virtual int latency() const noexcept { return 0; }
};

/** Sub-block length for coefficient updates of smoothly changing filters. */
inline constexpr int controlBlock = 32;

/** Relaxed atomic float for meters read by the UI. */
struct Meter
{
    std::atomic<float> value { 0.0f };
    void set (float v) noexcept { value.store (v, std::memory_order_relaxed); }
    float get() const noexcept { return value.load (std::memory_order_relaxed); }
};

/** Converts a log-skewed frequency change into a smooth per-control-block ramp. */
class FreqRamp
{
public:
    void reset (double hz) noexcept { r.reset (std::log (hz)); }
    void setTarget (double hz, int samples) noexcept { r.setTarget (std::log (hz), samples / controlBlock + 1); }
    double next() noexcept { return std::exp (r.next()); }
    bool isRamping() const noexcept { return r.isRamping(); }
    double value() const noexcept { return std::exp (r.value()); }

private:
    Ramp r;
};
} // namespace mm::dsp
