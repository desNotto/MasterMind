#pragma once

#include "ui/Context.h"

namespace mm::ui
{
/** Tonal balance (5.2). */
class TonalPanel final : public juce::Component
{
public:
    explicit TonalPanel (Context& c);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    Context& ctx;
    Segmented basis { { "Integrated", "Last 10 s" } };
    Btn target { "Target" };
    juce::Rectangle<int> rowsArea;
};

/** Loudness and dynamics (5.3). */
class LoudnessPanel final : public juce::Component
{
public:
    explicit LoudnessPanel (Context& c);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Context& ctx;
    Btn target { "Target: Streaming ref" };
};

/** Checks summary (5.10). */
class ChecksPanel final : public juce::Component
{
public:
    ChecksPanel (Context& c, bool compact);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    Context& ctx;
    bool compact;
    int hover = -1;
    juce::Rectangle<int> listArea;
};

/** Low end overview (5.4) for the Eyes grid; the big version is the Low-End tab. */
class LowEndPanel final : public juce::Component
{
public:
    LowEndPanel (Context& c, bool large);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    Context& ctx;
    bool large;
    Btn open { "Open Low-End tab" };
    Segmented channel { { "L", "R", "Mid", "Side" } };
};

/** Stereo overview (5.5); the big version is the Stereo tab. */
class StereoPanel final : public juce::Component
{
public:
    StereoPanel (Context& c, bool large);
    void paint (juce::Graphics&) override;

private:
    Context& ctx;
    bool large;
};

/** Eyes tab: all measurements together. */
class EyesView final : public juce::Component
{
public:
    explicit EyesView (Context& c);
    void resized() override;
    void update();
    void setMedium (bool m);

private:
    Context& ctx;
    TonalPanel tonal;
    LoudnessPanel loudness;
    ChecksPanel checks;
    LowEndPanel lowEnd;
    StereoPanel stereo;
    bool medium = false;
};

/** Shared drawing helpers. */
void drawVectorscope (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<std::pair<float, float>>& pts);
void drawCorrelation (juce::Graphics& g, juce::Rectangle<float> r, double corr);
const analysis::TapView& viewTap (const Context& c);
} // namespace mm::ui
