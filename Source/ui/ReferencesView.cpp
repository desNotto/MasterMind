#include "ui/ReferencesView.h"
#include "ui/Locale.h"

#include "reference/TrackAnalysis.h"

namespace mm::ui
{
namespace
{
const char* const audioWildcard = "*.wav;*.flac;*.mp3;*.ogg;*.aif;*.aiff";
constexpr int rowH = 34;

juce::String timeText (double s)
{
    if (s <= 0.0)
        return juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
    const int t = juce::roundToInt (s);
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}

/** Columns of the library table (x positions from the right edge). */
struct Columns
{
    int name, length, lufs, analysis, slots, del;
    static Columns forWidth (int w, bool compact)
    {
        Columns c {};
        c.del = w - 28;
        c.slots = c.del - 6 * 25 - 8;
        c.analysis = c.slots - (compact ? 40 : 126);
        c.lufs = c.analysis - 70;
        c.length = compact ? c.lufs : c.lufs - 64;
        c.name = 0;
        return c;
    }
};
} // namespace

struct ReferencesView::Job
{
    std::atomic<float> progress { 0.0f };
    std::atomic<int> state { 0 }; // 0 queued, 1 copying, 2 analysing, 3 done, 4 failed
};

// ==================================================================================================
class ReferencesView::Table final : public juce::Component
{
public:
    explicit Table (ReferencesView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        const auto cols = Columns::forWidth (getWidth(), owner.compact);
        const auto& items = owner.list;
        for (int i = 0; i < (int) items.size(); ++i)
        {
            const auto& it = items[(size_t) i];
            const juce::Rectangle<int> row (0, i * rowH, getWidth(), rowH);
            if (i == selected)
            {
                g.setColour (juce::Colour (0xff1f2633));
                g.fillRect (row);
            }
            g.setColour (juce::Colour (0xff1f232b));
            g.drawHorizontalLine (row.getBottom() - 1, 0.0f, (float) getWidth());
            g.setColour (col::text);
            g.setFont (semibold (13.0f));
            g.drawText (it.file.getFileName(), row.withRight (cols.length - 8).withTrimmedLeft (4), juce::Justification::centredLeft, true);
            g.setFont (font (13.0f, true));
            if (! owner.compact)
                g.drawText (timeText (it.seconds), juce::Rectangle<int> (cols.length, row.getY(), 54, rowH), juce::Justification::centredRight);
            g.drawText (std::isnan (it.lufs) ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : fmtMinus (it.lufs), juce::Rectangle<int> (cols.lufs, row.getY(), 56, rowH), juce::Justification::centredRight);
            // analysis state
            auto st = juce::Rectangle<int> (cols.analysis + 6, row.getY(), cols.slots - cols.analysis - 10, rowH);
            const float p = owner.progressOf (it.file);
            const bool ready = ! std::isnan (it.lufs);
            const auto dotCol = it.failed ? col::bad : (ready ? col::ok : col::warn);
            g.setColour (dotCol);
            g.fillEllipse (juce::Rectangle<float> ((float) st.getX(), st.getCentreY() - 4.0f, 8.0f, 8.0f));
            if (! owner.compact)
            {
                g.setColour (col::text2);
                g.setFont (font (12.5f));
                auto t = st.withTrimmedLeft (14);
                const auto label = it.failed ? juce::String ("failed") : (ready ? juce::String ("ready") : juce::String ("analysing"));
                g.drawText (label, t, juce::Justification::centredLeft);
                if (! ready && ! it.failed)
                {
                    auto bar = juce::Rectangle<float> ((float) t.getX() + 66.0f, (float) t.getCentreY() - 2.0f, 40.0f, 4.0f);
                    g.setColour (col::sunk);
                    g.fillRoundedRectangle (bar, 2.0f);
                    g.setColour (col::warn);
                    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, p)), 2.0f);
                }
            }
            // slot chips
            for (int k = 0; k < 6; ++k)
            {
                const auto chip = chipRect (cols, i, k);
                const bool on = owner.ctx.refs.slot (k).file == it.file && owner.ctx.refs.slot (k).state != reference::ReferenceLibrary::State::empty;
                const bool busy = ! on && owner.ctx.refs.slot (k).state != reference::ReferenceLibrary::State::empty;
                g.setColour (on ? col::magenta : col::panel2);
                g.fillRoundedRectangle (chip, 4.0f);
                if (busy)
                {
                    juce::Path p2, dashed;
                    p2.addRoundedRectangle (chip.reduced (0.5f), 4.0f);
                    const float dash[] = { 2.0f, 2.0f };
                    juce::PathStrokeType (1.0f).createDashedStroke (dashed, p2, dash, 2);
                    g.setColour (col::line2);
                    g.fillPath (dashed);
                }
                else
                {
                    g.setColour (on ? col::magenta : col::line2);
                    g.drawRoundedRectangle (chip.reduced (0.5f), 4.0f, 1.0f);
                }
                g.setColour (on ? juce::Colours::white : col::text3);
                g.setFont (font (12.5f, true));
                g.drawText (juce::String (k + 1), chip, juce::Justification::centred);
            }
            g.setColour (hoverDel == i ? col::bad : col::text3);
            g.setFont (font (13.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")), delRect (cols, i), juce::Justification::centred);
        }
        if (items.empty())
        {
            g.setColour (col::text3);
            g.setFont (font (13.0f));
            g.drawText (tr ("The library is empty. Drop files below or press Add files."), getLocalBounds().removeFromTop (60), juce::Justification::centred);
        }
    }

