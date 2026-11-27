#pragma once

#include "ui/Context.h"

#include <limits>
#include <map>

namespace mm::ui
{
/** References tab (mockup v11): a library folder with reference songs, assignment to the slots 1-6
    of the top bar, removal (to the Windows Recycle Bin) and a drop zone that copies files into the
    folder. Library songs are analysed in the background into the same cache as the slots. */
class ReferencesView final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer, private juce::ChangeListener
{
public:
    explicit ReferencesView (Context& c);
    ~ReferencesView() override;

    void setCompact (bool c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** Library folder (settings.json, key "referenceFolder"). */
    juce::File folder() const;

    struct Item
    {
        juce::File file;
        double seconds = 0.0, lufs = std::numeric_limits<double>::quiet_NaN();
        bool failed = false;
    };
    const std::vector<Item>& items() const { return list; }

private:
    class Table;
    class SlotPanel;
    struct Job;
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void rescan();
    void addFiles (const juce::Array<juce::File>& files);
    void queueAnalysis (const juce::File& f);
    void removeItem (int row);
    void toggleSlot (int row, int slot);
    float progressOf (const juce::File& f) const;

    Context& ctx;
    bool compact = false, dragOver = false;
    std::vector<Item> list;
    std::unique_ptr<Table> table;
    std::unique_ptr<SlotPanel> slots;
    juce::Viewport tableView;
    Btn add { "Add files..." }, change { "Change..." }, open { "Open folder" };
    juce::Rectangle<int> libPanel, pathArea, dropArea, headerArea;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::ThreadPool pool { 1 };
    std::map<juce::String, std::shared_ptr<Job>> jobs;
    std::atomic<bool> cancel { false };
    int scanCounter = 0;
};
} // namespace mm::ui
