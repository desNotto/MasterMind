// Loads the shipped flat .vst3 file the way a host does and checks that the
// default state is a bit-exact stereo passthrough delayed by the reported latency (spec section 10,
// "Transparency"). Usage: MasterMindBinaryTest <path to .vst3>

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstring>
#include <iostream>
#include <vector>

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

/** Streams noise through the plugin in blocks of the given sizes and checks that the output is the
    input delayed by exactly the reported latency, bit for bit (default state). */
bool streamAndCompare (juce::AudioPluginInstance& plugin, const std::vector<int>& blocks, juce::Random& random, int& latency)
{
    int total = 0;
    for (int b : blocks)
        total += b;
    juce::AudioBuffer<float> input (2, total), output (2, total);
    fillNoise (input, random);
    juce::MidiBuffer midi;
    int pos = 0;
    for (int b : blocks)
    {
        juce::AudioBuffer<float> block (2, b);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom (ch, 0, input, ch, pos, b);
        plugin.processBlock (block, midi);
        for (int ch = 0; ch < 2; ++ch)
            output.copyFrom (ch, pos, block, ch, 0, b);
        pos += b;
    }
    latency = plugin.getLatencySamples();
    if (latency < 0 || latency >= total)
        return false;
    for (int ch = 0; ch < 2; ++ch)
        if (std::memcmp (output.getReadPointer (ch) + latency, input.getReadPointer (ch), sizeof (float) * (size_t) (total - latency)) != 0)
            return false;
    return true;
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

    juce::Random random (42);
    int latency = 0;

    for (const int bs : blockSizes)
    {
        plugin->prepareToPlay (sampleRate, bs);
        const std::vector<int> blocks ((size_t) std::max (8, 40000 / bs + 1), bs);
        if (! streamAndCompare (*plugin, blocks, random, latency))
            fail ("output is not the delayed input at " + juce::String (sampleRate) + " Hz, block " + juce::String (bs) + " (latency " + juce::String (latency) + ")");
        plugin->releaseResources();
    }

    // Randomly varying block sizes within the prepared maximum.
    plugin->prepareToPlay (sampleRate, maxBlock);
    std::vector<int> blocks;
    for (int block = 0; block < 64; ++block)
        blocks.push_back (1 + random.nextInt (maxBlock));
    if (! streamAndCompare (*plugin, blocks, random, latency))
        fail ("output is not the delayed input at " + juce::String (sampleRate) + " Hz with varying blocks");

    plugin->releaseResources();
    std::cout << "checked " << sampleRate << " Hz, latency " << latency << " samples" << std::endl;
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