    static juce::Rectangle<float> chipRect (const Columns& c, int row, int k) { return { (float) (c.slots + k * 25), row * rowH + (rowH - 22) / 2.0f, 22.0f, 22.0f }; }
    static juce::Rectangle<int> delRect (const Columns& c, int row) { return { c.del, row * rowH + (rowH - 22) / 2, 24, 22 }; }

    int rowAt (int y) const { return y / rowH; }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto cols = Columns::forWidth (getWidth(), owner.compact);
        const int row = rowAt (e.y);
        juce::String tip;
        int del = -1;
        if (row >= 0 && row < (int) owner.list.size())
        {
            for (int k = 0; k < 6; ++k)
                if (chipRect (cols, row, k).contains (e.position))
                    tip = "c:lib.slot:" + juce::String (k + 1);
            if (delRect (cols, row).contains (e.getPosition()))
            {
                tip = "c:lib.del";
                del = row;
            }
        }
        setTip (*this, tip);
        setMouseCursor (tip.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        if (del != hoverDel)
        {
            hoverDel = del;
            repaint();
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto cols = Columns::forWidth (getWidth(), owner.compact);
        const int row = rowAt (e.y);
        if (row < 0 || row >= (int) owner.list.size())
            return;
        selected = row;
        for (int k = 0; k < 6; ++k)
            if (chipRect (cols, row, k).contains (e.position))
                owner.toggleSlot (row, k);
        if (delRect (cols, row).contains (e.getPosition()))
            owner.removeItem (row);
        repaint();
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hoverDel = -1;
        repaint();
    }

    ReferencesView& owner;
    int selected = -1, hoverDel = -1;
};

// ==================================================================================================
class ReferencesView::SlotPanel final : public juce::Component
{
public:
    explicit SlotPanel (ReferencesView& o) : owner (o)
    {
        for (int i = 0; i < 6; ++i)
        {
            auto in = std::make_unique<Btn> ("In set", true);
            in->setClickingTogglesState (false);
            in->onClick = [this, i] { owner.ctx.refs.setInSet (i, ! owner.ctx.refs.slot (i).inSet); };
            setTip (*in, "c:lib.inset");
            addChildComponent (*in);
            auto clear = std::make_unique<Btn> (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")));
            clear->onClick = [this, i] { owner.ctx.refs.clear (i); };
            setTip (*clear, "c:lib.clear");
            addChildComponent (*clear);
            inSet.push_back (std::move (in));
            clears.push_back (std::move (clear));
        }
    }

    juce::Rectangle<int> rowRect (int i) const { return { 10, 42 + i * 50, getWidth() - 20, 44 }; }

    void refresh()
    {
        for (int i = 0; i < 6; ++i)
        {
            const bool full = owner.ctx.refs.slot (i).state != reference::ReferenceLibrary::State::empty;
            inSet[(size_t) i]->setVisible (full);
            clears[(size_t) i]->setVisible (full);
            inSet[(size_t) i]->setToggleState (owner.ctx.refs.slot (i).inSet, juce::dontSendNotification);
        }
        repaint();
    }

    void resized() override
    {
        for (int i = 0; i < 6; ++i)
        {
            auto r = rowRect (i).reduced (8, 0);
            clears[(size_t) i]->setBounds (r.removeFromRight (26).withSizeKeepingCentre (26, 24));
            r.removeFromRight (8);
            inSet[(size_t) i]->setBounds (r.removeFromRight (64).withSizeKeepingCentre (64, 24));
        }
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat());
        paintPanelHeader (g, getLocalBounds(), tr ("Slots 1-6"), tr ("buttons 1-6 in the top bar"));
        int inSetCount = 0;
        for (int i = 0; i < 6; ++i)
        {
            const auto& s = owner.ctx.refs.slot (i);
            const bool full = s.state != reference::ReferenceLibrary::State::empty;
            inSetCount += full && s.inSet && s.state == reference::ReferenceLibrary::State::ready ? 1 : 0;
            auto r = rowRect (i);
            g.setColour (col::panel2);
            g.fillRoundedRectangle (r.toFloat(), 7.0f);
            g.setColour (col::line);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, 1.0f);
            auto in = r.reduced (8, 0);
            const auto chip = in.removeFromLeft (24).withSizeKeepingCentre (24, 24).toFloat();
            g.setColour (full ? col::magenta : col::panel);
            g.fillRoundedRectangle (chip, 5.0f);
            g.setColour (full ? col::magenta : col::line2);
            g.drawRoundedRectangle (chip.reduced (0.5f), 5.0f, 1.0f);
            g.setColour (full ? juce::Colours::white : col::text2);
            g.setFont (font (13.0f, true));
            g.drawText (juce::String (i + 1), chip, juce::Justification::centred);
            in.removeFromLeft (10);
            if (full)
                in.removeFromRight (106);
            if (! full)
            {
                g.setColour (col::text3);
                g.setFont (font (12.5f));
                g.drawText (tr ("empty: click a number in the list"), in, juce::Justification::centredLeft, true);
                continue;
            }
            g.setColour (col::text);
            g.setFont (semibold (13.0f));
            g.drawText (s.file.getFileName(), in.withTrimmedBottom (in.getHeight() / 2 - 2), juce::Justification::bottomLeft, true);
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            juce::String info;
            if (s.state == reference::ReferenceLibrary::State::analysing)
                info = "analysing " + juce::String (juce::roundToInt (s.progress * 100.0f)) + "%";
            else if (s.state == reference::ReferenceLibrary::State::failed)
                info = "failed: " + s.error;
            else if (s.analysis.has_value())
                info = timeText (s.analysis->durationSeconds) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + fmtMinus (s.analysis->integrated) + " LUFS";
            g.drawText (info, in.withTrimmedTop (in.getHeight() / 2 + 2), juce::Justification::topLeft, true);
        }
        auto note = juce::Rectangle<int> (12, rowRect (5).getBottom() + 10, getWidth() - 24, 48);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawFittedText (tr ("Songs with In set make the target and the corridor for the spectrum and the tonal balance. Now:") + " " + juce::String (inSetCount), note, juce::Justification::topLeft, 3);
    }

