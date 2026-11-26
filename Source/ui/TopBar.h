#pragma once

#include "ui/Context.h"

namespace mm::ui
{
/** Top bar (mockup v4): logo, presets, Bypass/Match, monitoring, references with A/B, meters and
    the size button. In compact mode it shows a reduced set. */
class TopBar final : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener
{
public:
    explicit TopBar (Context& c);
    ~TopBar() override;

    void setCompact (bool c);
    void setMedium (bool m) { medium = m; setCompact (compact); }
    void update(); // called by the editor timer
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    std::function<void()> onSizeButton;
    void setSizeLabel (const juce::String& s) { sizeBtn.setButtonText (s); }

private:
    class RefButton;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void showPresetMenu();
    void showMonitorMenu();
    void showSystemMenu();
    void showRefMenu (int slot);
    void saveAs();
    void selectRef (int slot);
    void loadRefFile (int slot);

    Context& ctx;
    bool compact = false, medium = false;
    Btn prev { juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xb9")) }, next { juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xba")) };
    Btn presetBox { "Init" }, save { "Save" }, saveAsBtn { "Save As" };
    Btn bypass { "Bypass", true }, match { "Match", true };
    Btn monitorBox { "Monitor: Off" }, systemBox { "System: Off" };
    std::vector<std::unique_ptr<RefButton>> refButtons;
    Segmented ab { { "A", "B" } };
    Btn sizeBtn { "100%" };
    juce::Rectangle<int> logoArea, presetGroup, abGroup, monGroup, refGroup, meterArea;
    std::vector<monitoring::SystemPreset> systems;
    juce::StringArray systemErrors;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace mm::ui
