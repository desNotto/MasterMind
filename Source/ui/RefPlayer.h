#pragma once

#include "ui/Context.h"

namespace mm::ui
{
/** Reference player (D-036): shown under the tab bar while B plays a reference or a library song.
    The waveform overview of the whole song shows the played part; a click or drag on it seeks.
    Replaces the small player strip of the tab bar. */
class RefPlayer final : public juce::Component
{
public:
    explicit RefPlayer (Context& c);

    void setCompact (bool c) { compact = c; resized(); }
    /** Message thread, about 30 Hz. */
    void update();

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    static constexpr int heightFull = 46, heightCompact = 36;

private:
    double fractionAt (int x) const;
    void seekTo (double fraction);
    juce::File currentFile() const;
    float xOfSeconds (double s) const;

    Context& ctx;
    bool compact = false;
    /** Flat button with a drawn icon (emoji glyphs render inconsistently on Windows). */
    class IconBtn final : public juce::Button
    {
    public:
        enum class Icon { playPause, restart, close };
        IconBtn (Icon i, const std::atomic<bool>* playingFlag = nullptr) : juce::Button ({}), icon (i), playing (playingFlag) {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        Icon icon;
        const std::atomic<bool>* playing;
    };
    IconBtn play, restart, close;
    juce::Rectangle<int> infoArea, waveArea;
    double dragFraction = -1.0, hoverFraction = -1.0;
    // metrics region (mockup v19, D-041): drag over the waveform selects, the edges can be dragged
    enum class Drag { none, seek, select, startEdge, endEdge } drag = Drag::none;
    int downX = 0;
    double selA = 0.0, selB = 0.0; // seconds while dragging
    juce::Rectangle<float> pillClose;
};
} // namespace mm::ui
