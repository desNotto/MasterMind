#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace mm::dsp
{
/** Returns true if any sample in the buffer is NaN or infinite.
    Works on the bit pattern, so it stays correct under fast-math flags. */
bool containsNonFinite (const juce::AudioBuffer<float>& buffer) noexcept;

/** The processing chain (spec section 4). In M0 it is a passthrough with
    the NaN/Inf input guard. Real-time safe: no allocation, no locks. */
class MasterChain
{
public:
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);
    void reset() noexcept;

    /** Processes the buffer in place. If the input contains NaN or Inf, the
        block is replaced by silence, all states are reset and false is
        returned. Otherwise the audio passes unchanged and true is returned. */
    bool process (juce::AudioBuffer<float>& buffer) noexcept;

    double getSampleRate() const noexcept { return sampleRate; }

private:
    double sampleRate = 44100.0;
    int maxBlockSize = 0;
    int channels = 2;
};
} // namespace mm::dsp
