#include "presets/PresetManager.h"

#include "mm_build_info.h"
#include "state/Parameters.h"

namespace mm::presets
{
namespace
{
constexpr const char* extension = ".mbpreset";

std::map<juce::String, float> defaults()
{
    std::map<juce::String, float> m;
    for (const auto& s : params::all())
        m[s.id] = s.defaultValue;
    return m;
}

} // namespace

const std::vector<std::pair<juce::String, std::map<juce::String, float>>>& PresetManager::factoryPresets()
{
    // Only Init: the user asked to remove every factory preset (D-045, after D-027). User presets stay.
    static const std::vector<std::pair<juce::String, std::map<juce::String, float>>> presets {
        { "Init", defaults() },
    };
    return presets;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, juce::File userFolder)
    : apvts (state), folder (std::move (userFolder))
{
    current.name = "Init";
    current.factory = true;
    loadedValues = defaults();
    refresh();
}

bool PresetManager::isValidName (const juce::String& name)
{
    const auto n = name.trim();
    return n.isNotEmpty() && n.length() <= 64 && ! n.containsAnyOf ("\\/:*?\"<>|") && n == name;
}

std::map<juce::String, float> PresetManager::captureValues() const
{
    std::map<juce::String, float> m;
    for (const auto& s : params::all())
        if (auto* p = apvts.getRawParameterValue (s.id))
            m[s.id] = p->load();
    return m;
}

void PresetManager::applyValues (const std::map<juce::String, float>& values)
{
    for (const auto& s : params::all())
    {
        auto* p = apvts.getParameter (s.id);
        if (p == nullptr)
            continue;
        const auto it = values.find (s.id);
        const float v = it != values.end() ? juce::jlimit (s.minValue, s.maxValue, it->second) : s.defaultValue; // missing IDs take defaults
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

juce::String PresetManager::toJson (const std::map<juce::String, float>& values, const juce::String& name)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("formatVersion", 1);
    o->setProperty ("pluginVersion", mm::build::version);
    o->setProperty ("name", name);
    auto* p = new juce::DynamicObject();
    for (const auto& [id, v] : values)
        p->setProperty (id, v);
    o->setProperty ("params", juce::var (p));
    return juce::JSON::toString (juce::var (o));
}

bool PresetManager::fromJson (const juce::String& json, std::map<juce::String, float>& values, juce::String& name, juce::String& error)
{
    juce::var v;
    const auto r = juce::JSON::parse (json, v);
    if (r.failed() || ! v.isObject())
    {
        error = "The preset file is damaged (not valid JSON).";
        return false;
    }
    if ((int) v.getProperty ("formatVersion", 0) < 1)
    {
        error = "The preset file has no formatVersion.";
        return false;
    }
    name = v.getProperty ("name", "").toString();
    values.clear();
    if (auto* obj = v.getProperty ("params", {}).getDynamicObject())
        for (const auto& prop : obj->getProperties())
            if (params::find (prop.name.toString()) != nullptr) // unknown IDs are ignored
                values[prop.name.toString()] = (float) (double) prop.value;
    return true;
}

const std::vector<Entry>& PresetManager::refresh()
{
    list.clear();
    for (const auto& [name, values] : factoryPresets())
    {
        Entry e;
        e.name = name;
        e.category = "Factory";
        e.factory = true;
        list.push_back (e);
    }
    folder.createDirectory();
    auto files = folder.findChildFiles (juce::File::findFiles, true, juce::String ("*") + extension);
    files.sort();
    for (const auto& f : files)
    {
        Entry e;
        e.name = f.getFileNameWithoutExtension();
        const auto rel = f.getParentDirectory().getRelativePathFrom (folder);
        e.category = (rel == "." || rel.isEmpty()) ? juce::String ("User") : "User/" + rel.replaceCharacter ('\\', '/');
        e.file = f;
        list.push_back (e);
    }
    return list;
}

PresetManager::Result PresetManager::load (const Entry& e, juce::String& error)
{
    std::map<juce::String, float> values;
    if (e.factory)
    {
        for (const auto& [name, v] : factoryPresets())
            if (name == e.name)
                values = v;
    }
    else
    {
        juce::String name;
        if (! e.file.existsAsFile())
        {
            error = "The preset file does not exist any more.";
            return Result::ioError;
        }
        if (! fromJson (e.file.loadFileAsString(), values, name, error))
            return Result::corrupt;
    }
    applyValues (values);
    current = e;
    loadedValues = captureValues();
    missing = false;
    sendChangeMessage();
    return Result::ok;
}

PresetManager::Result PresetManager::loadNext (int direction, juce::String& error)
{
    refresh();
    if (list.empty())
        return Result::ioError;
    int index = 0;
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == current.name && list[(size_t) i].factory == current.factory)
            index = i;
    index = (index + direction + (int) list.size()) % (int) list.size();
    return load (list[(size_t) index], error);
}

PresetManager::Result PresetManager::write (const juce::File& f, const juce::String& name, juce::String& error) const
{
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);
    if (! tmp.getFile().replaceWithText (toJson (captureValues(), name)) || ! tmp.overwriteTargetFileWithTemporary())
    {
        error = "Cannot write " + f.getFullPathName();
        return Result::ioError;
    }
    return Result::ok;
}

