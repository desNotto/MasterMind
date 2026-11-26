#include "dsp/MasterChain.h"

#include <bit>
#include <cstdint>

namespace mm::dsp
{
namespace
{
constexpr std::uint32_t exponentMask = 0x7f800000u;

inline bool isNonFinite (float x) noexcept
{
    return (std::bit_cast<std::uint32_t> (x) & exponentMask) == exponentMask;
}
} // namespace

bool containsNonFinite (const juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* data = buffer.getReadPointer (ch);

        for (int i = 0; i < numSamples; ++i)
            if (isNonFinite (data[i]))
                return true;
    }

    return false;
}

void MasterChain::prepare (double newSampleRate, int maximumBlockSize, int numChannels)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxBlockSize = maximumBlockSize;
    channels = numChannels;
    reset();
}

void MasterChain::reset() noexcept
{
    // No internal state yet. Filters and dynamics added in later milestones
    // reset their state here.
}

bool MasterChain::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (containsNonFinite (buffer))
    {
        buffer.clear();
        reset();
        return false;
    }

    // M0: bit-exact passthrough. The audio is left untouched.
    return true;
}
} // namespace mm::dsp
