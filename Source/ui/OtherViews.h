#pragma once

#include "ui/EyesView.h"

namespace mm::ui
{
/** Low-End tab (5.4) with band listen and the band crest view. */
class LowEndView final : public juce::Component
{
public:
    explicit LowEndView (Context& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void update();

private:
    Context& ctx;
    LowEndPanel panel;
    std::vector<std::unique_ptr<Btn>> listen;
    juce::Rectangle<int> side;
};

/** Stereo tab (5.5). */
class StereoView final : public juce::Component
{
public:
    explicit StereoView (Context& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void update();

private:
    Context& ctx;
    StereoPanel panel;
    juce::Rectangle<int> bars;
};

/** Spectrogram (5.6) plus harshness (5.7), dynamics per band (5.8) and limiter damage (5.9). */
class SpectrogramView final : public juce::Component
{
public:
    explicit SpectrogramView (Context& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void update();
    void mouseDown (const juce::MouseEvent&) override;

private:
    void rebuildImage();
    Context& ctx;
    Segmented tapSel { { "In", "Out" } };
    Dropdown seconds;
    Btn freeze { "Freeze", true };
    juce::Image image;
    std::vector<std::array<float, analysis::TapAnalyzer::spectrogramBins>> columns;
    uint64_t lastIndex = 0;
    int shownTap = 1;
    juce::Rectangle<int> plot, harsh, crest, damage;
    juce::String readout;
};

/** Album tab (7.4) with passports, album target, hints, overlays and chain snapshots. */
class AlbumView final : public juce::Component
{
public:
    explicit AlbumView (Context& c);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void update();

private:
    void storePassport();
    Context& ctx;
    Btn newAlbum { "New album..." }, openAlbum { "Open album..." }, store { "Store passport" };
    Dropdown targetMode;
    juce::Rectangle<int> table, hints, overlay, bars, guide;
    int rowH = 26;
    std::unique_ptr<juce::FileChooser> chooser;
};
} // namespace mm::ui
