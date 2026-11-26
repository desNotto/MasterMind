#pragma once

#include "ui/Context.h"

namespace mm::ui
{
/** Tonal balance (5.2). The compact variant is the Master balance column (mockup v5); yellow and
    red rows show an advice tooltip (mockup v7). */
class TonalPanel final : public juce::Component
{
public:
    explicit TonalPanel (Context& c, bool compact = false);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    int preferredHeight() const { return compact ? 32 + 8 + 6 * 29 + 8 : 300; }

private:
    Context& ctx;
    bool compact;
    Segmented basis { { "Integrated", "Last 10 s" } };
    Btn target { "Target" };
    juce::Rectangle<int> rowsArea;
    struct Row
    {
        juce::Rectangle<int> area;
        int kind = 3;
        double dev = 0.0, corridor = 1.5;
    };
    std::array<Row, analysis::numTonalBands> rows;
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

/** Checks summary (5.10). skipTonal: the Master balance column, where the tonal bands have their
    own panel (mockup v5). */
class ChecksPanel final : public juce::Component
{
public:
    ChecksPanel (Context& c, bool compact, bool skipTonal = false);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    Context& ctx;
    bool compact, skipTonal;
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

/** First advice step built from the current Dynamic EQ (mockup v7): names the band that cuts
    (below target) or boosts (above target) the tonal band. Empty when there is none. */
juce::String tonalEqStep (const Context& c, int band, double deviation);
/** Tooltip for a tonal band: advice for yellow and red, nothing for green. */
juce::String tonalTip (const Context& c, int band, int kind, double deviation, double corridor);

/** Shared drawing helpers. */
void drawVectorscope (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<std::pair<float, float>>& pts);
void drawCorrelation (juce::Graphics& g, juce::Rectangle<float> r, double corr);
juce::String trDetail (const juce::String& detail);
const analysis::TapView& viewTap (const Context& c);
} // namespace mm::ui
