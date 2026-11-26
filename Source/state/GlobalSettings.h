#pragma once

#include "state/WindowSizing.h"

#include <juce_core/juce_core.h>

#include <optional>

namespace mm
{
/** Global user settings in %APPDATA%\<product>\settings.json (spec section 9).
    Used on the message thread only. Unknown keys are preserved on write,
    writes are atomic, and a missing or corrupt file never throws. */
class GlobalSettings
{
public:
    static constexpr int formatVersion = 1;

    /** %APPDATA%\<product>\ */
    static juce::File userDataDirectory();

    /** %APPDATA%\<product>\settings.json */
    static juce::File defaultFile();

    explicit GlobalSettings (juce::File settingsFile);

    const juce::File& getFile() const noexcept { return file; }

    /** The last stored window size, or nothing if the file is missing,
        corrupt or holds values out of range. */
    std::optional<window::Size> loadWindowSize() const;

    /** Stores the window size and its preset name. Returns false if the file
        could not be written. */
    bool saveWindowSize (window::Size size) const;

    /** Generic top-level value (language, thresholds, analysis defaults ...). */
    juce::var get (const juce::String& key, const juce::var& fallback = {}) const;
    bool set (const juce::String& key, const juce::var& value) const;

private:
    juce::var readJson() const;
    bool writeJson (const juce::var& root) const;

    juce::File file;
};
} // namespace mm
