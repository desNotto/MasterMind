// Presets (spec 9), references (7.1-7.3) and album (7.4) tests.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "TestHost.h"
#include "album/Album.h"
#include "presets/PresetManager.h"
#include "reference/MatchEq.h"
#include "reference/TrackAnalysis.h"

#include <chrono>

using Catch::Approx;

namespace
{
struct TempDir
{
    TempDir() : dir (juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("mm_state_test", "", false))
    {
        dir.createDirectory();
    }
    ~TempDir() { dir.deleteRecursively(); }
    juce::File dir;
};

bool sameValues (const std::map<juce::String, float>& a, const std::map<juce::String, float>& b)
{
    if (a.size() != b.size())
        return false;
    for (const auto& [id, v] : a)
    {
        const auto* spec = mm::params::find (id);
        const auto it = b.find (id);
        if (spec == nullptr || it == b.end())
            return false;
        if (std::abs (v - it->second) > 1.0e-4f * std::max (1.0f, spec->maxValue - spec->minValue))
            return false;
    }
    return true;
}

juce::File writeWav (const juce::File& f, double fs, double seconds, double freq, float amp)
{
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
    REQUIRE (os != nullptr);
    auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions().withSampleRate (fs).withNumChannels (2).withBitsPerSample (24));
    REQUIRE (w != nullptr);
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (2, 65536);
    juce::Random r (3);
    for (int pos = 0; pos < n; pos += 65536)
    {
        const int len = std::min (65536, n - pos);
        for (int i = 0; i < len; ++i)
        {
            const float v = amp * (float) std::sin (6.283185307179586 * freq * (pos + i) / fs) + 0.05f * (r.nextFloat() - 0.5f);
            b.setSample (0, i, v);
            b.setSample (1, i, v * 0.9f);
        }
        w->writeFromAudioSampleBuffer (b, 0, len);
    }
    return f;
}
} // namespace

TEST_CASE ("Presets: save, load, rename and delete round-trip", "[presets]")
{
    TempDir tmp;
    mmtest::Host h;
    mm::presets::PresetManager pm (h.apvts, tmp.dir);
    juce::String err;

    h.set ("lim_on", 1.0f);
    h.set ("lim_drive", 6.5f);
    h.set ("eq_b3_freq", 333.0f);
    const auto before = pm.captureValues();
    REQUIRE (pm.saveAs ("My Master", false, err) == mm::presets::PresetManager::Result::ok);
    CHECK_FALSE (pm.isModified());
    CHECK (pm.saveAs ("My Master", false, err) == mm::presets::PresetManager::Result::exists);
    CHECK (pm.saveAs ("bad/name", false, err) == mm::presets::PresetManager::Result::invalidName);
    CHECK (pm.saveAs (juce::String::repeatedString ("x", 65), false, err) == mm::presets::PresetManager::Result::invalidName);

    h.set ("lim_drive", 0.0f);
    CHECK (pm.isModified());

    mm::presets::Entry mine;
    for (const auto& e : pm.refresh())
        if (e.name == "My Master")
            mine = e;
    REQUIRE (mine.file.existsAsFile());
    REQUIRE (pm.load (mine, err) == mm::presets::PresetManager::Result::ok);
    CHECK (sameValues (pm.captureValues(), before));

    REQUIRE (pm.rename (mine, "Renamed", err) == mm::presets::PresetManager::Result::ok);
    mm::presets::Entry renamed;
    for (const auto& e : pm.refresh())
        if (e.name == "Renamed")
            renamed = e;
    REQUIRE (renamed.file.existsAsFile());
    CHECK (pm.currentName() == "Renamed");
    REQUIRE (pm.load (renamed, err) == mm::presets::PresetManager::Result::ok);
    CHECK (sameValues (pm.captureValues(), before));

    REQUIRE (pm.remove (renamed, err) == mm::presets::PresetManager::Result::ok);
    CHECK_FALSE (renamed.file.existsAsFile());
    CHECK (sameValues (pm.captureValues(), before)); // deleting the loaded preset keeps the sound
    CHECK (pm.isModified());

    // factory presets are read-only and all load without errors
    for (const auto& e : pm.refresh())
        if (e.factory)
        {
            REQUIRE (pm.load (e, err) == mm::presets::PresetManager::Result::ok);
            CHECK (pm.remove (e, err) == mm::presets::PresetManager::Result::factoryReadOnly);
        }
}

