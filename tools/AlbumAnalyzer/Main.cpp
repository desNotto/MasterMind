// MasterMind Album Analyzer (spec 7.5): computes passports of bounced album files faster than
// real time and stores them in the same .mastermind-album file the plugin uses.
#include "album/Album.h"
#include "reference/TrackAnalysis.h"
#include "ui/Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
class MainComponent final : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    MainComponent()
    {
        for (auto* b : { &newAlbum, &openAlbum, &addFiles })
        {
            addAndMakeVisible (*b);
            b->setColour (juce::TextButton::buttonColourId, mm::ui::col::panel2);
        }
        newAlbum.onClick = [this] { chooseAlbum (true); };
        openAlbum.onClick = [this] { chooseAlbum (false); };
        addFiles.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Add bounced songs", juce::File(), "*.wav;*.flac;*.aif;*.aiff;*.mp3;*.ogg");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      for (const auto& f : fc.getResults())
                                          enqueue (f);
                                  });
        };
        setSize (720, 480);
        startTimerHz (10);
    }

    ~MainComponent() override
    {
        cancel = true;
        pool.removeAllJobs (true, 10000);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace mm::ui;
        g.fillAll (col::bg);
        g.setColour (col::text);
        g.setFont (font (18.0f, true));
        g.drawText ("MasterMind Album Analyzer", 16, 10, 400, 28, juce::Justification::centredLeft);
        g.setColour (col::text2);
        g.setFont (font (13.0f));
        g.drawText (album.isOpen() ? "Album: " + album.getFile().getFullPathName() : juce::String ("Create or open an album file first."), 16, 84, getWidth() - 32, 20,
                    juce::Justification::centredLeft);
        auto list = getLocalBounds().withTrimmedTop (112).reduced (16, 12);
        g.setColour (col::panel);
        g.fillRoundedRectangle (list.toFloat(), 8.0f);
        if (rows.empty())
        {
            g.setColour (col::text3);
            g.drawFittedText ("Drop the bounced WAV/FLAC files of the album here (in track order).\nEach file gets a passport in the album file, like Store passport in the plugin.",
                              list.reduced (20), juce::Justification::centred, 3);
        }
        auto r = list.reduced (10, 8);
        for (const auto& row : rows)
        {
            auto line = r.removeFromTop (24);
            g.setColour (col::text);
            g.drawText (row.name, line.removeFromLeft (line.getWidth() - 200), juce::Justification::centredLeft, true);
            g.setColour (row.error.isNotEmpty() ? col::bad : (row.done ? col::ok : col::text2));
            g.drawText (row.error.isNotEmpty() ? row.error : (row.done ? row.result : juce::String (juce::roundToInt (row.progress * 100.0f)) + "%"), line,
                        juce::Justification::centredRight, true);
        }
    }

    void resized() override
    {
        newAlbum.setBounds (16, 46, 130, 28);
        openAlbum.setBounds (152, 46, 130, 28);
        addFiles.setBounds (288, 46, 130, 28);
    }

    bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        juce::StringArray sorted (files);
        sorted.sortNatural();
        for (const auto& f : sorted)
            enqueue (juce::File (f));
    }

private:
    struct Row
    {
        juce::String name, result, error;
        float progress = 0.0f;
        bool done = false;
    };

    void chooseAlbum (bool create)
    {
        chooser = std::make_unique<juce::FileChooser> (create ? "New album" : "Open album", juce::File(), "*.mastermind-album");
        const int chooserFlags = create ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                 : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
        chooser->launchAsync (chooserFlags, [this, create] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (f == juce::File())
                return;
            juce::String err;
            const bool ok = create ? album.create (f, f.getFileNameWithoutExtension(), err) : album.open (f, err);
            if (! ok)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Album", err);
            repaint();
        });
    }

    void enqueue (const juce::File& f)
    {
        if (! album.isOpen())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Album", "Create or open an album file first.");
            return;
        }
        rows.push_back ({ f.getFileNameWithoutExtension(), {}, {}, 0.0f, false });
        const auto index = rows.size() - 1;
        auto safe = juce::Component::SafePointer<MainComponent> (this);
        pool.addJob ([this, safe, f, index]
        {
            juce::String err;
            auto result = mm::reference::analyseFile (
                f, [safe, index] (float p) { juce::MessageManager::callAsync ([safe, index, p] { if (safe) safe->rows[index].progress = p; }); }, cancel, err);
            juce::MessageManager::callAsync ([safe, index, result, err]
            {
                if (! safe)
                    return;
                auto& row = safe->rows[index];
                row.done = true;
                if (! result)
                {
                    row.error = err;
                    return;
                }
                juce::String err2;
                auto passport = *result;
                passport.name = row.name;
                if (! safe->album.storePassport (passport, err2))
                    row.error = err2;
                else
                    row.result = juce::String (passport.integrated, 1) + " LUFS, PLR " + juce::String (passport.plr, 1);
            });
        });
    }

    void timerCallback() override { repaint(); }

    juce::TextButton newAlbum { "New album..." }, openAlbum { "Open album..." }, addFiles { "Add songs..." };
    mm::album::AlbumFile album;
    std::vector<Row> rows;
    juce::ThreadPool pool { 2 };
    std::atomic<bool> cancel { false };
    std::unique_ptr<juce::FileChooser> chooser;
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name) : DocumentWindow (name, mm::ui::col::bg, DocumentWindow::allButtons) {}
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class AnalyzerApp final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "MasterMind Album Analyzer"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    void initialise (const juce::String&) override
    {
        window = std::make_unique<MainWindow> (getApplicationName());
        window->setUsingNativeTitleBar (true);
        window->setContentOwned (new MainComponent(), true);
        window->setResizable (true, false);
        window->centreWithSize (720, 480);
        window->setVisible (true);
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> window;
};
} // namespace

START_JUCE_APPLICATION (AnalyzerApp)
