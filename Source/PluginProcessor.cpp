#include "PluginProcessor.h"

#include "ui/PluginEditor.h"

namespace
{
const juce::Identifier stateType { "MasterMindState" };
const juce::Identifier stateVersionKey { "stateVersion" };
constexpr int stateVersion = 1;
} // namespace

/** Background worker: rebuilds linear-phase kernels and keeps the reported latency current. */
class MasterMindProcessor::Worker final : public juce::Thread
{
public:
    explicit Worker (MasterMindProcessor& p) : juce::Thread ("MasterMind worker"), owner (p) { startThread (juce::Thread::Priority::low); }
    ~Worker() override { stopThread (3000); }

    void run() override
    {
        while (! threadShouldExit())
        {
            owner.chain.updateLinearPhase();
            const int latency = owner.chain.latencySamples();
            if (latency != owner.getLatencySamples())
                owner.setLatencySamples (latency);
            wait (50);
        }
    }

private:
    MasterMindProcessor& owner;
};

MasterMindProcessor::MasterMindProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, mm::params::createLayout()),
      chain (parameters)
{
    parameters.state.setProperty (stateVersionKey, stateVersion, nullptr);
    ceilingParam = parameters.getRawParameterValue ("lim_ceiling");
    monoFreqParam = parameters.getRawParameterValue ("lem_mono_freq");
    worker = std::make_unique<Worker> (*this);
}

MasterMindProcessor::~MasterMindProcessor()
{
    worker.reset();
    engine.shutdown();
}

void MasterMindProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (1, samplesPerBlock);
    chain.prepare (sampleRate, maxBlock, isNonRealtime());
    loadMeter.prepare (sampleRate);
    engine.prepare (sampleRate);
    monitoring.prepare (sampleRate, maxBlock);
    inCopy.setSize (2, maxBlock, false, true, false);
    delayedIn.setSize (2, maxBlock, false, true, false);
    inputDelay.prepare (2, 60000);
    inputDelay.setDelay (chain.latencySamples());
    setLatencySamples (chain.latencySamples());
}

void MasterMindProcessor::releaseResources()
{
    chain.reset();
}

bool MasterMindProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Stereo in, stereo out only (spec section 3).
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MasterMindProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;
    mm::dsp::LoadMeter::Scope loadScope (loadMeter, buffer.getNumSamples());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
    if (buffer.getNumChannels() < 2)
        return;

    // Reset analysis when the transport starts (spec 5, setting "Reset on play").
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
        {
            const bool playing = pos->getIsPlaying();
            if (playing && ! wasPlaying && resetOnPlay.load())
            {
                engine.requestReset();
                chain.resetOvers();
            }
            wasPlaying = playing;
        }

    const bool offline = isNonRealtime();
    monitoring.inShortTerm.store (engine.inShortTermLufs.load());
    monitoring.outShortTerm.store (engine.outShortTermLufs.load());
    monitoring.outIntegrated.store (engine.outIntegratedLufs.load());

    for (int start = 0; start < buffer.getNumSamples(); start += maxBlock)
    {
        const int n = std::min (maxBlock, buffer.getNumSamples() - start);
        float* chans[2] = { buffer.getWritePointer (0) + start, buffer.getWritePointer (1) + start };
        juce::AudioBuffer<float> block (chans, 2, n);

        // IN tap copy (read-only)
        inCopy.copyFrom (0, 0, chans[0], n);
        inCopy.copyFrom (1, 0, chans[1], n);

        const bool ok = chain.process (block);
        if (! ok)
            inCopy.clear();

        // OUT tap: analysis reads before monitoring (spec 6.0 rule 1)
        engine.push (inCopy.getReadPointer (0), inCopy.getReadPointer (1), chans[0], chans[1],
                     chain.clipperReductionDb(), chain.limiterTrace(), n);

        // input aligned with the chain output, for loudness-matched bypass and delta listen
        delayedIn.copyFrom (0, 0, inCopy, 0, 0, n);
        delayedIn.copyFrom (1, 0, inCopy, 1, 0, n);
        inputDelay.setDelay (chain.latencySamples());
        float* d[2] = { delayedIn.getWritePointer (0), delayedIn.getWritePointer (1) };
        inputDelay.process (d, 2, n);
        juce::AudioBuffer<float> delayedView (d, 2, n);

        monitoring.process (block, delayedView, offline);
    }

    engine.ceiling.store (ceilingParam->load());
    engine.monoFreq.store (monoFreqParam->load());
    engine.preLimiterSampleOvers.store (chain.preLimiterSampleOvers.load());
    engine.preLimiterTpOvers.store (chain.preLimiterTpOvers.load());
    monitorActive.store (monitoring.isActive() && ! offline);
}

juce::AudioProcessorEditor* MasterMindProcessor::createEditor()
{
    return new MasterMindEditor (*this);
}

void MasterMindProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    state.setProperty (stateVersionKey, stateVersion, nullptr);
    state.removeChild (state.getChildWithName ("Extra"), nullptr);
    state.appendChild (extraState.createCopy(), nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void MasterMindProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (stateType))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    if (! state.isValid())
        return;

    // Version 1 is the first format with parameters; future versions migrate here.
    const auto extra = state.getChildWithName ("Extra");
    if (extra.isValid())
    {
        extraState.copyPropertiesAndChildrenFrom (extra, nullptr);
        state.removeChild (extra, nullptr);
    }
    state.setProperty (stateVersionKey, stateVersion, nullptr);
    parameters.replaceState (state);

    // Monitoring is never restored from a project (spec 6.0 rule 4).
    juce::MessageManager::callAsync ([weak = std::weak_ptr<bool> (alive), this]
    {
        if (! weak.expired() && onStateRestored)
            onStateRestored();
    });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterMindProcessor();
}