PresetManager::Result PresetManager::save (juce::String& error)
{
    if (current.factory || current.file == juce::File())
    {
        error = "Factory presets are read-only. Use Save As.";
        return Result::factoryReadOnly;
    }
    const auto r = write (current.file, current.name, error);
    if (r == Result::ok)
    {
        loadedValues = captureValues();
        missing = false;
        sendChangeMessage();
    }
    return r;
}

PresetManager::Result PresetManager::saveAs (const juce::String& name, bool overwrite, juce::String& error)
{
    if (! isValidName (name))
    {
        error = "Use 1 to 64 characters and none of \\ / : * ? \" < > |";
        return Result::invalidName;
    }
    const auto f = folder.getChildFile (name + extension);
    if (f.existsAsFile() && ! overwrite)
    {
        error = "A preset with this name exists.";
        return Result::exists;
    }
    const auto r = write (f, name, error);
    if (r != Result::ok)
        return r;
    current = Entry { name, "User", false, f };
    loadedValues = captureValues();
    missing = false;
    refresh();
    sendChangeMessage();
    return Result::ok;
}

PresetManager::Result PresetManager::rename (const Entry& e, const juce::String& newName, juce::String& error)
{
    if (e.factory)
    {
        error = "Factory presets cannot be renamed.";
        return Result::factoryReadOnly;
    }
    if (! isValidName (newName))
    {
        error = "Use 1 to 64 characters and none of \\ / : * ? \" < > |";
        return Result::invalidName;
    }
    const auto target = e.file.getParentDirectory().getChildFile (newName + extension);
    if (target.existsAsFile())
    {
        error = "A preset with this name exists.";
        return Result::exists;
    }
    std::map<juce::String, float> values;
    juce::String oldName;
    if (! fromJson (e.file.loadFileAsString(), values, oldName, error))
        return Result::corrupt;
    juce::TemporaryFile tmp (target);
    if (! tmp.getFile().replaceWithText (toJson (values, newName)) || ! tmp.overwriteTargetFileWithTemporary() || ! e.file.deleteFile())
    {
        error = "Cannot rename the preset file.";
        return Result::ioError;
    }
    if (! current.factory && current.file == e.file)
    {
        current.name = newName;
        current.file = target;
    }
    refresh();
    sendChangeMessage();
    return Result::ok;
}

PresetManager::Result PresetManager::remove (const Entry& e, juce::String& error)
{
    if (e.factory)
    {
        error = "Factory presets cannot be deleted.";
        return Result::factoryReadOnly;
    }
    if (! e.file.deleteFile())
    {
        error = "Cannot delete the preset file.";
        return Result::ioError;
    }
    if (! current.factory && current.file == e.file)
    {
        // deleting the loaded preset keeps the current sound and marks it as unsaved
        current.file = juce::File();
        loadedValues.clear();
    }
    refresh();
    sendChangeMessage();
    return Result::ok;
}

PresetManager::Result PresetManager::importFile (const juce::File& f, juce::String& error)
{
    std::map<juce::String, float> values;
    juce::String name;
    if (! fromJson (f.loadFileAsString(), values, name, error))
        return Result::corrupt;
    if (! isValidName (name))
        name = f.getFileNameWithoutExtension();
    auto target = folder.getChildFile (name + extension);
    if (target.existsAsFile())
        target = target.getNonexistentSibling (true);
    juce::TemporaryFile tmp (target);
    if (! tmp.getFile().replaceWithText (toJson (values, target.getFileNameWithoutExtension())) || ! tmp.overwriteTargetFileWithTemporary())
    {
        error = "Cannot copy the preset.";
        return Result::ioError;
    }
    refresh();
    return load (Entry { target.getFileNameWithoutExtension(), "User", false, target }, error);
}

PresetManager::Result PresetManager::exportTo (const juce::File& f, juce::String& error) const
{
    return write (f, current.name, error);
}

bool PresetManager::isModified() const
{
    if (loadedValues.empty())
        return true;
    for (const auto& s : params::all())
    {
        const auto it = loadedValues.find (s.id);
        auto* p = apvts.getRawParameterValue (s.id);
        if (it == loadedValues.end() || p == nullptr)
            continue;
        if (std::abs (p->load() - it->second) > 1.0e-4f * std::max (1.0f, std::abs (s.maxValue - s.minValue)))
            return true;
    }
    return false;
}

juce::ValueTree PresetManager::toState() const
{
    juce::ValueTree v ("Preset");
    v.setProperty ("name", current.name, nullptr);
    v.setProperty ("factory", current.factory, nullptr);
    v.setProperty ("file", current.file.getFullPathName(), nullptr);
    v.setProperty ("modified", isModified(), nullptr);
    return v;
}

void PresetManager::fromState (const juce::ValueTree& v)
{
    if (! v.isValid())
        return;
    current.name = v.getProperty ("name", "Init").toString();
    current.factory = v.getProperty ("factory", true);
    const juce::String path = v.getProperty ("file", "");
    current.file = path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    missing = ! current.factory && ! current.file.existsAsFile();
    // the stored parameter values are used; the preset file is only compared for the "*" marker
    loadedValues.clear();
    if (current.factory)
    {
        for (const auto& [name, values] : factoryPresets())
            if (name == current.name)
                loadedValues = values;
    }
    else if (! missing)
    {
        juce::String n, err;
        fromJson (current.file.loadFileAsString(), loadedValues, n, err);
    }
    if (! (bool) v.getProperty ("modified", false) && loadedValues.empty())
        loadedValues = captureValues();
    sendChangeMessage();
}
} // namespace mm::presets