    ReferencesView& owner;
    std::vector<std::unique_ptr<Btn>> inSet, clears;
};

// ==================================================================================================
ReferencesView::ReferencesView (Context& c) : ctx (c)
{
    table = std::make_unique<Table> (*this);
    slots = std::make_unique<SlotPanel> (*this);
    addAndMakeVisible (tableView);
    tableView.setViewedComponent (table.get(), false);
    tableView.setScrollBarsShown (true, false);
    addAndMakeVisible (*slots);
    for (auto* b : { &add, &change, &open })
        addAndMakeVisible (*b);
    setTip (add, "c:lib.add");
    setTip (change, "c:lib.folder");
    setTip (open, "c:lib.open");
    add.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Add reference files", juce::File(), audioWildcard, true, false, getTopLevelComponent());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc) { addFiles (fc.getResults()); });
    };
    change.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Reference library folder", folder(), {}, true, false, getTopLevelComponent());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
            {
                ctx.settings.set ("referenceFolder", fc.getResult().getFullPathName());
                rescan();
                repaint();
            }
        });
    };
    open.onClick = [this]
    {
        folder().createDirectory();
        folder().startAsProcess();
    };
    ctx.refs.addChangeListener (this);
}

ReferencesView::~ReferencesView()
{
    stopTimer();
    ctx.refs.removeChangeListener (this);
    cancel.store (true);
    pool.removeAllJobs (true, 10000);
    tableView.setViewedComponent (nullptr, false);
}

