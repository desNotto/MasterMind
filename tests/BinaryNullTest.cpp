// Loads the shipped flat .vst3 file the way a host does and checks that the
// default state is a bit-exact stereo passthrough (spec section 10,
// "Transparency"). Usage: MasterMindBinaryTest <path to .vst3>

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstring>
#include <iostream>

namespace
{
int failures = 0;

void fail (const juce::String& message)
{
    ++failures;
    std::cerr << "FAIL: " << message << std::endl;
}

void fillNoise (juce::AudioBuffer<float>& buffer, juce::Random& random)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample (ch, i, random.nextFloat() * 2.0f - 1.0f);
}

bool bitIdentical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    const auto bytes = sizeof (float) * static_cast<size_t> (a.getNumSamples());

    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), bytes) != 0)
            return false;

    return true;
}

bool processAndCompare (juce::AudioPluginInstance& plugin, int numSamples, juce::Random& random)
{
    juce::AudioBuffer<float> buffer (2, numSamples);
    juce::AudioBuffer<float> original (2, numSamples);
    juce::MidiBuffer midi;

    fillNoise (buffer, random);
    original.makeCopyOf (buffer);
    plugin.processBlock (buffer, midi);
    return bitIdentical (buffer, original);
}

void testAtSampleRate (juce::AudioPluginFormat& format, const juce::PluginDescription& description, double sampleRate)
{
    const int blockSizes[] = { 1, 32, 64, 512, 2048, 8192 };
    constexpr int maxBlock = 8192;

    juce::String error;
    auto plugin = format.createInstanceFromDescription (description, sampleRate, maxBlock, error);

    if (plugin == nullptr)
    {
        fail ("could not create plugin instance: " + error);
        return;
    }

    if (plugin->getTotalNumInputChannels() != 2 || plugin->getTotalNumOutputChannels() != 2)
    {
        fail ("plugin is not stereo in / stereo out");
        return;
    }

    if (plugin->getLatencySamples() != 0)
        fail ("unexpected latency " + juce::String (plugin->getLatencySamples()));

    juce::Random random (42);

    for (const int bs : blockSizes)
    {
        plugin->prepareToPlay (sampleRate, bs);

        for (int block = 0; block < 8; ++block)
        {
            if (! processAndCompare (*plugin, bs, random))
            {
                fail ("output differs from input at " + juce::String (sampleRate) + " Hz, block " + juce::String (bs));
                break;
            }
        }

        plugin->releaseResources();
    }

    // Randomly varying block sizes within the prepared maximum.
    plugin->prepareToPlay (sampleRate, maxBlock);

    for (int block = 0; block < 64; ++block)
    {
        const int n = 1 + random.nextInt (maxBlock);

        if (! processAndCompare (*plugin, n, random))
        {
            fail ("output differs from input at " + juce::String (sampleRate) + " Hz, varying block " + juce::String (n));
            break;
        }
    }

    plugin->releaseResources();
    std::cout << "checked " << sampleRate << " Hz" << std::endl;
}
} // namespace

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc < 2)
    {
        std::cerr << "usage: MasterMindBinaryTest <path to .vst3>" << std::endl;
        return 2;
    }

    const juce::File pluginFile (juce::String::fromUTF8 (argv[1]));

    if (! pluginFile.existsAsFile())
    {
        std::cerr << "FAIL: not a single file: " << pluginFile.getFullPathName() << std::endl;
        return 1;
    }

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, pluginFile.getFullPathName());

    if (types.isEmpty())
    {
        std::cerr << "FAIL: no VST3 plugin found in " << pluginFile.getFullPathName() << std::endl;
        return 1;
    }

    std::cout << "loaded " << types[0]->name << " " << types[0]->version << " by " << types[0]->manufacturerName
              << std::endl;

    for (const double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        testAtSampleRate (format, *types[0], sr);

    if (failures > 0)
    {
        std::cerr << failures << " failure(s)" << std::endl;
        return 1;
    }

    std::cout << "binary null test passed" << std::endl;
    return 0;
}
