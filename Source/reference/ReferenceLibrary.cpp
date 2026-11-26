#include "reference/ReferenceLibrary.h"

#include "monitoring/Monitoring.h"

#include <algorithm>

namespace mm::reference
{
namespace
{
double percentile (std::vector<double> v, double p)
{
    std::sort (v.begin(), v.end());
    const double pos = p * (double) (v.size() - 1);
    const auto i = (size_t) pos;
    const double t = pos - (double) i;
    return i + 1 < v.size() ? v[i] * (1.0 - t) + v[i + 1] * t : v[i];
}
} // namespace

ReferenceTarget buildTarget (const std::vector<const TrackAnalysis*>& set)
{
    ReferenceTarget t;
    t.count = (int) set.size();
    if (set.empty())
        return t;
    const size_t points = spectrumFreqs().size();
    t.spectrum.assign (points, 0.0f);
    t.low.assign (points, 0.0f);
    t.high.assign (points, 0.0f);
    const bool spread = set.size() >= 3;
    for (size_t k = 0; k < points; ++k)
    {
        std::vector<double> v;
        double sum = 0.0;
        for (const auto* a : set)
        {
            v.push_back (a->spectrum[k]);
            sum += a->spectrum[k];
        }
        const double mean = sum / (double) set.size();
        t.spectrum[k] = (float) mean;
        t.low[k] = (float) (spread ? std::min (mean, percentile (v, 0.10)) : mean - 1.5);
        t.high[k] = (float) (spread ? std::max (mean, percentile (v, 0.90)) : mean + 1.5);
    }
    for (int b = 0; b < analysis::numTonalBands; ++b)
    {
        std::vector<double> v, c;
        double sum = 0.0, csum = 0.0;
        for (const auto* a : set)
        {
            v.push_back (a->tonal[(size_t) b]);
            sum += a->tonal[(size_t) b];
            csum += a->crest[(size_t) b];
        }
        const double mean = sum / (double) set.size();
        t.tonal[(size_t) b] = mean;
        t.tonalLow[(size_t) b] = spread ? std::min (mean, percentile (v, 0.10)) : mean - 1.5;
        t.tonalHigh[(size_t) b] = spread ? std::max (mean, percentile (v, 0.90)) : mean + 1.5;
        t.crest[(size_t) b] = csum / (double) set.size();
    }
    double h = 0.0, sb = 0.0, li = 0.0;
    for (const auto* a : set)
    {
        h += a->harshness;
        sb += a->subToBass;
        li += a->integrated;
        for (int b = 0; b < analysis::numStereoBands; ++b)
            t.width[(size_t) b] += a->width[(size_t) b] / (double) set.size();
    }
    t.harshness = h / (double) set.size();
    t.subToBass = sb / (double) set.size();
    t.integrated = li / (double) set.size();
    return t;
}

// ---- Jobs ---------------------------------------------------------------------------------------------
class ReferenceLibrary::AnalyseJob final : public juce::ThreadPoolJob
{
public:
    AnalyseJob (ReferenceLibrary& o, int i, juce::File f)
        : juce::ThreadPoolJob ("Analyse reference"), owner (o), index (i), file (std::move (f)), alive (o.alive)
    {
    }

    JobStatus runJob() override
    {
        juce::String error;
        std::optional<TrackAnalysis> result;
        const auto cacheFile = owner.cacheFileFor (file);
        if (cacheFile.existsAsFile())
        {
            juce::var v;
            if (juce::JSON::parse (cacheFile.loadFileAsString(), v).wasOk())
                result = TrackAnalysis::fromJson (v);
        }
        if (! result)
        {
            result = analyseFile (
                file,
                [w = std::weak_ptr<bool> (alive), o = &owner, i = index, last = std::make_shared<float> (0.0f)] (float p)
                {
                    if (p - *last < 0.02f && p < 1.0f)
                        return; // throttle progress messages
                    *last = p;
                    juce::MessageManager::callAsync ([w, o, i, p]
                    {
                        if (! w.expired() && o->slots[(size_t) i].state == State::analysing)
                        {
                            o->slots[(size_t) i].progress = p;
                            o->sendChangeMessage();
                        }
                    });
                },
                owner.cancelAll, error);
            if (result)
            {
                cacheFile.getParentDirectory().createDirectory();
                juce::TemporaryFile tmp (cacheFile);
                if (tmp.getFile().replaceWithText (juce::JSON::toString (result->toJson(), true)))
                    tmp.overwriteTargetFileWithTemporary();
            }
        }
        juce::MessageManager::callAsync ([w = std::weak_ptr<bool> (alive), o = &owner, i = index, f = file, result, error]
        {
            if (! w.expired())
                o->finished (i, f, result, error);
        });
        return jobHasFinished;
    }

private:
    ReferenceLibrary& owner;
    int index;
    juce::File file;
    std::shared_ptr<bool> alive;
};

class ReferenceLibrary::DecodeJob final : public juce::ThreadPoolJob
{
public:
    DecodeJob (ReferenceLibrary& o, int i, juce::File f, double rate, std::vector<float> st, double integ)
        : juce::ThreadPoolJob ("Decode reference"), owner (o), index (i), file (std::move (f)), sampleRate (rate),
          shortTerm (std::move (st)), integrated (integ), alive (o.alive)
    {
    }

