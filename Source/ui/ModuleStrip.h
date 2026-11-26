#pragma once

#include "ui/Context.h"

namespace mm::ui
{
/** Signal chain strip (mockup v4): ten module cards in two rows, in signal order. */
class ModuleStrip final : public juce::Component, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit ModuleStrip (Context& c);
    ~ModuleStrip() override;
    void update(); // timer: meters
    void paint (juce::Graphics&) override;
    void resized() override;
    void selectEqBand (int band);
    void mouseUp (const juce::MouseEvent&) override;

private:
    class Card;
    void parameterChanged (const juce::String& id, float value) override;

    Context& ctx;
    std::vector<std::unique_ptr<Card>> cards;
    int shownEqBand = -1;
};

/** Overlay listing every parameter of one module with generic controls (double-click on a module
    name). */
class ModuleDetail final : public juce::Component
{
public:
    ModuleDetail (Context& c, const juce::String& moduleKey);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    std::function<void()> onClose;

private:
    Context& ctx;
    juce::String key, title;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<std::unique_ptr<juce::Component>> controls;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> groupHeaders;
    Btn close { "Close" };
    struct Header final : public juce::Component
    {
        std::vector<std::pair<juce::String, juce::Rectangle<int>>>* groups = nullptr;
        void paint (juce::Graphics& g) override;
    } headerPainter;
};
} // namespace mm::ui
