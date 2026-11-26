#include "dsp/MasterChain.h"
#include "state/GlobalSettings.h"
#include "state/WindowSizing.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>
#include <vector>

namespace
{
void fillNoise (juce::AudioBuffer<float>& buffer, juce::Random& random)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
}

bool bitIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
        return false;

    const auto bytes = sizeof (float) * static_cast<size_t> (a.getNumSamples());

    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), bytes) != 0)
            return false;

    return true;
}

bool isSilent (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (buffer.getSample (ch, i) != 0.0f)
                return false;

    return true;
}

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

TEST_CASE ("MasterChain is a bit-exact passthrough", "[dsp][transparency]")
{
    const double sampleRates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
    const int blockSizes[] = { 1, 32, 64, 512, 2048, 8192 };
    juce::Random random (1234);

    for (const double sr : sampleRates)
    {
        for (const int bs : blockSizes)
        {
            mm::dsp::MasterChain chain;
            chain.prepare (sr, bs, 2);

            juce::AudioBuffer<float> buffer (2, bs);
            juce::AudioBuffer<float> original (2, bs);

            for (int block = 0; block < 4; ++block)
            {
                fillNoise (buffer, random);
                original.makeCopyOf (buffer);
                REQUIRE (chain.process (buffer));
                REQUIRE (bitIdentical (buffer, original));
            }
        }
    }
}

TEST_CASE ("MasterChain is bit-exact with randomly varying block sizes", "[dsp][transparency]")
{
    constexpr int maxBlock = 8192;
    mm::dsp::MasterChain chain;
    chain.prepare (48000.0, maxBlock, 2);
    juce::Random random (99);

    for (int block = 0; block < 200; ++block)
    {
        const int n = 1 + random.nextInt (maxBlock);
        juce::AudioBuffer<float> buffer (2, n);
        fillNoise (buffer, random);
        juce::AudioBuffer<float> original;
        original.makeCopyOf (buffer);
        REQUIRE (chain.process (buffer));
        REQUIRE (bitIdentical (buffer, original));
    }
}

TEST_CASE ("NaN and Inf input produce silence", "[dsp][robustness]")
{
    mm::dsp::MasterChain chain;
    chain.prepare (48000.0, 512, 2);
    juce::Random random (7);

    const float badValues[] = { std::numeric_limits<float>::quiet_NaN(),
                                std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity() };

    for (const float bad : badValues)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        fillNoise (buffer, random);
        buffer.setSample (1, 300, bad);

        REQUIRE (mm::dsp::containsNonFinite (buffer));
        REQUIRE_FALSE (chain.process (buffer));
        REQUIRE (isSilent (buffer));

        // The next clean block passes unchanged again.
        fillNoise (buffer, random);
        juce::AudioBuffer<float> original;
        original.makeCopyOf (buffer);
        REQUIRE (chain.process (buffer));
        REQUIRE (bitIdentical (buffer, original));
    }
}

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
