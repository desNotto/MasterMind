#pragma once

#include "PluginProcessor.h"
#include "state/GlobalSettings.h"
#include "ui/Widgets.h"

#include <functional>

namespace mm::ui
{
/** Shared state of the editor: the processor's services, the latest analysis snapshot and
    view settings. Message thread only. */
struct Context
{
    explicit Context (MasterMindProcessor& p)
        : proc (p), apvts (p.getValueTreeState()), engine (p.getEngine()), monitoring (p.getMonitoring()),
          presets (p.getPresets()), refs (p.getReferences()), album (p.getAlbum()), settings (GlobalSettings::defaultFile())
    {
    }

    MasterMindProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    analysis::Engine& engine;
    monitoring::Monitoring& monitoring;
    presets::PresetManager& presets;
    reference::ReferenceLibrary& refs;
    album::AlbumFile& album;
    GlobalSettings settings;

    analysis::Snapshot snap;
    int tap = 2;             // analyse: 0 In, 1 Out, 2 Both
    int spectrumMode = 0;    // 0 real time, 1 average, 2 peak hold
    bool delta = false;      // spectrum delta view
    int selectedEqBand = 5;  // 1..10
    int selectedModule = 1;  // module card with the orange outline

    /** Navigation (set by the editor). */
    std::function<void (const juce::String&)> showTab;
    std::function<void (const juce::String& moduleKey)> showModuleDetail;
    std::function<void (const juce::String& message)> notify;

    const analysis::TapView& out() const { return snap.out; }
    const analysis::TapView& in() const { return snap.in; }
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float plain)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
    }
};
} // namespace mm::ui
