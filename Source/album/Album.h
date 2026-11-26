#pragma once

#include "reference/ReferenceLibrary.h"
#include "reference/TrackAnalysis.h"

#include <juce_core/juce_core.h>

#include <map>
#include <optional>
#include <vector>

namespace mm::album
{
/** Album project (spec 7.4): passports, target mode, anchor track and chain snapshots. */
struct Album
{
    enum class TargetMode
    {
        mean,
        referenceSet,
        anchor
    };

    juce::String name = "Album";
    std::vector<reference::TrackAnalysis> tracks; // passports in track order
    TargetMode targetMode = TargetMode::mean;
    int anchor = -1;
    std::map<juce::String, juce::String> snapshots; // track name -> preset JSON of the chain

    juce::var toJson() const;
    static std::optional<Album> fromJson (const juce::var& v, juce::String& error);

    /** Album target as a TrackAnalysis-shaped set of values (tonal, loudness, PLR, width ...). */
    reference::TrackAnalysis target (const reference::ReferenceTarget* refs) const;
};

/** Album file I/O with atomic writes and reload on external change (another plugin instance). */
class AlbumFile
{
public:
    static constexpr const char* extension = ".mastermind-album";

    bool open (const juce::File& f, juce::String& error);
    bool create (const juce::File& f, const juce::String& albumName, juce::String& error);
    bool save (juce::String& error);
    /** Reloads when the file changed on disk. Returns true if reloaded. */
    bool reloadIfChanged();
    void close();

    bool isOpen() const { return file != juce::File(); }
    juce::File getFile() const { return file; }
    Album& album() { return data; }
    const Album& album() const { return data; }

    /** Adds or replaces the passport of a track (by name) and saves. */
    bool storePassport (const reference::TrackAnalysis& passport, juce::String& error);

private:
    juce::File file;
    Album data;
    juce::Time lastModified;
};

/** Per-track deviation hints, e.g. "Track 5: Bass +2.3 dB, Air -1.1 dB vs album target". */
juce::StringArray albumHints (const Album& a, const reference::TrackAnalysis& target, double thresholdDb = 1.0);
} // namespace mm::album
