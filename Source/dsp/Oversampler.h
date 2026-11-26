#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>

namespace mm::dsp
{
/** Two prepared oversamplers (low and high factor) with integer latency. Switching factor is
    allocation free; the shorter one is padded so the reported latency never changes. */
class DualOversampler
{
public:
    /** factorsLog2: e.g. 1 and 2 for 2x / 4x. */
    void prepare (int lowLog2, int highLog2, int maxBlock)
    {
        for (int i = 0; i < 2; ++i)
        {
            const int f = i == 0 ? lowLog2 : highLog2;
            os[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
                2, (size_t) f, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
            os[(size_t) i]->initProcessing ((size_t) maxBlock);
            lat[(size_t) i] = juce::roundToInt (os[(size_t) i]->getLatencyInSamples());
        }
        maxLatency = std::max (lat[0], lat[1]);
        pad.setSize (2, maxLatency + maxBlock + 1);
        reset();
    }

    void reset()
    {
        for (auto& o : os)
            if (o)
                o->reset();
        pad.clear();
        padPos = 0;
    }

    void setUseHigh (bool high) noexcept { current = high ? 1 : 0; }
    int factor() const noexcept { return (int) os[(size_t) current]->getOversamplingFactor(); }
    int latency() const noexcept { return maxLatency; }

    juce::dsp::AudioBlock<float> up (juce::dsp::AudioBlock<float>& block) noexcept
    {
        return os[(size_t) current]->processSamplesUp (block);
    }

    void down (juce::dsp::AudioBlock<float>& block) noexcept
    {
        os[(size_t) current]->processSamplesDown (block);
        const int extra = maxLatency - lat[(size_t) current];
        if (extra > 0)
            delayBy (block, extra);
    }

private:
    void delayBy (juce::dsp::AudioBlock<float>& block, int extra) noexcept
    {
        const int cap = pad.getNumSamples();
        const int n = (int) block.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            int r = padPos - extra;
            if (r < 0)
                r += cap;
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* line = pad.getWritePointer (ch);
                auto* d = block.getChannelPointer ((size_t) ch);
                line[padPos] = d[i];
                d[i] = line[r];
            }
            if (++padPos == cap)
                padPos = 0;
        }
    }

    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> os;
    std::array<int, 2> lat {};
    int maxLatency = 0, current = 0, padPos = 0;
    juce::AudioBuffer<float> pad;
};
} // namespace mm::dsp
