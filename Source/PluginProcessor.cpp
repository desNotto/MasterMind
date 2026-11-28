#include "PluginProcessor.h"

#include "state/GlobalSettings.h"
#include "ui/PluginEditor.h"

namespace
{
const juce::Identifier stateType { "MasterMindState" };
const juce::Identifier stateVersionKey { "stateVersion" };
constexpr int stateVersion = 2; // 2: six-band Saturation (D-047)
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
            if (! owner.preparing.load())
            {
                const int latency = owner.chain.latencySamples();
                if (latency != owner.getLatencySamples())
                    owner.setLatencySamples (latency);
            }
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
    const auto dataDir = mm::GlobalSettings::userDataDirectory();
    presets = std::make_unique<mm::presets::PresetManager> (parameters, dataDir.getChildFile ("presets"));
    references = std::make_unique<mm::reference::ReferenceLibrary> (monitoring, dataDir.getChildFile ("refcache"));
    worker = std::make_unique<Worker> (*this);
}

MasterMindProcessor::~MasterMindProcessor()
{
    alive.reset();
    worker.reset();
    references.reset();
    engine.shutdown();
}

void MasterMindProcessor::syncExtraState()
{
    extraState.removeAllChildren (nullptr);
    extraState.appendChild (presets->toState(), nullptr);
    extraState.appendChild (references->toState(), nullptr);
    extraState.setProperty ("album", album.getFile().getFullPathName(), nullptr);
    extraState.setProperty ("correction1", correctionFiles[0].getFullPathName(), nullptr);
    extraState.setProperty ("correction2", correctionFiles[1].getFullPathName(), nullptr);
    extraState.setProperty ("correctionStrength", correctionStrength, nullptr);
    extraState.setProperty ("correctionBoostLimit", correctionBoostLimit, nullptr);
    extraState.setProperty ("correctionCutOnly", correctionCutOnly, nullptr);
}

void MasterMindProcessor::reloadCorrection (int slot)
{
    slot = juce::jlimit (0, 1, slot);
    const auto f = correctionFiles[slot];
    if (! f.existsAsFile())
    {
        monitoring.setCorrection (slot, nullptr);
        return;
    }
    const double fs = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    if (f.hasFileExtension ("wav;aif;aiff;flac"))
    {
        std::unique_ptr<juce::AudioFormatReader> r (mm::reference::formatManager().createReaderFor (f));
        if (r != nullptr && r->lengthInSamples > 0 && r->lengthInSamples < (juce::int64) r->sampleRate * 10)
        {
            juce::AudioBuffer<float> ir ((int) std::min<unsigned> (2, r->numChannels), (int) r->lengthInSamples);
            r->read (&ir, 0, (int) r->lengthInSamples, 0, true, true);
            monitoring.setCorrectionImpulse (slot, std::move (ir), r->sampleRate);
            auto set = std::make_unique<mm::monitoring::Monitoring::FilterSet>();
            set->useConvolution = true;
            monitoring.setCorrection (slot, std::move (set));
        }
        return;
    }
    mm::monitoring::ParametricProfile p;
    juce::String err;
    if (mm::monitoring::parseParametric (f.loadFileAsString(), p, err))
        monitoring.setCorrection (slot, mm::monitoring::Monitoring::buildCorrection (p, fs, correctionStrength, correctionBoostLimit, correctionCutOnly));
}

void MasterMindProcessor::restoreExtraState()
{
    presets->fromState (extraState.getChildWithName ("Preset"));
    references->fromState (extraState.getChildWithName ("References"));
    const juce::String albumPath = extraState.getProperty ("album", "");
    if (albumPath.isNotEmpty() && juce::File::isAbsolutePath (albumPath) && juce::File (albumPath).existsAsFile())
    {
        juce::String err;
        album.open (juce::File (albumPath), err);
    }
    for (int i = 0; i < 2; ++i)
    {
        const juce::String path = extraState.getProperty ("correction" + juce::String (i + 1), "");
        correctionFiles[i] = path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }
    correctionStrength = extraState.getProperty ("correctionStrength", 1.0f);
    correctionBoostLimit = extraState.getProperty ("correctionBoostLimit", 3.0f);
    correctionCutOnly = extraState.getProperty ("correctionCutOnly", true);
    reloadCorrection (0);
    reloadCorrection (1);
}

void MasterMindProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparing = true;
    maxBlock = std::max (1, samplesPerBlock);
    chain.prepare (sampleRate, maxBlock, isNonRealtime());
    loadMeter.prepare (sampleRate);
    engine.prepare (sampleRate);
    monitoring.prepare (sampleRate, maxBlock);
    juce::MessageManager::callAsync ([weak = std::weak_ptr<bool> (alive), this]
    {
        if (! weak.expired())
        {
            reloadCorrection (0);
            reloadCorrection (1);
        }
    });
    inCopy.setSize (2, maxBlock, false, true, false);
    delayedIn.setSize (2, maxBlock, false, true, false);
    inputDelay.prepare (2, 120000);
    inputDelay.setDelay (chain.latencySamples());
    setLatencySamples (chain.latencySamples());
    preparing = false;
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
    if (juce::MessageManager::existsAndIsCurrentThread())
        syncExtraState();
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

    // Version 1 is the first format with parameters; version 2 has the six-band Saturation (D-047).
    if ((int) state.getProperty (stateVersionKey, 1) < 2)
    {
        std::map<juce::String, float> values;
        for (const auto& c : state)
            if (c.hasType ("PARAM"))
                values[c.getProperty ("id").toString()] = (float) (double) c.getProperty ("value");
        if (mm::params::migrateSaturation (values))
            for (int b = 1; b <= mm::params::satBands; ++b)
                for (const auto* f : { "drive", "type", "bias", "mix", "out", "solo", "mute" })
                {
                    const auto id = mm::params::sat (b, f);
                    auto node = state.getChildWithProperty ("id", id);
                    if (! node.isValid())
                    {
                        node = juce::ValueTree ("PARAM");
                        node.setProperty ("id", id, nullptr);
                        state.appendChild (node, nullptr);
                    }
                    node.setProperty ("value", values[id], nullptr);
                }
    }
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
        if (weak.expired())
            return;
        restoreExtraState();
        if (onStateRestored)
            onStateRestored();
    });
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MasterMindProcessor();
}
