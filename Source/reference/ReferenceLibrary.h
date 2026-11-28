#pragma once

#include "analysis/Checks.h"
#include "reference/TrackAnalysis.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <array>
#include <memory>
#include <optional>

namespace mm::monitoring
{
class Monitoring;
}

namespace mm::reference
{
/** Reference target built from the reference set (spec 7.2). */
struct ReferenceTarget
{
    int count = 0;
    std::vector<float> spectrum, low, high; // normalised band power at spectrumFreqs(): target and corridor
    std::array<double, analysis::numTonalBands> tonal {}, tonalLow {}, tonalHigh {}, crest {};
    double harshness = 0.0, subToBass = 0.0, integrated = -100.0;
    std::array<double, analysis::numStereoBands> width {};
};

/** Builds the target and corridor: mean of the set, 10th-90th percentile with 3 or more
    tracks, otherwise a fixed +/-1.5 dB. */
ReferenceTarget buildTarget (const std::vector<const TrackAnalysis*>& set);

/** Six reference slots with background analysis, an on-disk cache and playback loading. */
class ReferenceLibrary : public juce::ChangeBroadcaster
{
public:
    static constexpr int numSlots = 6;

    enum class State
    {
        empty,
        analysing,
        ready,
        failed
    };

    struct Slot
    {
        juce::File file;
        State state = State::empty;
        float progress = 0.0f;
        juce::String error;
        std::optional<TrackAnalysis> analysis;
        bool inSet = true;
    };

    ReferenceLibrary (monitoring::Monitoring& monitoring, juce::File cacheFolder);
    ~ReferenceLibrary() override;

    const Slot& slot (int i) const { return slots[(size_t) std::clamp (i, 0, numSlots - 1)]; }
    void load (int index, const juce::File& file);
    void clear (int index);
    void setInSet (int index, bool inSet);

    /** Selects the slot for playback (decodes in the background). -1 = none. */
    void select (int index);
    int selected() const { return selectedSlot; }

    /** The buttons 1-6 (mockup v18): makes the slot the selected reference for the analysis and
        Auto match without touching playback. References play only from the References tab. */
    void choose (int index);

    /** Plays a library song (References tab) through B without assigning it to a slot. Decodes in
        the background, then switches B on and starts playback. Selecting a slot ends the preview. */
    void preview (const juce::File& file);
    juce::File previewing() const { return previewFile; }
    bool isLoadingPlayback() const { return playbackLoading; }

    ReferenceTarget target() const { return currentTarget; }
    /** Targets for the checks (tonal, corridor, harshness), or the defaults without references. */
    analysis::Targets checkTargets() const;

    /** Project state (paths and set flags). */
    juce::ValueTree toState() const;
    void fromState (const juce::ValueTree& v);

    /** Cache file for an audio file (keyed by path, size and modification time). */
    juce::File cacheFileFor (const juce::File& audio) const;

private:
    class AnalyseJob;
    class DecodeJob;
    void finished (int index, const juce::File& file, std::optional<TrackAnalysis> result, juce::String error);
    void recomputeTarget();

    monitoring::Monitoring& monitoring;
    juce::File cache;
    std::array<Slot, numSlots> slots;
    juce::ThreadPool pool { 2 };
    int selectedSlot = -1;
    bool playbackLoading = false;
    juce::File previewFile;
    ReferenceTarget currentTarget;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    std::atomic<bool> cancelAll { false };
};
} // namespace mm::reference
