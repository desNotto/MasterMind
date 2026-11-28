#pragma once

#include "dsp/BusComp.h"
#include "dsp/ClipLimit.h"
#include "dsp/Inflator.h"
#include "dsp/DynamicEq.h"
#include "dsp/InputOutput.h"
#include "dsp/LowEnd.h"
#include "dsp/Multiband.h"
#include "dsp/SatStereo.h"
#include "state/Parameters.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <memory>

namespace mm::dsp
{
/** True if any sample in the buffer is NaN or +-Inf (checked through the bit pattern, so it works
    with fast-math compiler options). */
bool containsNonFinite (const juce::AudioBuffer<float>& buffer) noexcept;

/** The fixed-order processing chain of section 4. Every module has a latency-compensated bypass,
    so the total latency depends only on structural settings, never on module on/off. */
class MasterChain
{
public:
    enum ModuleId
    {
        input,
        eq,
        multiband,
        lowEnd,
        busComp,
        saturation,
        stereo,
        inflator, // slot 7: the Inflator replaced the Clipper (D-038)
        limiter,
        output,
        numModules
    };

    explicit MasterChain (juce::AudioProcessorValueTreeState& state);
    ~MasterChain();

    void prepare (double sampleRate, int maxBlockSize, bool offline);
    void reset();

    /** Processes in place. Returns false if the input contained NaN or Inf; the buffer is then
        silenced and the internal state reset. Real-time safe. */
    bool process (juce::AudioBuffer<float>& buffer) noexcept;

    /** Total latency for the current structural settings. */
    int latencySamples() const noexcept;

    /** Rebuilds linear-phase kernels when needed. Worker thread only (allocates). */
    void updateLinearPhase();

    bool isModuleOn (int id) const noexcept { return slots[(size_t) id].on; }

    const DynamicEq& getEq() const noexcept { return eqModule; }
    const Multiband& getMultiband() const noexcept { return mbModule; }
    const LowEnd& getLowEnd() const noexcept { return lowEndModule; }
    const BusComp& getBusComp() const noexcept { return busModule; }
    const StereoStage& getStereo() const noexcept { return stereoModule; }
    const Saturation& getSaturation() const noexcept { return satModule; }
    const Inflator& getInflator() const noexcept { return inflModule; }
    const Limiter& getLimiter() const noexcept { return limModule; }
    const InputStage& getInput() const noexcept { return inputModule; }

    double getSampleRate() const noexcept { return fs; }

    /** Sample overs and inter-sample overs before the limiter (spec 5.9), counted on the audio thread. */
    std::atomic<uint64_t> preLimiterSampleOvers { 0 }, preLimiterTpOvers { 0 };
    void resetOvers() noexcept { preLimiterSampleOvers = 0; preLimiterTpOvers = 0; }

    /** Inflator clip reduction of the last block and per-sample limiter gain reduction (dB). */
    float clipperReductionDb() const noexcept { return slots[inflator].on ? inflModule.clipReductionDb() : 0.0f; }
    const float* limiterTrace() const noexcept { return slots[limiter].on ? limModule.lastGainReduction() : nullptr; }

    /** Settings snapshots read from the parameters (any thread). */
    DynamicEq::Settings eqSnapshot() const;
    Multiband::Settings multibandSnapshot() const;

private:
    struct Slot
    {
        Module* module = nullptr;
        std::atomic<float>* onParam = nullptr;
        DelayLine bypass;
        bool on = false;
        double fade = 0.0; // 0 = dry (bypassed), 1 = processed
    };

    void readSettings() noexcept;
    float p (const char* id) const noexcept;
    float p (const juce::String& id) const noexcept;

    juce::AudioProcessorValueTreeState& apvts;
    params::Raw raw;
    struct ParamPtrs;
    std::unique_ptr<ParamPtrs> ptrs;

    InputStage inputModule;
    DynamicEq eqModule;
    Multiband mbModule;
    LowEnd lowEndModule;
    BusComp busModule;
    Saturation satModule;
    StereoStage stereoModule;
    Inflator inflModule;
    Limiter limModule;
    OutputStage outputModule;

    std::array<Slot, numModules> slots;
    std::array<TruePeakDetector, 2> preLimTp;
    juce::AudioBuffer<float> dry;
    double fs = 48000.0;
    int maxBlock = 512;
    bool offlineMode = false;
    double fadeStep = 0.001;

    juce::CriticalSection buildLock;
    juce::String lastEqKey, lastMbKey;
};
} // namespace mm::dsp
