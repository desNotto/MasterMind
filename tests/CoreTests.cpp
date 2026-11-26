#include "state/GlobalSettings.h"
#include "state/WindowSizing.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>
#include <vector>

namespace
{
struct TempDir
{
    TempDir()
        : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getNonexistentChildFile ("mastermind_test", "", false))
    {
        dir.createDirectory();
    }

    ~TempDir() { dir.deleteRecursively(); }

    juce::File dir;
};
} // namespace

TEST_CASE ("Size button toggles 100% and 120%", "[ui][window]")
{
    using namespace mm::window;

    REQUIRE (sizeAfterButtonClick (size100) == size120);
    REQUIRE (sizeAfterButtonClick (size120) == size100);

    // From a custom size the first click returns to 100%, the next goes to 120%.
    const Size custom { 1000, 700 };
    REQUIRE (sizeAfterButtonClick (custom) == size100);
    REQUIRE (sizeAfterButtonClick (sizeAfterButtonClick (custom)) == size120);

    REQUIRE (presetFor (size100) == SizePreset::percent100);
    REQUIRE (presetFor (size120) == SizePreset::percent120);
    REQUIRE (presetFor (custom) == SizePreset::custom);
}

TEST_CASE ("Stored window size is clamped to the work area", "[ui][window]")
{
    using namespace mm::window;

    REQUIRE (clampToWorkArea ({ 5000, 3000 }, { 1920, 1040 }) == Size { 1920, 1040 });
    REQUIRE (clampToWorkArea ({ 1536, 960 }, { 1920, 1040 }) == Size { 1536, 960 });
    REQUIRE (clampToWorkArea ({ 100, 100 }, { 1920, 1040 }) == minimumSize);
    REQUIRE (clampToWorkArea ({ 1280, 800 }, { 0, 0 }) == size100);
    REQUIRE (clampToWorkArea ({ 1280, 800 }, { 400, 200 }) == minimumSize);
}

TEST_CASE ("Window size round-trips through settings.json", "[state][settings]")
{
    TempDir temp;
    const mm::GlobalSettings settings (temp.dir.getChildFile ("sub").getChildFile ("settings.json"));

    REQUIRE_FALSE (settings.loadWindowSize().has_value());

    REQUIRE (settings.saveWindowSize ({ 1536, 960 }));
    REQUIRE (settings.loadWindowSize() == mm::window::Size { 1536, 960 });

    REQUIRE (settings.saveWindowSize ({ 1111, 777 }));
    REQUIRE (settings.loadWindowSize() == mm::window::Size { 1111, 777 });

    // A second reader (another plugin instance) sees the same value.
    const mm::GlobalSettings other (settings.getFile());
    REQUIRE (other.loadWindowSize() == mm::window::Size { 1111, 777 });
}

TEST_CASE ("Corrupt or invalid settings never crash", "[state][settings]")
{
    TempDir temp;
    const auto file = temp.dir.getChildFile ("settings.json");
    const mm::GlobalSettings settings (file);

    const char* bad[] = { "{ this is not json",
                          "   ",
                          "[1, 2, 3]",
                          R"({"window": "big"})",
                          R"({"window": {"width": "wide", "height": 800}})",
                          R"({"window": {"width": 10, "height": 10}})",
                          R"({"window": {"width": 99999999, "height": 800}})" };

    for (const char* text : bad)
    {
        REQUIRE (file.replaceWithText (text));
        REQUIRE_FALSE (settings.loadWindowSize().has_value());
    }

    // Writing over a corrupt file works.
    REQUIRE (file.replaceWithText ("{ broken"));
    REQUIRE (settings.saveWindowSize (mm::window::size100));
    REQUIRE (settings.loadWindowSize() == mm::window::size100);
}

TEST_CASE ("Saving the window size keeps unknown settings", "[state][settings]")
{
    TempDir temp;
    const auto file = temp.dir.getChildFile ("settings.json");
    REQUIRE (file.replaceWithText (R"({"language": "en", "window": {"width": 1280, "height": 800, "extra": 5}})"));

    const mm::GlobalSettings settings (file);
    REQUIRE (settings.saveWindowSize (mm::window::size120));

    juce::var root;
    REQUIRE (juce::JSON::parse (file.loadFileAsString(), root).wasOk());
    REQUIRE (root.getProperty ("language", {}).toString() == "en");
    REQUIRE (static_cast<int> (root.getProperty ("window", {}).getProperty ("extra", {})) == 5);
    REQUIRE (root.getProperty ("window", {}).getProperty ("sizePreset", {}).toString() == "120%");
    REQUIRE (static_cast<int> (root.getProperty ("formatVersion", {})) == mm::GlobalSettings::formatVersion);
}
