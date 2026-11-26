#pragma once

#include "ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace mm::ui
{
/** Component property naming the tooltip of a control: "k:<knob id>", "c:<control id>",
    "m:<module key>", or "t:<free text>" (first line title, rest body). */
inline const juce::Identifier tipProperty { "mmTip" };
inline void setTip (juce::Component& c, const juce::String& id) { c.getProperties().set (tipProperty, id); }

/** Look and feel matching the mockup: dark controls, light popup menus. */
class MasterLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    MasterLookAndFeel();
    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f); }
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h, bool ticked, bool enabled, bool over, bool down) override;
};

/** Rotary knob drawn like the mockup (arc, pointer, value, label), attached to a parameter.
    Double-click resets, Shift is fine adjustment, right-click or double-click on the value types a
    number (spec section 8). */
class Knob final : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& label,
          juce::Colour colour = col::orange, bool bipolar = false);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    void setLabel (const juce::String& l) { label = l; repaint(); }
    juce::String getParamId() const { return id; }
    void setColour (juce::Colour c) { colour = c; repaint(); }
    void rebind (const juce::String& paramId);
    bool hovered = false;
    /** Optional replacement for the value text (e.g. "Auto"). */
    std::function<juce::String()> textOverride;
    /** Called after the user changed the value (not on automation). */
    std::function<void()> onUserChange;

private:
    void showEditor();
    float valueNorm() const;
    void setNorm (float v);

    juce::AudioProcessorValueTreeState& apvts;
    juce::String id, label;
    juce::RangedAudioParameter* param = nullptr;
    juce::Colour colour;
    bool bipolar;
    juce::Point<float> lastDrag;
    bool dragging = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    std::unique_ptr<juce::TextEditor> editor;
    juce::Rectangle<int> valueArea;
};

/** Text button styled like the mockup ("btn", "btn tog on"). Optional parameter attachment. */
class Btn final : public juce::TextButton
{
public:
    explicit Btn (const juce::String& text, bool toggle = false);
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);
    bool accent = false; // filled blue when on

private:
    std::unique_ptr<juce::ButtonParameterAttachment> attachment;
};

/** Drop-down list ("dd"). Optional parameter attachment. */
class Dropdown final : public juce::ComboBox
{
public:
    Dropdown();
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);

private:
    std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
};

/** Segmented control ("seg") with an optional choice parameter. */
class Segmented final : public juce::Component
{
public:
    explicit Segmented (juce::StringArray options);
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);
    void setSelected (int index, bool notify = true);
    int getSelected() const { return selected; }
    void setOptions (const juce::StringArray& o)
    {
        if (o != options)
        {
            options = o;
            repaint();
        }
    }
    std::function<void (int)> onChange;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int preferredWidth (float px) const;

private:
    juce::StringArray options;
    int selected = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

/** Module on/off LED (green when on), attached to a boolean parameter. */
class Led final : public juce::Component
{
public:
    Led (juce::AudioProcessorValueTreeState& state, const juce::String& paramId);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool isOn() const { return on; }

private:
    bool on = false;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

/** Draws a panel header ("ph") with a title and optional subtitle; returns the remaining area. */
juce::Rectangle<int> paintPanelHeader (juce::Graphics& g, juce::Rectangle<int> panel, const juce::String& title, const juce::String& sub = {});

/** Rich tooltip bubble (title, values, labelled sections, footer). */
class TipBubble final : public juce::Component
{
public:
    struct Section
    {
        juce::String label, text;
        bool warning = false;
    };
    void setContent (juce::String title, juce::String values, std::vector<Section> sections, juce::String footer);
    /** Layout for a target rectangle inside `bounds` (above, or below if no room). */
    void place (juce::Rectangle<int> target, juce::Rectangle<int> bounds);
    void paint (juce::Graphics&) override;

private:
    juce::String title, values, footer;
    std::vector<Section> sections;
    bool above = true;
    int arrowX = 0;
    int contentHeight (int width) const;
};

/** Resolves a tooltip property string into a bubble (uses the approved texts). */
void fillTip (TipBubble& bubble, const juce::String& tip, juce::Component* source, juce::AudioProcessorValueTreeState& state);
} // namespace mm::ui
