#pragma once

#include "PluginProcessor.h"
#include "state/GlobalSettings.h"
#include "state/WindowSizing.h"

#include <juce_gui_basics/juce_gui_basics.h>

/** M0 placeholder editor: free resizing, the 100% / 120% size button, the
    remembered window size and the footer with version, commit, CPU load and
    latency. The real interface follows the approved mockups in M1. */
class MasterMindEditor final : public juce::AudioProcessorEditor,
                               private juce::Timer
{
public:
    explicit MasterMindEditor (MasterMindProcessor&);
    ~MasterMindEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    mm::window::Size currentSize() const noexcept { return { getWidth(), getHeight() }; }
    float uiScale() const noexcept;
    void saveSizeIfChanged();
    juce::String footerLeftText() const;
    juce::String footerRightText() const;

    MasterMindProcessor& processorRef;
    mm::GlobalSettings settings;
    juce::TextButton sizeButton;
    juce::TooltipWindow tooltipWindow { this, 600 };

    juce::Rectangle<int> topBarArea, contentArea, footerArea;
    mm::window::Size lastSavedSize;
    double saveDueAtMs = 0.0;
    bool initialised = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterMindEditor)
};