juce::File ReferencesView::folder() const
{
    const auto stored = ctx.settings.get ("referenceFolder", {}).toString();
    if (stored.isNotEmpty() && juce::File::isAbsolutePath (stored))
        return juce::File (stored);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MasterMind References");
}

void ReferencesView::setCompact (bool c)
{
    compact = c;
    slots->setVisible (! c);
    resized();
}

void ReferencesView::visibilityChanged()
{
    if (isVisible())
    {
        rescan();
        slots->refresh();
        startTimerHz (2);
    }
    else
        stopTimer();
}

void ReferencesView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    slots->refresh();
    table->repaint();
}

void ReferencesView::timerCallback()
{
    if (++scanCounter % 6 == 0)
        rescan();
    bool changed = false;
    for (auto& it : list)
        if (std::isnan (it.lufs) && ! it.failed)
        {
            const auto cache = ctx.refs.cacheFileFor (it.file);
            if (cache.existsAsFile())
            {
                juce::var v;
                if (juce::JSON::parse (cache.loadFileAsString(), v).wasOk())
                {
                    it.lufs = v.getProperty ("integrated", std::numeric_limits<double>::quiet_NaN());
                    changed = true;
                }
            }
            else if (const auto j = jobs.find (it.file.getFullPathName()); j != jobs.end() && j->second->state.load() == 4)
                it.failed = changed = true;
        }
    juce::ignoreUnused (changed);
    table->repaint();
}

void ReferencesView::rescan()
{
    const auto dir = folder();
    juce::Array<juce::File> files;
    if (dir.isDirectory())
        files = dir.findChildFiles (juce::File::findFiles, false, audioWildcard, juce::File::FollowSymlinks::no);
    files.sort();
    std::vector<Item> next;
    for (const auto& f : files)
    {
        auto old = std::find_if (list.begin(), list.end(), [&] (const Item& it) { return it.file == f; });
        if (old != list.end())
        {
            next.push_back (*old);
            continue;
        }
        Item it;
        it.file = f;
        if (std::unique_ptr<juce::AudioFormatReader> r { reference::formatManager().createReaderFor (f) }; r != nullptr && r->sampleRate > 0)
            it.seconds = (double) r->lengthInSamples / r->sampleRate;
        const auto cache = ctx.refs.cacheFileFor (f);
        juce::var v;
        if (cache.existsAsFile() && juce::JSON::parse (cache.loadFileAsString(), v).wasOk())
            it.lufs = v.getProperty ("integrated", std::numeric_limits<double>::quiet_NaN());
        else
            queueAnalysis (f);
        next.push_back (it);
    }
    list = std::move (next);
    table->setSize (std::max (1, tableView.getWidth() - tableView.getScrollBarThickness()), std::max (tableView.getHeight(), (int) list.size() * rowH));
    repaint();
}

void ReferencesView::queueAnalysis (const juce::File& f)
{
    const auto key = f.getFullPathName();
    if (jobs.count (key) > 0)
        return;
    auto job = std::make_shared<Job>();
    jobs[key] = job;
    const auto cacheFile = ctx.refs.cacheFileFor (f);
    pool.addJob ([this, job, f, cacheFile]
    {
        job->state = 2;
        juce::String error;
        const auto result = reference::analyseFile (f, [job] (float p) { job->progress = p; }, cancel, error);
        if (result)
        {
            cacheFile.getParentDirectory().createDirectory();
            juce::TemporaryFile tmp (cacheFile);
            if (tmp.getFile().replaceWithText (juce::JSON::toString (result->toJson(), true)))
                tmp.overwriteTargetFileWithTemporary();
            job->state = 3;
        }
        else
            job->state = 4;
    });
}

