#pragma once

#include "dsp/MasterChain.h"
#include "state/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <map>

namespace mmtest
{
/** Minimal processor that owns the real parameter layout, so the chain can be tested headless. */
class Host final : public juce::AudioProcessor
{
public:
    Host()
        : AudioProcessor (BusesProperties()
                              .withInput ("In", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "MasterMindState", mm::params::createLayout()),
          chain (apvts)
    {
    }

    void set (const juce::String& id, float plainValue)
    {
        auto* p = apvts.getParameter (id);
        REQUIRE (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    }

    float get (const juce::String& id) { return apvts.getRawParameterValue (id)->load(); }

    // AudioProcessor boilerplate
    const juce::String getName() const override { return "Host"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;
    mm::dsp::MasterChain chain;
};

inline void fillNoise (juce::AudioBuffer<float>& b, juce::Random& r, float amp = 1.0f)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (ch, i, amp * (r.nextFloat() * 2.0f - 1.0f));
}

/** Runs a whole signal through the chain in blocks of the given size (in place). */
inline void runChain (Host& h, juce::AudioBuffer<float>& signal, int blockSize)
{
    for (int start = 0; start < signal.getNumSamples(); start += blockSize)
    {
        const int n = std::min (blockSize, signal.getNumSamples() - start);
        float* chans[2] = { signal.getWritePointer (0) + start, signal.getWritePointer (1) + start };
        juce::AudioBuffer<float> view (chans, 2, n);
        h.chain.process (view);
    }
}
} // namespace mmtest
