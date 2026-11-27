#pragma once

#include "ui/Context.h"

#include <map>

namespace mm::ui
{
/** Experimental Auto match (mockup v11, D-029): moves the plugin's knobs so that the six tonal
    bands become green, band by band from Sub to Air with a re-check of the lower bands after each
    step, then matches the integrated loudness of the reference and tries to make the other checks
    green. Works on the live analysis while the song plays. Undo restores every parameter.

    The strip at the top of the Eyes tab shows the reference, the progress and the buttons. */
class AutoMatchStrip final : public juce::Component, private juce::Timer
{
public:
    explicit AutoMatchStrip (Context& c);
    ~AutoMatchStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** For tests: the steps of the band loop on fixed deviations (no audio). Returns the next band
        to adjust (0..5), or -1 when the bands up to `current` are green; `current` advances. */
    static int nextBand (const std::array<double, 6>& deviation, int& current, double green);

private:
    enum class Phase
    {
        idle,
        tonal,
        loudness,
        checks,
        done
    };
    enum class StepState
    {
        pending,
        running,
        ok,
        failed
    };

    void timerCallback() override;
    void start();
    void stop (const juce::String& message);
    void undo();
    void setupBands();
    void measureAndAct();
    void restartMeasurement();
    bool hasReference() const;
    juce::String referenceText() const;
    double referenceLufs() const;
    std::array<double, 6> deviations() const;

    Context& ctx;
    Btn go { "Auto match" }, undoBtn { "Undo" }, stopBtn { "Stop" };
    Phase phase = Phase::idle;
    std::array<StepState, 8> steps {};
    int currentBand = 0, attempts = 0, loudAttempts = 0, checkStage = 0, cycles = 0;
    std::array<int, 6> bandAttempts {};
    double waited = 0.0, noSignalFor = 0.0, startedAt = 0.0;
    juce::String status;
    std::map<juce::String, float> snapshot;
    juce::Rectangle<int> refArea, badgeArea, chipsArea, statusArea, hintArea;
};
} // namespace mm::ui