void ReferencesView::addFiles (const juce::Array<juce::File>& files)
{
    const auto dir = folder();
    if (! dir.createDirectory())
    {
        if (ctx.notify)
            ctx.notify ("Cannot create the library folder. Choose another folder.");
        return;
    }
    int copied = 0;
    for (const auto& f : files)
    {
        if (! f.existsAsFile() || ! f.hasFileExtension ("wav;flac;mp3;ogg;aif;aiff"))
            continue;
        if (f.getParentDirectory() == dir)
            continue;
        auto target = dir.getChildFile (f.getFileName());
        if (target.existsAsFile() && target.getSize() == f.getSize())
            continue; // already in the library
        target = target.existsAsFile() ? dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), f.getFileExtension(), false) : target;
        auto job = std::make_shared<Job>();
        job->state = 1;
        jobs[target.getFullPathName()] = job;
        // copy in the background through a temporary name, so a half-written file never shows up in the list
        pool.addJob ([this, job, f, target]
        {
            const auto part = target.withFileExtension (target.getFileExtension() + ".part");
            if (cancel.load() || ! f.copyFileTo (part) || ! part.moveFileTo (target))
            {
                part.deleteFile();
                job->state = 4;
                return;
            }
            job->state = 2;
            juce::String error;
            const auto result = reference::analyseFile (target, [job] (float p) { job->progress = p; }, cancel, error);
            if (result)
            {
                const auto cacheFile = ctx.refs.cacheFileFor (target);
                cacheFile.getParentDirectory().createDirectory();
                juce::TemporaryFile tmp (cacheFile);
                if (tmp.getFile().replaceWithText (juce::JSON::toString (result->toJson(), true)))
                    tmp.overwriteTargetFileWithTemporary();
                job->state = 3;
            }
            else
                job->state = 4;
        });
        ++copied;
    }
    if (copied > 0 && ctx.notify)
        ctx.notify (juce::String (copied) + (copied == 1 ? " file is" : " files are") + " copied into the library and analysed.");
    juce::Timer::callAfterDelay (600, [safe = juce::Component::SafePointer<ReferencesView> (this)] { if (safe) safe->rescan(); });
}

float ReferencesView::progressOf (const juce::File& f) const
{
    const auto j = jobs.find (f.getFullPathName());
    return j != jobs.end() ? j->second->progress.load() : 0.0f;
}

void ReferencesView::removeItem (int row)
{
    if (row < 0 || row >= (int) list.size())
        return;
    const auto file = list[(size_t) row].file;
    juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Remove from the library",
                                        file.getFileName() + "\n\nThe file goes to the Windows Recycle Bin. You can restore it from there.", "Remove", "Cancel", nullptr,
                                        juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<ReferencesView> (this), file] (int r)
    {
        if (safe == nullptr || r != 1)
            return;
        for (int k = 0; k < reference::ReferenceLibrary::numSlots; ++k)
            if (safe->ctx.refs.slot (k).file == file)
                safe->ctx.refs.clear (k);
        if (! file.moveToTrash() && safe->ctx.notify)
            safe->ctx.notify ("The file cannot be moved to the Recycle Bin. Close other programs that use it.");
        safe->ctx.proc.syncExtraState();
        safe->rescan();
    }));
}

void ReferencesView::toggleSlot (int row, int slot)
{
    if (row < 0 || row >= (int) list.size())
        return;
    const auto& f = list[(size_t) row].file;
    if (ctx.refs.slot (slot).file == f && ctx.refs.slot (slot).state != reference::ReferenceLibrary::State::empty)
        ctx.refs.clear (slot);
    else
        ctx.refs.load (slot, f);
    ctx.proc.syncExtraState();
    table->repaint();
    slots->refresh();
}

// ---- drag and drop ---------------------------------------------------------------------------------------
bool ReferencesView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;flac;mp3;ogg;aif;aiff"))
            return true;
    return false;
}

void ReferencesView::fileDragEnter (const juce::StringArray&, int, int)
{
    dragOver = true;
    repaint (dropArea.expanded (4));
}

void ReferencesView::fileDragExit (const juce::StringArray&)
{
    dragOver = false;
    repaint (dropArea.expanded (4));
}

void ReferencesView::filesDropped (const juce::StringArray& paths, int, int)
{
    dragOver = false;
    juce::Array<juce::File> files;
    for (const auto& p : paths)
        files.add (juce::File (p));
    addFiles (files);
    repaint();
}

