#pragma once

#include "album/Album.h"
#include "analysis/Engine.h"
#include "dsp/LoadMeter.h"
#include "dsp/MasterChain.h"
#include "monitoring/Monitoring.h"
#include "presets/PresetManager.h"
#include "reference/ReferenceLibrary.h"
#include "state/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

class MasterMindProcessor final : public juce::AudioProcessor
{
public:
    MasterMindProcessor();
    ~MasterMindProcessor() override;

    // Audio
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    // Editor
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    // Info
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Programs (presets are handled by the plugin itself, section 9)
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    // State
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Audio thread load, 0..1 (smoothed). Safe to call from any thread. */
    float getCpuLoad() const noexcept { return loadMeter.getLoadProportion(); }

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }
    mm::dsp::MasterChain& getChain() noexcept { return chain; }
    mm::analysis::Engine& getEngine() noexcept { return engine; }
    mm::monitoring::Monitoring& getMonitoring() noexcept { return monitoring; }

    /** Non-parameter state stored with the project: reference paths, reference set, album path,
        correction profile paths, preset name and its modified flag (spec section 9). Message thread. */
    juce::ValueTree& getExtraState() noexcept { return extraState; }

    /** Called after setStateInformation on the message thread (the editor listens). */
    std::function<void()> onStateRestored;

    // Message-thread services that live as long as the plugin instance
    mm::presets::PresetManager& getPresets() noexcept { return *presets; }
    mm::reference::ReferenceLibrary& getReferences() noexcept { return *references; }
    mm::album::AlbumFile& getAlbum() noexcept { return album; }

    /** Copies preset, reference and album state into the project state (message thread). */
    void syncExtraState();

    /** Monitor correction profile files (project state, spec 9). */
    juce::File correctionFiles[2];
    float correctionStrength = 1.0f, correctionBoostLimit = 3.0f;
    bool correctionCutOnly = true;
    void reloadCorrection (int slot);

    std::atomic<bool> resetOnPlay { true };
    std::atomic<bool> monitorActive { false };

private:
    class Worker;

    juce::AudioProcessorValueTreeState parameters;
    juce::ValueTree extraState { "Extra" };
    mm::dsp::MasterChain chain;
    mm::dsp::LoadMeter loadMeter;
    mm::analysis::Engine engine;
    mm::monitoring::Monitoring monitoring;
    std::unique_ptr<Worker> worker;
    std::unique_ptr<mm::presets::PresetManager> presets;
    std::unique_ptr<mm::reference::ReferenceLibrary> references;
    mm::album::AlbumFile album;
    void restoreExtraState();

    juce::AudioBuffer<float> inCopy, delayedIn;
    mm::dsp::DelayLine inputDelay;
    int maxBlock = 512;
    bool wasPlaying = false;
    std::atomic<float>* ceilingParam = nullptr;
    std::atomic<float>* monoFreqParam = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterMindProcessor)
};