TEST_CASE ("A corrupt preset produces an error, never a crash", "[presets]")
{
    TempDir tmp;
    mmtest::Host h;
    mm::presets::PresetManager pm (h.apvts, tmp.dir);
    const auto bad = tmp.dir.getChildFile ("Broken.mbpreset");
    bad.replaceWithText ("{ this is not json");
    juce::String err;
    for (const auto& e : pm.refresh())
        if (e.name == "Broken")
            CHECK (pm.load (e, err) == mm::presets::PresetManager::Result::corrupt);
    CHECK (err.isNotEmpty());
    // unknown IDs are ignored and missing IDs take defaults
    std::map<juce::String, float> values;
    juce::String name;
    REQUIRE (mm::presets::PresetManager::fromJson (R"({"formatVersion":1,"name":"x","params":{"lim_drive":3,"no_such_id":5}})", values, name, err));
    CHECK (values.size() == 1);
}

TEST_CASE ("Reference analysis of a 5-minute file takes under 5 seconds", "[reference][performance]")
{
    TempDir tmp;
    const auto f = writeWav (tmp.dir.getChildFile ("ref.wav"), 48000.0, 300.0, 55.0, 0.4f);
    std::atomic<bool> cancel { false };
    juce::String err;
    const auto t0 = std::chrono::steady_clock::now();
    const auto a = mm::reference::analyseFile (f, nullptr, cancel, err);
    const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    INFO ("analysis took " << seconds << " s");
    REQUIRE (a.has_value());
    CHECK (seconds < 5.0);
    CHECK (a->integrated > -30.0);
    CHECK (a->shortTerm.size() > 2900);

    // JSON round trip (cache format)
    const auto back = mm::reference::TrackAnalysis::fromJson (juce::JSON::parse (juce::JSON::toString (a->toJson())));
    REQUIRE (back.has_value());
    CHECK (back->integrated == Approx (a->integrated).margin (0.01));
    CHECK (back->spectrum.size() == a->spectrum.size());

    // a damaged file is reported
    const auto junk = tmp.dir.getChildFile ("junk.wav");
    junk.replaceWithText ("not audio");
    CHECK_FALSE (mm::reference::analyseFile (junk, nullptr, cancel, err).has_value());
    CHECK (err.isNotEmpty());
}

TEST_CASE ("Reference corridor: percentiles with 3+ tracks, fixed 1.5 dB otherwise", "[reference]")
{
    std::vector<mm::reference::TrackAnalysis> tracks (4);
    for (int i = 0; i < 4; ++i)
    {
        tracks[(size_t) i].spectrum.assign (mm::reference::spectrumFreqs().size(), (float) i);
        tracks[(size_t) i].tonal.fill ((double) i);
    }
    std::vector<const mm::reference::TrackAnalysis*> two { &tracks[0], &tracks[1] };
    auto t2 = mm::reference::buildTarget (two);
    CHECK (t2.spectrum[10] == Approx (0.5f));
    CHECK (t2.high[10] - t2.low[10] == Approx (3.0f));
    std::vector<const mm::reference::TrackAnalysis*> four { &tracks[0], &tracks[1], &tracks[2], &tracks[3] };
    auto t4 = mm::reference::buildTarget (four);
    CHECK (t4.spectrum[10] == Approx (1.5f));
    CHECK (t4.low[10] == Approx (0.3f).margin (0.01));
    CHECK (t4.high[10] == Approx (2.7f).margin (0.01));
}

