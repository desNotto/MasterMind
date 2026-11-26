#pragma once

#include "ui/EyesView.h"
#include "ui/ModuleEditors.h"
#include "ui/SpectrumPanel.h"

namespace mm::ui
{
/** Loudness summary for the Master balance column (mockup v5). */
class LoudnessCompact final : public juce::Component
{
public:
    explicit LoudnessCompact (Context& c) : ctx (c) {}
    void paint (juce::Graphics&) override;

private:
    Context& ctx;
};

/** Master tab (mockups v5 to v9): spectrum with EQ editing, module bar, editor of the selected module
    and the balance column (tonal balance, loudness, checks). Medium mode drops the balance column
    (it stays on the Eyes tab); compact mode shows the module bar and the editor only. */
class MasterView final : public juce::Component
{
public:
    enum class Mode
    {
        full,
        medium,
        compact
    };

    explicit MasterView (Context& c);
    ~MasterView() override;
    void setMode (Mode m);
    void update();
    void resized() override;
    void paint (juce::Graphics&) override;

    /** For the layout harness. */
    void selectModule (int index);

private:
    void placeEditor (juce::Rectangle<int> area);

    Context& ctx;
    Mode mode = Mode::full;
    SpectrumPanel spectrum;
    ModuleBar bar;
    std::vector<std::unique_ptr<ModuleEditor>> editors;
    juce::Viewport editorView;
    TonalPanel tonal;
    LoudnessCompact loudness;
    ChecksPanel checks;
    int shownModule = -1;
    juce::Rectangle<int> caretArea;
};
} // namespace mm::ui
