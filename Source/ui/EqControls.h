#pragma once

#include "ui/Context.h"

namespace mm::ui
{
/** Helpers for editing Dynamic EQ bands (mockups v8 and v9). A band "exists" on the spectrum when
    it is on or when any of its settings differs from the default; Delete resets it to the default
    (see docs/DECISIONS.md, D-028). */
namespace eqedit
{
bool bandUsed (const Context& ctx, int band);
bool isCut (const Context& ctx, int band);
bool isNotch (const Context& ctx, int band);
bool hasGain (const Context& ctx, int band);
void deleteBand (Context& ctx, int band);
/** Double-click on the spectrum: low cut at the left edge (band 1), high cut at the right edge
    (band 10), otherwise a bell on the first free band 2-9. Returns the band or 0. */
int addBandAt (Context& ctx, double freq, double gainDb);
double snapToNote (double freq);
/** Parses "4800", "4.8k", "4.8 kHz", "E1", "D#2", "Bb3". Returns NaN when the text is not valid. */
double parseFrequency (const juce::String& text);
juce::String noteText (double freq);
juce::String freqText (double freq);
/** Sets a parameter as one gesture. */
void set (Context& ctx, const juce::String& id, float plain);
} // namespace eqedit

/** Click-to-type value box ("fld" in the mockup) bound to a parameter. */
class ValueField final : public juce::Label, private juce::Timer
{
public:
    ValueField (Context& c, juce::String paramId, bool frequency);
    void rebind (const juce::String& paramId);
    void paint (juce::Graphics&) override;
    std::function<void()> onCommitted;

private:
    void textWasEdited() override;
    void editorShown (juce::TextEditor*) override;
    void timerCallback() override;
    juce::String display() const;
    Context& ctx;
    juce::String id;
    bool freq;
};

/** Round on/off button with a power symbol (band panel). */
class PowerToggle final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool on = false;
    std::function<void()> onClick;
};

/** The selected band's controls. Panel layout: the floating panel on the spectrum (two rows with
    type, placement, Dynamic, Snap and Delete). Editor layout: the block next to the band knobs in
    the Dynamic EQ editor (Freq with the note, Snap, Delete; Gain or Slope, Q). */
class BandFields final : public juce::Component
{
public:
    BandFields (Context& c, bool panelLayout);
    void setBand (int band);
    int getBand() const { return band; }
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Point<int> preferredSize() const;

private:
    void rebuild();
    Context& ctx;
    bool panel;
    int band = 0;
    bool cutShown = false, notchShown = false;
    juce::String noteShown;
    PowerToggle power;
    Btn dynamic { "Dynamic", true }, snap { juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xaa Snap")), true }, del { juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95")) };
    Dropdown type, place, slope;
    std::unique_ptr<ValueField> freqField, gainField, qField;
    juce::Rectangle<int> noteArea, labelFreq, labelGain, labelQ, titleArea;
};
} // namespace mm::ui
