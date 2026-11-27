#pragma once

#include "ui/Context.h"

#include <map>

namespace mm::ui
{
/** Experimental Auto match (mockups v11 and v12, D-031): moves the knobs of all ten modules so that
    the tonal bands become green, the dynamics and the stereo width come close to the reference,
    the loudness matches the reference and the checks become green. Works on the live analysis
    while the song plays; Undo restores every parameter.

    The strip at the top of the Eyes tab shows the reference, the progress and the buttons. */
class AutoMatchStrip final : public juce::Component, private juce::Timer
{
public:
    explicit AutoMatchStrip (Context& c);
    ~AutoMatchStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    /** The band loop on fixed deviations (no audio): returns the next band to adjust (0..5), or -1
        when the bands up to `current` are green; `current` advances. */
    static int nextBand (const std::array<double, 6>& deviation, int& current, double green);

    static constexpr int numSteps = 11; // Input, six bands, Dynamics, Width, Loudness, Checks

private:
    enum class Phase
    {
        idle,
        input,
        tonal,
        dynamics,
        width,
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
    void afterTonal();
    void restartMeasurement();
    bool hasReference() const;
    juce::String referenceText() const;
    double referenceLufs() const;
    std::array<double, 6> deviations() const;
    void enterTonal (const juce::String& why);
    void finish();
    /** Overall progress 0..1: finished steps plus the running measurement (never goes back). */
    double progress();

    Context& ctx;
    Btn go { "Auto match" }, undoBtn { "Undo" }, stopBtn { "Stop" };
    Phase phase = Phase::idle;
    std::array<StepState, numSteps> steps {};
    int currentBand = 0, loudAttempts = 0, checkStage = 0, cycles = 0, dynStage = 0, widthAttempts = 0;
    bool dynamicsDone = false, widthDone = false, loudnessDone = false;
    std::array<int, 6> bandAttempts {};
    std::array<double, 5> mbTargetGr {};
    double waited = 0.0, noSignalFor = 0.0, startedAt = 0.0, shownProgress = 0.0;
    juce::Rectangle<int> barArea;

    // meters sampled while a measurement runs (averages and maxima)
    struct Meters
    {
        int n = 0;
        std::array<double, 5> mbGr {}, mbLevel {};
        std::array<int, 5> mbLevelN {};
        double busGr = 0.0, busLevel = 0.0, clipPeak = -100.0;
        int busLevelN = 0;
    } meters;

    juce::String status;
    std::map<juce::String, float> snapshot;
    juce::Rectangle<int> refArea, badgeArea, chipsArea, statusArea, hintArea;
};
} // namespace mm::ui
