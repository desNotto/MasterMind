#pragma once

#include "ui/Context.h"
#include "ui/EqControls.h"

#include <array>

namespace mm::ui
{
/** The ten modules in signal order. */
struct ModuleInfo
{
    const char* key;
    const char* name;
    const char* onParam;
};
const std::array<ModuleInfo, 10>& moduleList();

/** Module bar (mockup v5): ten tiles with an on/off LED, a status line and a gain reduction bar.
    A click on a tile selects the module shown in the editor below. */
class ModuleBar final : public juce::Component
{
public:
    explicit ModuleBar (Context& c);
    ~ModuleBar() override;
    void setCompact (bool c);
    void update();
    void resized() override;

private:
    class Tile;
    Context& ctx;
    std::vector<std::unique_ptr<Tile>> tiles;
    bool compact = false;
    int shownSelection = -1;
};

/** Editor of one module (mockups v5 to v9): header with the LED, name, subtitle and controls on
    the right, and every control of the module in the body. */
class ModuleEditor : public juce::Component
{
public:
    ModuleEditor (Context& c, int moduleIndex, juce::String subtitle);
    ~ModuleEditor() override;

    virtual void update();
    /** Height that shows every control without scrolling, for a given width. */
    virtual int naturalHeight (int width) const = 0;
    void setCompact (bool c);

    void paint (juce::Graphics&) override;
    void resized() override;

protected:
    virtual void layoutBody (juce::Rectangle<int> body) = 0;
    virtual void compactChanged() {}

    /** Header controls, placed from the right edge in the order they are added. */
    void addRight (juce::Component& c, int width);
    template <class T>
    T& own (std::unique_ptr<T> c, bool visible = true)
    {
        auto& ref = *c;
        if (visible)
            addAndMakeVisible (ref);
        else
            addChildComponent (ref);
        owned.push_back (std::move (c));
        return ref;
    }
    Knob& makeKnob (juce::Component& parent, const juce::String& id, const juce::String& label, const juce::String& tipId, juce::Colour colour, bool bipolar = false);
    bool moduleOn() const;

    Context& ctx;
    int index;
    juce::String subtitle;
    bool compact = false;
    std::unique_ptr<Led> led;
    std::vector<std::pair<juce::Component*, int>> right;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::vector<Knob*> knobs;
    bool lastOn = true;
};

std::unique_ptr<ModuleEditor> createModuleEditor (Context& c, int moduleIndex);

/** Lays out knobs in one row from the left; returns the used width. */
int layoutKnobRow (juce::Rectangle<int> row, const std::vector<Knob*>& ks, int gap = 4);
int knobWidthFor (const juce::String& label);
} // namespace mm::ui
