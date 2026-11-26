#include "state/GlobalSettings.h"

#include "mm_build_info.h"

namespace mm
{
namespace
{
const juce::Identifier keyFormatVersion { "formatVersion" };
const juce::Identifier keyWindow { "window" };
const juce::Identifier keyWidth { "width" };
const juce::Identifier keyHeight { "height" };
const juce::Identifier keySizePreset { "sizePreset" };

std::optional<int> readInt (const juce::var& value)
{
    if (value.isInt() || value.isInt64() || value.isDouble())
    {
        const auto d = static_cast<double> (value);

        if (d >= -1.0e9 && d <= 1.0e9)
            return static_cast<int> (d);
    }

    return std::nullopt;
}
} // namespace

juce::File GlobalSettings::userDataDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile (build::productName);
}

juce::File GlobalSettings::defaultFile()
{
    return userDataDirectory().getChildFile ("settings.json");
}

GlobalSettings::GlobalSettings (juce::File settingsFile) : file (std::move (settingsFile)) {}

juce::var GlobalSettings::readJson() const
{
    if (! file.existsAsFile())
        return {};

    juce::var root;

    if (juce::JSON::parse (file.loadFileAsString(), root).failed())
        return {};

    return root.isObject() ? root : juce::var();
}

bool GlobalSettings::writeJson (const juce::var& root) const
{
    const auto dir = file.getParentDirectory();

    if (! dir.isDirectory() && dir.createDirectory().failed())
        return false;

    juce::TemporaryFile temp (file);

    if (! temp.getFile().replaceWithText (juce::JSON::toString (root)))
        return false;

    return temp.overwriteTargetFileWithTemporary();
}

std::optional<window::Size> GlobalSettings::loadWindowSize() const
{
    const auto root = readJson();
    const auto win = root.getProperty (keyWindow, {});

    if (! win.isObject())
        return std::nullopt;

    const auto w = readInt (win.getProperty (keyWidth, {}));
    const auto h = readInt (win.getProperty (keyHeight, {}));

    if (! w || ! h)
        return std::nullopt;

    if (*w < window::minimumSize.width || *h < window::minimumSize.height
        || *w > window::maximumSize.width || *h > window::maximumSize.height)
        return std::nullopt;

    return window::Size { *w, *h };
}

bool GlobalSettings::saveWindowSize (window::Size size) const
{
    auto root = readJson();

    if (! root.isObject())
        root = juce::var (new juce::DynamicObject());

    auto* rootObject = root.getDynamicObject();
    rootObject->setProperty (keyFormatVersion, formatVersion);

    auto win = root.getProperty (keyWindow, {});

    if (! win.isObject())
        win = juce::var (new juce::DynamicObject());

    auto* winObject = win.getDynamicObject();
    winObject->setProperty (keyWidth, size.width);
    winObject->setProperty (keyHeight, size.height);
    winObject->setProperty (keySizePreset, juce::String (window::presetName (window::presetFor (size))));
    rootObject->setProperty (keyWindow, win);

    return writeJson (root);
}
} // namespace mm