    JobStatus runJob() override
    {
        auto ref = std::make_unique<monitoring::ReferenceAudio>();
        juce::String error;
        const bool ok = decodeForPlayback (file, sampleRate, ref->audio, error, owner.cancelAll);
        ref->shortTerm = shortTerm;
        ref->integrated = integrated;
        ref->name = file.getFileNameWithoutExtension();
        ref->slot = index;
        auto* raw = ok ? ref.release() : nullptr;
        juce::MessageManager::callAsync ([w = std::weak_ptr<bool> (alive), o = &owner, i = index, raw]
        {
            std::unique_ptr<monitoring::ReferenceAudio> r (raw);
            if (w.expired())
                return;
            o->playbackLoading = false;
            if (r != nullptr && o->selectedSlot == i)
                o->monitoring.setReference (std::move (r));
            o->sendChangeMessage();
        });
        return jobHasFinished;
    }

private:
    ReferenceLibrary& owner;
    int index;
    juce::File file;
    double sampleRate;
    std::vector<float> shortTerm;
    double integrated;
    std::shared_ptr<bool> alive;
};

// ---- Library ------------------------------------------------------------------------------------------
ReferenceLibrary::ReferenceLibrary (monitoring::Monitoring& m, juce::File cacheFolder) : monitoring (m), cache (std::move (cacheFolder)) {}

ReferenceLibrary::~ReferenceLibrary()
{
    cancelAll.store (true);
    alive.reset();
    pool.removeAllJobs (true, 10000);
}

juce::File ReferenceLibrary::cacheFileFor (const juce::File& audio) const
{
    const auto key = audio.getFullPathName() + "|" + juce::String (audio.getSize()) + "|"
                   + juce::String (audio.getLastModificationTime().toMilliseconds());
    return cache.getChildFile (juce::String::toHexString (key.hashCode64()) + ".json");
}

void ReferenceLibrary::load (int index, const juce::File& file)
{
    if (index < 0 || index >= numSlots)
        return;
    auto& s = slots[(size_t) index];
    s.file = file;
    s.state = State::analysing;
    s.progress = 0.0f;
    s.error.clear();
    s.analysis.reset();
    pool.addJob (new AnalyseJob (*this, index, file), true);
    sendChangeMessage();
}

void ReferenceLibrary::finished (int index, const juce::File& file, std::optional<TrackAnalysis> result, juce::String error)
{
    auto& s = slots[(size_t) index];
    if (s.file != file)
        return; // replaced meanwhile
    if (result)
    {
        result->name = file.getFileNameWithoutExtension();
        s.analysis = result;
        s.state = State::ready;
        s.progress = 1.0f;
    }
    else
    {
        s.state = State::failed;
        s.error = error;
    }
    recomputeTarget();
    if (selectedSlot == index)
        select (index);
    sendChangeMessage();
}

void ReferenceLibrary::clear (int index)
{
    if (index < 0 || index >= numSlots)
        return;
    slots[(size_t) index] = Slot {};
    if (selectedSlot == index)
        select (-1);
    recomputeTarget();
    sendChangeMessage();
}

void ReferenceLibrary::setInSet (int index, bool inSet)
{
    if (index < 0 || index >= numSlots)
        return;
    slots[(size_t) index].inSet = inSet;
    recomputeTarget();
    sendChangeMessage();
}

void ReferenceLibrary::select (int index)
{
    selectedSlot = index;
    if (index < 0 || slots[(size_t) index].state != State::ready)
    {
        monitoring.setReference (nullptr);
        playbackLoading = false;
        sendChangeMessage();
        return;
    }
    const auto& s = slots[(size_t) index];
    playbackLoading = true;
    pool.addJob (new DecodeJob (*this, index, s.file, monitoring.getSampleRate(), s.analysis->shortTerm, s.analysis->integrated), true);
    sendChangeMessage();
}

void ReferenceLibrary::recomputeTarget()
{
    std::vector<const TrackAnalysis*> set;
    for (const auto& s : slots)
        if (s.state == State::ready && s.inSet && s.analysis)
            set.push_back (&*s.analysis);
    currentTarget = buildTarget (set);
}

analysis::Targets ReferenceLibrary::checkTargets() const
{
    analysis::Targets t;
    if (currentTarget.count == 0)
        return t;
    t.tonal = currentTarget.tonal;
    for (int b = 0; b < analysis::numTonalBands; ++b)
        t.corridor[(size_t) b] = 0.5 * (currentTarget.tonalHigh[(size_t) b] - currentTarget.tonalLow[(size_t) b]);
    t.harshness = currentTarget.harshness;
    t.referenceCount = currentTarget.count;
    t.name = "Ref set (" + juce::String (currentTarget.count) + ")";
    return t;
}

juce::ValueTree ReferenceLibrary::toState() const
{
    juce::ValueTree v ("References");
    v.setProperty ("selected", selectedSlot, nullptr);
    for (int i = 0; i < numSlots; ++i)
    {
        juce::ValueTree s ("Slot");
        s.setProperty ("index", i, nullptr);
        s.setProperty ("path", slots[(size_t) i].file.getFullPathName(), nullptr);
        s.setProperty ("inSet", slots[(size_t) i].inSet, nullptr);
        v.appendChild (s, nullptr);
    }
    return v;
}

void ReferenceLibrary::fromState (const juce::ValueTree& v)
{
    if (! v.isValid())
        return;
    for (const auto& s : v)
    {
        const int i = s.getProperty ("index", -1);
        const juce::String path = s.getProperty ("path", "");
        if (i < 0 || i >= numSlots)
            continue;
        slots[(size_t) i].inSet = s.getProperty ("inSet", true);
        if (path.isNotEmpty() && juce::File::isAbsolutePath (path))
        {
            const juce::File f (path);
            if (f.existsAsFile())
                load (i, f);
            else
            {
                slots[(size_t) i].file = f;
                slots[(size_t) i].state = State::failed;
                slots[(size_t) i].error = "File not found: " + path;
            }
        }
    }
    recomputeTarget();
    sendChangeMessage();
}
} // namespace mm::reference
