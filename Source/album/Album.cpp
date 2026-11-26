#include "album/Album.h"

#include "analysis/Checks.h"

namespace mm::album
{
juce::var Album::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("formatVersion", 1);
    o->setProperty ("name", name);
    o->setProperty ("targetMode", (int) targetMode);
    o->setProperty ("anchor", anchor);
    juce::Array<juce::var> t;
    for (const auto& p : tracks)
        t.add (p.toJson());
    o->setProperty ("tracks", t);
    auto* s = new juce::DynamicObject();
    for (const auto& [k, v] : snapshots)
        s->setProperty (k, v);
    o->setProperty ("snapshots", juce::var (s));
    return juce::var (o);
}

std::optional<Album> Album::fromJson (const juce::var& v, juce::String& error)
{
    if (! v.isObject() || (int) v.getProperty ("formatVersion", 0) != 1)
    {
        error = "Not an album file (missing formatVersion 1).";
        return std::nullopt;
    }
    Album a;
    a.name = v.getProperty ("name", "Album").toString();
    a.targetMode = (TargetMode) juce::jlimit (0, 2, (int) v.getProperty ("targetMode", 0));
    a.anchor = v.getProperty ("anchor", -1);
    if (auto* arr = v.getProperty ("tracks", {}).getArray())
        for (const auto& t : *arr)
            if (auto p = reference::TrackAnalysis::fromJson (t))
                a.tracks.push_back (*p);
    if (auto* s = v.getProperty ("snapshots", {}).getDynamicObject())
        for (const auto& prop : s->getProperties())
            a.snapshots[prop.name.toString()] = prop.value.toString();
    if (a.anchor >= (int) a.tracks.size())
        a.anchor = -1;
    return a;
}

reference::TrackAnalysis Album::target (const reference::ReferenceTarget* refs) const
{
    reference::TrackAnalysis t;
    t.name = "Album target";
    if (targetMode == TargetMode::anchor && anchor >= 0 && anchor < (int) tracks.size())
        return tracks[(size_t) anchor];
    if (targetMode == TargetMode::referenceSet && refs != nullptr && refs->count > 0)
    {
        t.tonal = refs->tonal;
        t.crest = refs->crest;
        t.harshness = refs->harshness;
        t.integrated = refs->integrated;
        t.width = refs->width;
        t.spectrum = refs->spectrum;
        return t;
    }
    if (tracks.empty())
        return t;
    // mean of all passports (default without a reference set)
    const double n = (double) tracks.size();
    t.integrated = t.lra = t.plr = t.harshness = 0.0;
    t.spectrum.assign (reference::spectrumFreqs().size(), 0.0f);
    for (const auto& p : tracks)
    {
        t.integrated += p.integrated / n;
        t.lra += p.lra / n;
        t.plr += p.plr / n;
        t.harshness += p.harshness / n;
        for (size_t b = 0; b < t.tonal.size(); ++b)
            t.tonal[b] += p.tonal[b] / n;
        for (size_t b = 0; b < t.crest.size(); ++b)
            t.crest[b] += p.crest[b] / n;
        for (size_t b = 0; b < t.width.size(); ++b)
            t.width[b] += p.width[b] / n;
        for (size_t k = 0; k < t.spectrum.size() && k < p.spectrum.size(); ++k)
            t.spectrum[k] += (float) (p.spectrum[k] / n);
    }
    return t;
}

bool AlbumFile::open (const juce::File& f, juce::String& error)
{
    juce::var v;
    const auto r = juce::JSON::parse (f.loadFileAsString(), v);
    if (r.failed())
    {
        error = "The album file is damaged: " + r.getErrorMessage();
        return false;
    }
    auto a = Album::fromJson (v, error);
    if (! a)
        return false;
    data = *a;
    file = f;
    lastModified = f.getLastModificationTime();
    return true;
}

bool AlbumFile::create (const juce::File& f, const juce::String& albumName, juce::String& error)
{
    data = Album {};
    data.name = albumName;
    file = f.hasFileExtension (extension) ? f : f.withFileExtension (extension);
    return save (error);
}

bool AlbumFile::save (juce::String& error)
{
    if (file == juce::File())
    {
        error = "No album file is open.";
        return false;
    }
    file.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (file);
    if (! tmp.getFile().replaceWithText (juce::JSON::toString (data.toJson())) || ! tmp.overwriteTargetFileWithTemporary())
    {
        error = "Cannot write the album file " + file.getFullPathName();
        return false;
    }
    lastModified = file.getLastModificationTime();
    return true;
}

bool AlbumFile::reloadIfChanged()
{
    if (file == juce::File() || ! file.existsAsFile())
        return false;
    const auto t = file.getLastModificationTime();
    if (t == lastModified)
        return false;
    juce::String err;
    const auto keep = file;
    if (open (keep, err))
        return true;
    lastModified = t; // ignore a half-written or broken file until it changes again
    return false;
}

void AlbumFile::close()
{
    file = juce::File();
    data = Album {};
}

bool AlbumFile::storePassport (const reference::TrackAnalysis& passport, juce::String& error)
{
    reloadIfChanged(); // merge with writes from other instances first
    bool replaced = false;
    for (auto& t : data.tracks)
        if (t.name == passport.name)
        {
            t = passport;
            replaced = true;
        }
    if (! replaced)
        data.tracks.push_back (passport);
    return save (error);
}

juce::StringArray albumHints (const Album& a, const reference::TrackAnalysis& target, double thresholdDb)
{
    juce::StringArray out;
    for (size_t i = 0; i < a.tracks.size(); ++i)
    {
        const auto& t = a.tracks[i];
        juce::StringArray parts;
        for (int b = 0; b < analysis::numTonalBands; ++b)
        {
            const double d = t.tonal[(size_t) b] - target.tonal[(size_t) b];
            if (std::abs (d) >= thresholdDb)
            {
                const auto v = juce::String (std::abs (d), 1);
                parts.add (juce::String (analysis::tonalBandName (b)) + " " + (d > 0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + v + " dB");
            }
        }
        const double ld = t.integrated - target.integrated;
        if (std::abs (ld) >= thresholdDb)
            parts.add ("Loudness " + juce::String (ld > 0 ? "+" : juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92"))) + juce::String (std::abs (ld), 1) + " LU");
        if (! parts.isEmpty())
            out.add ("Track " + juce::String ((int) i + 1) + ": " + parts.joinIntoString (", ") + " vs album target");
    }
    return out;
}
} // namespace mm::album
