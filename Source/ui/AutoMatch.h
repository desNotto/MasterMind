#pragma once

#include "automatch/MatchEngine.h"
#include "ui/Context.h"

#include <map>

namespace mm::ui
{
/** Auto match strip at the top of the Eyes tab (mockup v18, D-040). Three modes: Fast (formulas),
    Hybrid (formulas, then a short search) and Precise (a long search). Every mode records about 12 s
    of the playing song, then works on a private copy of the chain in the background; the song may
    stop after the recording. Undo restores every parameter. */
class AutoMatchStrip final : public juce::Component, private juce::Timer
{
public:
    explicit AutoMatchStrip (Context& c);
    ~AutoMatchStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int numChips = 5;
    static constexpr double recordSeconds = 12.0, excerptSeconds = 8.0, prerollSeconds = 1.0;

    /** Start index of the loudest window of `length` samples (energy of the mid signal). */
    static int loudestWindow (const juce::AudioBuffer<float>& audio, int length);

private:
    enum class Phase
    {
        idle,
        listening,
        running,
        done
    };

    void timerCallback() override;
    void start();
    void stop (const juce::String& message);
    void undo();
    void launch (juce::AudioBuffer<float> recording);
    void finish();
    bool hasReference() const;
    juce::String referenceText() const;
    double referenceLufs() const;
    automatch::Target buildTarget() const;
    automatch::Mode mode() const { return (automatch::Mode) modeSel.getSelected(); }
    int chip() const; // the running chip, -1 when idle
    double progress();
    juce::String clockText (double seconds) const;

    Context& ctx;
    Btn go { "Auto match" }, undoBtn { "Undo" }, stopBtn { "Stop" };
    Segmented modeSel { { "Fast", "Hybrid", "Precise" } };
    Phase phase = Phase::idle;
    std::unique_ptr<automatch::Matcher> matcher;
    automatch::Mode runMode = automatch::Mode::hybrid;
    double startedAt = 0.0, runStartedAt = 0.0, shownProgress = 0.0, lastCapture = 0.0, captureStill = 0.0;
    bool failed = false;
    juce::String status;
    std::map<juce::String, float> snapshot;
    juce::Rectangle<int> refArea, chipsArea, statusArea, hintArea, barArea;
};
} // namespace mm::ui
