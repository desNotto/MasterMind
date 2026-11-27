#pragma once

#include "PluginProcessor.h"
#include "state/GlobalSettings.h"
#include "state/WindowSizing.h"
#include "ui/Context.h"
#include "ui/EyesView.h"
#include "ui/MasterView.h"
#include "ui/OtherViews.h"
#include "ui/RefPlayer.h"
#include "ui/ReferencesView.h"
#include "ui/SpectrumPanel.h"
#include "ui/TopBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace mm::ui
{
/** Layout modes with their reference sizes (spec section 8, confirmed by mockup v4). */
enum class LayoutMode
{
    full,
    medium,
    compact
};

LayoutMode layoutModeFor (int width, int height);
juce::Point<int> referenceSize (LayoutMode m);
/** Scale for a window size: the smaller ratio to the mode's reference size, so that text is never
    below 11 px on screen and never letterboxed (the logical size flexes instead). */
float layoutScale (int width, int height);
} // namespace mm::ui

/** The MasterMind window (approved mockup v9). Everything is drawn in reference units of the
    current layout mode and scaled uniformly with the window. */
class MasterMindEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterMindEditor (MasterMindProcessor&);
    ~MasterMindEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    /** For the layout test harness. */
    juce::Component& getRoot() noexcept;
    void showTab (const juce::String& name);
    void forceUpdate() { timerCallback(); }
    void selectModule (int index);
    bool isCompactTab (const juce::String& t) const;

private:
    class Root;
    class TabBar;
    class Footer;

    void timerCallback() override;
    void updateTooltip();
    void notify (const juce::String& message);
    void layoutContent();

    mm::window::Size currentSize() const noexcept { return { getWidth(), getHeight() }; }
    void saveSizeIfChanged();

    MasterMindProcessor& processorRef;
    mm::ui::MasterLookAndFeel lookAndFeel;
    mm::ui::Context ctx;
    std::unique_ptr<Root> root;
    std::unique_ptr<mm::ui::TopBar> topBar;
    std::unique_ptr<TabBar> tabBar;
    std::unique_ptr<Footer> footer;
    std::unique_ptr<mm::ui::SpectrumPanel> spectrumCompact;
    std::unique_ptr<mm::ui::MasterView> master;
    std::unique_ptr<mm::ui::EyesView> eyes;
    std::unique_ptr<mm::ui::ChecksPanel> checksCompact;
    std::unique_ptr<mm::ui::LowEndView> lowEnd;
    std::unique_ptr<mm::ui::StereoView> stereo;
    std::unique_ptr<mm::ui::SpectrogramView> spectrogram;
    std::unique_ptr<mm::ui::AlbumView> albumView;
    std::unique_ptr<mm::ui::ReferencesView> refsView;
    std::unique_ptr<mm::ui::RefPlayer> player;
    mm::ui::TipBubble bubble;
    juce::String currentTab = "Master";
    mm::ui::LayoutMode mode = mm::ui::LayoutMode::full;

    juce::Component* tipSource = nullptr;
    juce::String tipText;
    double tipSince = 0.0;
    bool tipShown = false;
    juce::String toast;
    double toastUntil = 0.0;
    bool lastPlayer = false, lastBypass = false;
    uint64_t lastSerial = 0;
    int frame = 0;

    mm::window::Size lastSavedSize;
    double saveDueAtMs = 0.0;
    bool initialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterMindEditor)
};
