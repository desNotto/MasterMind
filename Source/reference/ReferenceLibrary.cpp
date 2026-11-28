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

/** Analysis of the metrics region of a song (D-041). Cached like the whole-song analysis, keyed by
    the region too. */
class ReferenceLibrary::RegionJob final : public juce::ThreadPoolJob
{
public:
    RegionJob (ReferenceLibrary& o, juce::File f, Region r)
        : juce::ThreadPoolJob ("Analyse reference region"), owner (o), file (std::move (f)), region (r), alive (o.alive)
    {
    }

    JobStatus runJob() override
    {
        juce::String error;
        std::optional<TrackAnalysis> result;
        const auto base = owner.cacheFileFor (file);
        const auto cacheFile = base.getSiblingFile (base.getFileNameWithoutExtension() + "-r" + juce::String (juce::roundToInt (region.start * 10.0)) + "-"
                                                    + juce::String (juce::roundToInt (region.end * 10.0)) + ".json");
        if (cacheFile.existsAsFile())
        {
            juce::var v;
            if (juce::JSON::parse (cacheFile.loadFileAsString(), v).wasOk())
                result = TrackAnalysis::fromJson (v);
        }
        if (! result)
        {
            result = analyseFile (file, {}, owner.cancelAll, error, region.start, region.end);
            if (result)
            {
                cacheFile.getParentDirectory().createDirectory();
                juce::TemporaryFile tmp (cacheFile);
                if (tmp.getFile().replaceWithText (juce::JSON::toString (result->toJson(), true)))
                    tmp.overwriteTargetFileWithTemporary();
            }
        }
        juce::MessageManager::callAsync ([w = std::weak_ptr<bool> (alive), o = &owner, f = file, r = region, result]
        {
            if (w.expired())
                return;
            const auto key = f.getFullPathName();
            if (auto it = o->regionJobs.find (key); it != o->regionJobs.end() && --it->second <= 0)
                o->regionJobs.erase (it);
            const auto now = o->region (f);
            if (std::abs (now.start - r.start) > 1.0e-6 || std::abs (now.end - r.end) > 1.0e-6)
                return; // the region changed meanwhile; its own job delivers
            for (auto& s : o->slots)
                if (s.file == f && s.state == State::ready)
                {
                    s.regionAnalysis = result;
                    if (s.regionAnalysis && s.analysis)
                        s.regionAnalysis->name = s.analysis->name;
                }
            o->recomputeTarget();
            o->sendChangeMessage();
        });
        return jobHasFinished;
    }

private:
    ReferenceLibrary& owner;
    juce::File file;
    Region region;
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
        ref->file = file;
        ref->slot = index;
        if (ok && ref->shortTerm.empty())
        {
            // library preview: loudness curve from the analysis cache, when the song is analysed
            const auto v = juce::JSON::parse (owner.cacheFileFor (file).loadFileAsString());
            if (const auto a = TrackAnalysis::fromJson (v))
            {
                ref->shortTerm = a->shortTerm;
                ref->integrated = a->integrated;
            }
        }
        if (ok)
            ref->computeOverview();
        auto* raw = ok ? ref.release() : nullptr;
        juce::MessageManager::callAsync ([w = std::weak_ptr<bool> (alive), o = &owner, i = index, f = file, raw]
        {
            std::unique_ptr<monitoring::ReferenceAudio> r (raw);
            if (w.expired())
                return;
            o->playbackLoading = false;
            if (r != nullptr && i >= 0 && o->selectedSlot == i && o->previewFile == juce::File())
                o->monitoring.setReference (std::move (r));
            else if (r != nullptr && i < 0 && o->previewFile == f)
            {
                o->monitoring.setReference (std::move (r));
                o->monitoring.seekRequest = 0.0;
                o->monitoring.referenceActive = true;
                o->monitoring.refPlaying = true;
            }
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
        s.regionAnalysis.reset();
        s.state = State::ready;
        s.progress = 1.0f;
    }
    else
    {
        s.state = State::failed;
        s.error = error;
    }
    recomputeTarget();
    if (result)
        startRegionJobs (file);
    if (selectedSlot == index && previewFile == juce::File())
        select (index);
    sendChangeMessage();
}

void ReferenceLibrary::clear (int index)
{
    if (index < 0 || index >= numSlots)
        return;
    slots[(size_t) index] = Slot {};
    if (selectedSlot == index && previewFile == juce::File())
        select (-1);
    else if (selectedSlot == index)
        selectedSlot = -1; // a library preview keeps playing
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

void ReferenceLibrary::preview (const juce::File& file)
{
    if (! file.existsAsFile())
        return;
    previewFile = file;
    playbackLoading = true;
    pool.addJob (new DecodeJob (*this, -1, file, monitoring.getSampleRate(), {}, -100.0), true);
    sendChangeMessage();
}

void ReferenceLibrary::choose (int index)
{
    selectedSlot = index;
    sendChangeMessage();
}

void ReferenceLibrary::select (int index)
{
    selectedSlot = index;
    previewFile = juce::File();
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
        if (s.state == State::ready && s.inSet && s.metrics() != nullptr)
            set.push_back (s.metrics());
    currentTarget = buildTarget (set);
}

ReferenceLibrary::Region ReferenceLibrary::region (const juce::File& file) const
{
    const auto it = regions.find (file.getFullPathName());
    return it != regions.end() ? it->second : Region {};
}

bool ReferenceLibrary::isAnalysingRegion (const juce::File& file) const { return regionJobs.count (file.getFullPathName()) > 0; }

void ReferenceLibrary::setRegion (const juce::File& file, double start, double end)
{
    const Region r { std::min (start, end), std::max (start, end) };
    if (r.valid())
        regions[file.getFullPathName()] = r;
    else
        regions.erase (file.getFullPathName());
    for (auto& s : slots)
        if (s.file == file)
            s.regionAnalysis.reset();
    if (r.valid())
        startRegionJobs (file);
    recomputeTarget();
    sendChangeMessage();
}

void ReferenceLibrary::startRegionJobs (const juce::File& file)
{
    const auto r = region (file);
    if (! r.valid())
        return;
    bool used = false;
    for (const auto& s : slots)
        used = used || (s.file == file && s.state == State::ready);
    if (! used)
        return; // a library song without a slot: analysed when it gets one
    ++regionJobs[file.getFullPathName()];
    pool.addJob (new RegionJob (*this, file, r), true);
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
    for (const auto& [path, r] : regions)
    {
        juce::ValueTree rv ("Region");
        rv.setProperty ("path", path, nullptr);
        rv.setProperty ("start", r.start, nullptr);
        rv.setProperty ("end", r.end, nullptr);
        v.appendChild (rv, nullptr);
    }
    return v;
}

void ReferenceLibrary::fromState (const juce::ValueTree& v)
{
    if (! v.isValid())
        return;
    regions.clear();
    for (const auto& rv : v)
        if (rv.hasType ("Region"))
        {
            const Region r { (double) rv.getProperty ("start", 0.0), (double) rv.getProperty ("end", 0.0) };
            if (r.valid())
                regions[rv.getProperty ("path", "").toString()] = r;
        }
    for (const auto& s : v)
    {
        if (! s.hasType ("Slot"))
            continue;
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
