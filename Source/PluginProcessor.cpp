#include "PluginProcessor.h"

#include "ui/PluginEditor.h"

namespace
{
const juce::Identifier stateType { "MasterMindState" };
const juce::Identifier stateVersionKey { "stateVersion" };
constexpr int stateVersion = 1;

} // namespace

MasterMindProcessor::MasterMindProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, stateType, mm::params::createLayout()),
      chain (parameters)
{
    parameters.state.setProperty (stateVersionKey, stateVersion, nullptr);
}

void MasterMindProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    chain.prepare (sampleRate, samplesPerBlock, isNonRealtime());
    loadMeter.prepare (sampleRate);
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

    chain.process (buffer);
}

juce::AudioProcessorEditor* MasterMindProcessor::createEditor()
{
    return new MasterMindEditor (*this);
}

void MasterMindProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const auto state = parameters.copyState();

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

    state.setProperty (stateVersionKey, stateVersion, nullptr);
    parameters.replaceState (state);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterMindProcessor();
}