TEST_CASE ("Match EQ proposes limited filters and Undo restores the EQ", "[reference][matcheq]")
{
    const auto& f = mm::reference::spectrumFreqs();
    std::vector<float> ours (f.size(), 0.0f), target (f.size(), 0.0f);
    for (size_t k = 0; k < f.size(); ++k)
        target[k] = (float) (4.0 * std::exp (-std::pow (std::log2 (f[k] / 300.0), 2) / 0.3)); // +4 dB bump at 300 Hz
    const auto s = mm::reference::suggestMatchEq (ours, target, 1.0);
    REQUIRE_FALSE (s.empty());
    CHECK (s.size() <= 8);
    for (const auto& e : s)
    {
        CHECK (std::abs (e.gainDb) <= 3.0);
        CHECK (e.q >= 0.5);
        CHECK (e.q <= 2.0);
        CHECK (e.freq >= 30.0);
        CHECK (e.freq <= 16000.0);
    }
    CHECK (s[0].freq == Approx (300.0).epsilon (0.1));

    mmtest::Host h;
    const auto before = h.get ("eq_b2_freq");
    const auto prev = mm::reference::applyMatchEq (h.apvts, s);
    CHECK (h.get ("eq_on") == 1.0f);
    mm::reference::undoMatchEq (h.apvts, prev);
    CHECK (h.get ("eq_b2_freq") == Approx (before));
    CHECK (h.get ("eq_on") == 0.0f);
}

TEST_CASE ("Album file survives reopening and a concurrent write", "[album]")
{
    TempDir tmp;
    juce::String err;
    mm::album::AlbumFile a, b;
    const auto file = tmp.dir.getChildFile ("Test.mastermind-album");
    REQUIRE (a.create (file, "Test", err));
    mm::reference::TrackAnalysis t1, t2;
    t1.name = "Song 1";
    t1.spectrum.assign (mm::reference::spectrumFreqs().size(), 0.0f);
    t1.integrated = -9.0;
    t1.tonal[1] = 2.0;
    t2 = t1;
    t2.name = "Song 2";
    t2.integrated = -11.0;
    REQUIRE (a.storePassport (t1, err));
    REQUIRE (b.open (file, err));
    juce::Thread::sleep (1100); // file times have a resolution of up to a second
    REQUIRE (b.storePassport (t2, err)); // another instance writes
    a.reloadIfChanged();
    CHECK (a.album().tracks.size() == 2);

    mm::album::AlbumFile c;
    REQUIRE (c.open (file, err));
    CHECK (c.album().tracks.size() == 2);
    const auto target = c.album().target (nullptr);
    CHECK (target.integrated == Approx (-10.0));
    const auto hints = mm::album::albumHints (c.album(), target);
    CHECK (hints.size() == 2);
    CHECK (hints[0].startsWith ("Track 1:"));

    file.replaceWithText ("{ broken");
    mm::album::AlbumFile d;
    CHECK_FALSE (d.open (file, err));
    CHECK (err.isNotEmpty());
}

TEST_CASE ("Generate docs/PARAMETERS.md", "[.docs]")
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("MM_PARAMS_MD", "");
    REQUIRE (path.isNotEmpty());
    juce::String md;
    md << "# MasterMind parameters\n\n"
       << "Generated from `Source/state/Parameters.cpp`. The IDs are frozen since M1 (spec section 9): never rename or remove an ID and never change the meaning of a choice index.\n\n"
       << "| ID | Name | Range or choices | Default | Automatable |\n| --- | --- | --- | --- | --- |\n";
    for (const auto& s : mm::params::all())
    {
        juce::String range;
        if (s.kind == mm::params::Kind::toggle)
            range = "Off / On";
        else if (s.kind == mm::params::Kind::choice)
            range = s.choices.joinIntoString (", ");
        else
            range = mm::params::formatValue (s, s.minValue) + " to " + mm::params::formatValue (s, s.maxValue);
        md << "| `" << s.id << "` | " << s.name << " | " << range << " | " << mm::params::formatValue (s, s.defaultValue) << " | "
           << (s.automatable ? "yes" : "no") << " |\n";
    }
    REQUIRE (juce::File (path).replaceWithText (md));
}
