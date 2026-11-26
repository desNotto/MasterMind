#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/** Colours, fonts and drawing helpers of the approved mockup v4 (docs/mockups). All sizes are in
    reference units of the current layout mode; the editor scales the whole tree uniformly. */
namespace mm::ui
{
namespace col
{
inline const juce::Colour bg { 0xff111317 }, panel { 0xff171a20 }, panel2 { 0xff1d2129 }, sunk { 0xff0d0f13 };
inline const juce::Colour line { 0xff272c35 }, line2 { 0xff343a46 };
inline const juce::Colour text { 0xffe8eaee }, text2 { 0xffa4acb9 }, text3 { 0xff6f7786 };
inline const juce::Colour blue { 0xff4ea1ff }, orange { 0xffff9f43 }, yellow { 0xffffd84d }, magenta { 0xffe35bd8 };
inline const juce::Colour ok { 0xff3ccf91 }, warn { 0xfff2c23a }, bad { 0xffff5468 }, info { 0xff7fb3ff }, mon { 0xffe3262f };
inline const juce::Colour tipBg { 0xfff2f4f7 }, tipText { 0xff15171b }, tipLabel { 0xff5b6370 }, tipWarn { 0xffb3261e };
inline const juce::Colour selBlue { 0xff2b4a73 };
} // namespace col

/** UI font (Segoe UI on Windows, see D-020). Never smaller than 11 px on screen. */
inline juce::Font font (float px, bool bold = false)
{
    auto opts = juce::FontOptions ("Segoe UI", px, bold ? juce::Font::bold : juce::Font::plain);
    return juce::Font (opts);
}

inline juce::Font semibold (float px) { return juce::Font (juce::FontOptions ("Segoe UI Semibold", px, juce::Font::plain)); }

/** U+2212 minus and other typographic helpers. */
inline juce::String minusSign() { return juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")); }
inline juce::String fmtNumber (double v, int decimals)
{
    return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
}
inline juce::String fmtSigned (double v, int decimals = 1)
{
    const auto s = fmtNumber (std::abs (v), decimals);
    if (std::abs (v) < 0.5 * std::pow (10.0, -decimals))
        return s;
    return (v < 0 ? minusSign() : juce::String ("+")) + s;
}
inline juce::String fmtMinus (double v, int decimals = 1)
{
    return (v < -0.5 * std::pow (10.0, -decimals) ? minusSign() : juce::String()) + fmtNumber (std::abs (v), decimals);
}
inline juce::String fmtLufs (double v) { return v <= -70.0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : fmtMinus (v); }

/** Rounded panel background with a hairline border. */
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour fill = col::panel, float radius = 8.0f)
{
    g.setColour (fill);
    g.fillRoundedRectangle (r, radius);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

/** Traffic light with shape + colour (circle ok, triangle warn, square bad, circle i info). */
void drawLight (juce::Graphics& g, juce::Rectangle<float> r, int kind /* 0 ok 1 warn 2 bad 3 info */);
juce::Colour lightColour (int kind);
} // namespace mm::ui
