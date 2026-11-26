#pragma once

#include "reference/MatchEq.h"
#include "ui/Context.h"
#include "ui/EqControls.h"

namespace mm::ui
{
/** Spectrum analyzer with EQ overlay (spec 5.1, 4.2, 7.2, 7.3). */
class SpectrumPanel final : public juce::Component
{
public:
    explicit SpectrumPanel (Context& c, bool compact = false);
    ~SpectrumPanel() override;
    void update(); // timer
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Plot;
    class MatchEqPopup;
    void applySettings();
    void placeBandPanel();

    Context& ctx;
    bool compact;
    Segmented mode { { "Real-time", "Average", "Peak hold" } };
    Segmented modeCompact { { "RT", "Avg" } };
    Dropdown fft, smoothing, tilt, channel;
    Segmented view { { "Curve", "Delta" } };
    Btn matchEq { "Match EQ" };
    std::unique_ptr<Plot> plot;
    std::unique_ptr<MatchEqPopup> popup;
    std::unique_ptr<BandFields> bandPanel;
    juce::Rectangle<int> legendArea;
};
} // namespace mm::ui
