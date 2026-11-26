#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <vector>

namespace mm::presets
{
/** One preset in the list (factory presets live in memory, user presets in .mbpreset files). */
struct Entry
{
    juce::String name, category;
    bool factory = false;
    juce::File file;
};

/** Preset management (spec section 9): Factory and User groups, Save, Save As, Rename, Delete,
    Import, Export, a modified marker and atomic writes. Message thread only. */
class PresetManager : public juce::ChangeBroadcaster
{
public:
    enum class Result
    {
        ok,
        invalidName,
        exists,
        factoryReadOnly,
        ioError,
        corrupt
    };

    PresetManager (juce::AudioProcessorValueTreeState& state, juce::File userFolder);

    /** Rebuilds the list (Factory first, then User grouped by subfolder). */
    const std::vector<Entry>& refresh();
    const std::vector<Entry>& entries() const { return list; }

    Result load (const Entry& e, juce::String& error);
    Result loadNext (int direction, juce::String& error);
    Result save (juce::String& error);                                   // overwrite current user preset
    Result saveAs (const juce::String& name, bool overwrite, juce::String& error);
    Result rename (const Entry& e, const juce::String& newName, juce::String& error);
    Result remove (const Entry& e, juce::String& error);
    Result importFile (const juce::File& f, juce::String& error);
    Result exportTo (const juce::File& f, juce::String& error) const;

    juce::String currentName() const { return current.name; }
    bool currentIsFactory() const { return current.factory; }
    bool isMissing() const { return missing; }
    /** True when parameters differ from the loaded preset (the "*" marker). */
    bool isModified() const;

    /** Project state: name, factory flag, file path. */
    juce::ValueTree toState() const;
    void fromState (const juce::ValueTree& v);

    static bool isValidName (const juce::String& name);
    static juce::String toJson (const std::map<juce::String, float>& values, const juce::String& name);
    static bool fromJson (const juce::String& json, std::map<juce::String, float>& values, juce::String& name, juce::String& error);

    std::map<juce::String, float> captureValues() const;
    void applyValues (const std::map<juce::String, float>& values);

    juce::File getUserFolder() const { return folder; }
    static const std::vector<std::pair<juce::String, std::map<juce::String, float>>>& factoryPresets();

private:
    Result write (const juce::File& f, const juce::String& name, juce::String& error) const;

    juce::AudioProcessorValueTreeState& apvts;
    juce::File folder;
    std::vector<Entry> list;
    Entry current;
    std::map<juce::String, float> loadedValues;
    bool missing = false;
};
} // namespace mm::presets
