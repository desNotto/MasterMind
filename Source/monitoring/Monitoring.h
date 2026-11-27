#pragma once

#include "dsp/Filters.h"
#include "monitoring/Profiles.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace mm::monitoring
{
/** Prepared reference audio at the plugin sample rate, plus its short-term loudness curve. */
struct ReferenceAudio
{
    juce::AudioBuffer<float> audio;
    std::vector<float> shortTerm; // LUFS every 100 ms
    double integrated = -100.0;
    juce::String name;
    juce::File file;
    int slot = -1; // -1: library preview (References tab)
    /** Waveform overview for the player: peak and RMS of both channels in waveBins equal bins. */
    static constexpr int waveBins = 1200;
    std::vector<float> wavePeak, waveRms;
    void computeOverview();
};

/** Lock-free hand-over of an object from the message thread to the audio thread: the writer
    publishes a new object, the audio thread adopts it at the start of a block, and the old one is
    handed back for deletion on the message thread. */
template <typename T>
class Exchange
{
public:
    ~Exchange()
    {
        delete pending.exchange (nullptr);
        collect();
        delete current;
    }
    void publish (std::unique_ptr<T> next)
    {
        delete pending.exchange (next.release());
        collect();
    }
    /** Audio thread: adopts a pending object. The previous one goes to a fixed retire list
        (never freed here); the message thread frees it in collect(). */
    T* get() noexcept
    {
        if (T* p = pending.exchange (nullptr))
        {
            T* old = current;
            current = p;
            if (old != nullptr)
                for (auto& slot : retired)
                {
                    T* expected = nullptr;
                    if (slot.compare_exchange_strong (expected, old))
                        break;
                }
        }
        return current;
    }
    /** Message thread: frees retired objects. */
    void collect()
    {
        for (auto& slot : retired)
            delete slot.exchange (nullptr);
    }

private:
    std::atomic<T*> pending { nullptr };
    std::array<std::atomic<T*>, 8> retired {};
    T* current = nullptr;
};

/** Monitoring section (spec section 6). Changes only what the user hears; bypassed completely when
    the host renders offline. */
class Monitoring
{
public:
    enum class Listen
    {
        stereo,
        mono,
        midOnly,
        sideOnly,
        leftOnly,
        rightOnly
    };

    enum class RefMatch
    {
        autoSection,
        autoWhole,
        manual
    };

    /** Filter set for one correction slot or emulation, built on the message thread. */
    struct FilterSet
    {
        std::vector<dsp::Coeffs> sections;
        double preampGain = 1.0;
        bool mono = false, softLimit = false;
        bool useConvolution = false;
    };

    Monitoring();

    void prepare (double sampleRate, int maxBlock);

    /** Audio thread. `delayedInput` is the chain input delayed by the chain latency (for bypass and
        delta listen). Returns true if any monitoring feature changed the audio. */
    bool process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& delayedInput, bool nonRealtime) noexcept;

    /** True while any monitoring feature is active (MONITOR ON banner). */
    bool isActive() const noexcept;

    // ---- Settings (message thread) ---------------------------------------------------------------
    std::atomic<int> correctionSlot { 0 };   // 0 Off, 1 Headphones, 2 Speakers
    std::atomic<int> systemIndex { -1 };     // -1 Off
    std::atomic<int> listenMode { 0 };       // Listen
    std::atomic<int> bandSolo { -1 };        // -1 off, 0..5 tonal band
    std::atomic<bool> deltaListen { false };
    std::atomic<bool> chainBypass { false }, matchOn { true };
    std::atomic<bool> referenceActive { false }; // B
    std::atomic<bool> refPlaying { false };
    std::atomic<int> refMatchMode { 0 };
    std::atomic<float> refManualGainDb { 0.0f };
    std::atomic<double> refLoopStart { 0.0 }, refLoopEnd { 0.0 }; // seconds, end <= start: whole file
    std::atomic<double> seekRequest { -1.0 };

    // Loudness published by the analysis engine (LUFS)
    std::atomic<float> inShortTerm { -100.0f }, outShortTerm { -100.0f }, outIntegrated { -100.0f };

    // Read back for the UI
    std::atomic<float> appliedMatchDb { 0.0f }, appliedRefGainDb { 0.0f };
    std::atomic<double> refPositionSeconds { 0.0 }, refLengthSeconds { 0.0 };

    /** Publishes filter sets (message thread). */
    void setCorrection (int slot, std::unique_ptr<FilterSet> set);
    void setCorrectionImpulse (int slot, juce::AudioBuffer<float> ir, double irSampleRate);
    void setSystem (std::unique_ptr<FilterSet> set);
    void setReference (std::unique_ptr<ReferenceAudio> ref);
    const ReferenceAudio* currentReferenceForUi() const noexcept { return refShadow.load(); }
    void collectGarbage();

    double getSampleRate() const noexcept { return fs; }

    /** Builds a filter set from a parametric profile with the spec 6.1 safety rules. */
    static std::unique_ptr<FilterSet> buildCorrection (const ParametricProfile& p, double fs, double strength,
                                                       double boostLimitDb, bool cutOnlyBelow300);
    static std::unique_ptr<FilterSet> buildSystem (const SystemPreset& p, double fs);

private:
    struct Chain
    {
        std::array<dsp::Cascade, 2> cascades; // up to 8 sections each; more are chained below
        std::array<dsp::Cascade, 2> cascades2;
        const FilterSet* applied = nullptr;
        void apply (const FilterSet* set);
        void process (float* l, float* r, int n) noexcept;
    };

    double fs = 48000.0;
    int maxBlock = 512;
    Exchange<FilterSet> correction[2];
    Exchange<FilterSet> system;
    Exchange<ReferenceAudio> reference;
    std::atomic<const ReferenceAudio*> refShadow { nullptr };
    Chain correctionChain[2], systemChain;
    std::array<juce::dsp::Convolution, 2> irConv;
    std::array<std::atomic<bool>, 2> irLoaded { false, false };
    std::array<dsp::LinkwitzRiley, 2> bandLo, bandHi; // band listen per channel
    int bandForFilters = -2;
    double matchGain = 1.0, refGain = 1.0, smoothCoeff = 0.0;
    double refPos = 0.0;
    const ReferenceAudio* lastRef = nullptr;
    juce::AudioBuffer<float> scratch;
};
} // namespace mm::monitoring