// ---- layout ----------------------------------------------------------------------------------------------
void ReferencesView::resized()
{
    auto r = getLocalBounds();
    if (! compact)
    {
        slots->setBounds (r.removeFromRight (372));
        r.removeFromRight (8);
    }
    libPanel = r;
    auto in = r;
    headerArea = in.removeFromTop (32);
    add.setBounds (headerArea.reduced (8, 4).removeFromRight (compact ? 96 : 104));
    if (! compact)
    {
        auto pathRow = in.removeFromTop (40).reduced (10, 8);
        open.setBounds (pathRow.removeFromRight (100));
        pathRow.removeFromRight (6);
        change.setBounds (pathRow.removeFromRight (84));
        pathRow.removeFromRight (8);
        pathArea = pathRow;
        open.setVisible (true);
    }
    else
    {
        change.setBounds (add.getBounds().translated (-(add.getWidth() + 6), 0).withWidth (84));
        change.setButtonText ("Folder...");
        open.setVisible (false);
        pathArea = {};
    }
    in.removeFromTop (30); // column header
    dropArea = in.removeFromBottom (compact ? 62 : 102).reduced (10, compact ? 6 : 9);
    tableView.setBounds (in.reduced (10, 0));
    table->setSize (std::max (1, tableView.getWidth() - tableView.getScrollBarThickness()), std::max (tableView.getHeight(), (int) list.size() * rowH));
}

void ReferencesView::paint (juce::Graphics& g)
{
    drawPanel (g, libPanel.toFloat());
    paintPanelHeader (g, libPanel, tr ("Reference library"),
                      juce::String ((int) list.size()) + " " + tr ("files") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + tr ("click a number to put a song into a slot"),
                      add.getWidth() + (compact ? change.getWidth() + 6 : 0) + 16);
    if (! pathArea.isEmpty())
    {
        g.setColour (col::text3);
        g.setFont (font (12.5f));
        g.drawText (tr ("Folder"), pathArea.removeFromLeft (52), juce::Justification::centredLeft);
        g.setColour (col::sunk);
        g.fillRoundedRectangle (pathArea.toFloat(), 5.0f);
        g.setColour (col::line2);
        g.drawRoundedRectangle (pathArea.toFloat().reduced (0.5f), 5.0f, 1.0f);
        g.setColour (col::text2);
        g.drawText (folder().getFullPathName(), pathArea.reduced (8, 0), juce::Justification::centredLeft, true);
        g.setColour (col::line);
        g.drawHorizontalLine (pathArea.getBottom() + 8, (float) libPanel.getX() + 1, (float) libPanel.getRight() - 1);
    }
    // column header
    const auto tv = tableView.getBounds();
    const auto cols = Columns::forWidth (tv.getWidth() - tableView.getScrollBarThickness(), compact);
    const int y = tv.getY() - 30;
    g.setColour (col::text3);
    g.setFont (font (12.5f));
    g.drawText ("Name", tv.getX() + 4, y, 200, 30, juce::Justification::centredLeft);
    if (! compact)
        g.drawText ("Length", tv.getX() + cols.length, y, 54, 30, juce::Justification::centredRight);
    g.drawText ("LUFS-I", tv.getX() + cols.lufs, y, 56, 30, juce::Justification::centredRight);
    g.drawText (compact ? juce::String() : juce::String ("Analysis"), tv.getX() + cols.analysis + 6, y, 90, 30, juce::Justification::centredLeft);
    g.drawText ("Assign to slot", tv.getX() + cols.slots, y, 150, 30, juce::Justification::centredLeft);
    g.setColour (col::line);
    g.drawHorizontalLine (tv.getY() - 1, (float) tv.getX(), (float) tv.getRight());

    // drop zone
    const auto d = dropArea.toFloat();
    g.setColour (dragOver ? col::blue.withAlpha (0.10f) : juce::Colours::white.withAlpha (0.015f));
    g.fillRoundedRectangle (d, 8.0f);
    juce::Path border, dashed;
    border.addRoundedRectangle (d.reduced (1.0f), 8.0f);
    const float dash[] = { 7.0f, 5.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, border, dash, 2);
    g.setColour (dragOver ? col::blue : juce::Colour (0xff3a414d));
    g.fillPath (dashed);
    g.setColour (dragOver ? juce::Colour (0xffcfe3ff) : col::text);
    g.setFont (semibold (13.0f));
    auto t = dropArea;
    g.drawText (dragOver ? tr ("Release to add the files to the library") : tr ("Drop WAV, FLAC, MP3, OGG or AIFF files here"),
                compact ? t : t.removeFromTop (t.getHeight() / 2 + 2), compact ? juce::Justification::centred : juce::Justification::centredBottom, true);
    if (! compact)
    {
        g.setColour (col::text2);
        g.setFont (font (12.5f));
        g.drawText (tr ("The plugin copies them into the library folder and analyzes the whole file."), t.withTrimmedTop (4), juce::Justification::centredTop, true);
    }
}
} // namespace mm::ui
