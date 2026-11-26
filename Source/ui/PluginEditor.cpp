#include "ui/PluginEditor.h"

#include "mm_build_info.h"
#include "ui/Strings.h"

namespace
{
namespace colours
{
const juce::Colour background { 0xff15171b };
const juce::Colour bar { 0xff1e2127 };
const juce::Colour divider { 0xff2c3038 };
const juce::Colour textPrimary { 0xffe6e8eb };
const juce::Colour textSecondary { 0xff9aa1ab };
} // namespace colours

constexpr double saveDebounceMs = 400.0;
constexpr float minTextPx = 11.0f;

mm::window::Size primaryWorkArea()
{
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        return { display->userArea.getWidth(), display->userArea.getHeight() };

    return {};
}

juce::Font fontPx (float px)
{
    return juce::Font (juce::FontOptions (std::max (minTextPx, px)));
}
} // namespace

MasterMindEditor::MasterMindEditor (MasterMindProcessor& p)
    : AudioProcessorEditor (p),
      processorRef (p),
      settings (mm::GlobalSettings::defaultFile())
{
    sizeButton.setTooltip (mm::ui::strings::sizeButtonTooltip);
    sizeButton.onClick = [this]
    {
        const auto next = mm::window::sizeAfterButtonClick (currentSize());
        setSize (next.width, next.height);
    };
    addAndMakeVisible (sizeButton);

    setResizable (true, true);
    setResizeLimits (mm::window::minimumSize.width, mm::window::minimumSize.height,
                     mm::window::maximumSize.width, mm::window::maximumSize.height);

    const auto stored = settings.loadWindowSize().value_or (mm::window::size100);
    const auto initial = mm::window::clampToWorkArea (stored, primaryWorkArea());
    lastSavedSize = stored;
    setSize (initial.width, initial.height);

    initialised = true;
    startTimerHz (10);
}

MasterMindEditor::~MasterMindEditor()
{
    stopTimer();
    saveSizeIfChanged();
}

float MasterMindEditor::uiScale() const noexcept
{
    return std::max (0.1f, mm::window::scaleFactor (currentSize()));
}

void MasterMindEditor::saveSizeIfChanged()
{
    saveDueAtMs = 0.0;
    const auto size = currentSize();

    if (size == lastSavedSize || size.width <= 0 || size.height <= 0)
        return;

    if (settings.saveWindowSize (size))
        lastSavedSize = size;
}

void MasterMindEditor::timerCallback()
{
    repaint (footerArea);

    if (saveDueAtMs > 0.0 && juce::Time::getMillisecondCounterHiRes() >= saveDueAtMs)
        saveSizeIfChanged();
}

juce::String MasterMindEditor::footerLeftText() const
{
    return juce::String (mm::build::productName) + "  " + mm::build::version + "  (" + mm::build::gitHash + ")";
}

juce::String MasterMindEditor::footerRightText() const
{
    namespace s = mm::ui::strings;
    const int latency = processorRef.getLatencySamples();
    const double sampleRate = processorRef.getSampleRate();
    const double latencyMs = sampleRate > 0.0 ? 1000.0 * latency / sampleRate : 0.0;

    return juce::String (s::cpu) + " " + juce::String (100.0f * processorRef.getCpuLoad(), 1) + "%    "
         + s::latency + " " + juce::String (latency) + " " + s::samples
         + " (" + juce::String (latencyMs, 2) + " " + s::ms + ")";
}

void MasterMindEditor::paint (juce::Graphics& g)
{
    namespace s = mm::ui::strings;
    const float scale = uiScale();

    g.fillAll (colours::background);

    // Top bar
    g.setColour (colours::bar);
    g.fillRect (topBarArea);
    g.setColour (colours::divider);
    g.fillRect (topBarArea.withTop (topBarArea.getBottom() - 1));

    const int pad = juce::roundToInt (16.0f * scale);
    g.setColour (colours::textPrimary);
    g.setFont (fontPx (20.0f * scale).boldened());
    g.drawFittedText (mm::build::productName, topBarArea.reduced (pad, 0), juce::Justification::centredLeft, 1);

    // Placeholder content
    auto content = contentArea.reduced (pad);
    const int titleHeight = juce::roundToInt (std::max (minTextPx, 28.0f * scale) * 1.4f);
    const int lineHeight = juce::roundToInt (std::max (minTextPx, 16.0f * scale) * 1.4f);
    auto textBlock = content.withSizeKeepingCentre (content.getWidth(), titleHeight + 3 * lineHeight);

    g.setFont (fontPx (28.0f * scale));
    g.drawFittedText (s::placeholderTitle, textBlock.removeFromTop (titleHeight), juce::Justification::centred, 1);

    g.setColour (colours::textSecondary);
    g.setFont (fontPx (16.0f * scale));
    g.drawFittedText (s::placeholderBody, textBlock.removeFromTop (2 * lineHeight), juce::Justification::centred, 2);

    const auto size = currentSize();
    const auto preset = mm::window::presetFor (size);
    const juce::String presetText = preset == mm::window::SizePreset::custom ? s::sizeCustom
                                                                             : mm::window::presetName (preset);
    g.drawFittedText (juce::String (s::windowSize) + ": " + juce::String (size.width) + " x "
                          + juce::String (size.height) + "  (" + presetText + ")",
                      textBlock.removeFromTop (lineHeight), juce::Justification::centred, 1);

    // Footer
    g.setColour (colours::bar);
    g.fillRect (footerArea);
    g.setColour (colours::divider);
    g.fillRect (footerArea.withHeight (1));

    // Leave room on the right for the resize corner.
    auto footerText = footerArea.reduced (pad, 0).withTrimmedRight (juce::roundToInt (18.0f * scale) + 8);
    g.setColour (colours::textSecondary);
    g.setFont (fontPx (13.0f * scale));
    g.drawFittedText (footerLeftText(), footerText, juce::Justification::centredLeft, 1);
    g.drawFittedText (footerRightText(), footerText, juce::Justification::centredRight, 1);
}

void MasterMindEditor::resized()
{
    namespace s = mm::ui::strings;
    const float scale = uiScale();
    auto area = getLocalBounds();

    topBarArea = area.removeFromTop (std::max (32, juce::roundToInt (48.0f * scale)));
    footerArea = area.removeFromBottom (std::max (22, juce::roundToInt (28.0f * scale)));
    contentArea = area;

    const int pad = juce::roundToInt (16.0f * scale);
    const int buttonWidth = std::max (64, juce::roundToInt (84.0f * scale));
    const int buttonHeight = std::max (22, juce::roundToInt (30.0f * scale));
    sizeButton.setBounds (topBarArea.reduced (pad, 0)
                              .removeFromRight (buttonWidth)
                              .withSizeKeepingCentre (buttonWidth, buttonHeight));

    const auto preset = mm::window::presetFor (currentSize());
    sizeButton.setButtonText (preset == mm::window::SizePreset::custom ? s::sizeCustom
                                                                       : mm::window::presetName (preset));

    if (initialised)
        saveDueAtMs = juce::Time::getMillisecondCounterHiRes() + saveDebounceMs;
}
