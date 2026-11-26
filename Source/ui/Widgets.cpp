#include "ui/Widgets.h"

#include "analysis/Checks.h"
#include "state/Parameters.h"
#include "ui/TooltipText.h"

namespace mm::ui
{
// ---- Theme helpers --------------------------------------------------------------------------------
juce::Colour lightColour (int kind)
{
    switch (kind)
    {
        case 0: return col::ok;
        case 1: return col::warn;
        case 2: return col::bad;
        default: return col::info;
    }
}

void drawLight (juce::Graphics& g, juce::Rectangle<float> r, int kind)
{
    const auto c = lightColour (kind);
    r = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight()));
    g.setColour (c);
    juce::String glyph;
    switch (kind)
    {
        case 0:
            g.fillEllipse (r);
            glyph = juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93"));
            break;
        case 1:
        {
            juce::Path p;
            p.addTriangle (r.getCentreX(), r.getY(), r.getRight(), r.getBottom(), r.getX(), r.getBottom());
            g.fillPath (p);
            glyph = "!";
            break;
        }
        case 2:
            g.fillRoundedRectangle (r, 2.5f);
            glyph = juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x95"));
            break;
        default:
            g.drawEllipse (r.reduced (0.75f), 1.5f);
            g.setFont (font (r.getHeight() * 0.7f, true));
            g.drawText ("i", r, juce::Justification::centred);
            return;
    }
    g.setColour (juce::Colour (0xff111317));
    g.setFont (font (r.getHeight() * 0.68f, true));
    g.drawText (glyph, r.translated (0.0f, kind == 1 ? r.getHeight() * 0.1f : 0.0f), juce::Justification::centred);
}

// ---- Look and feel -------------------------------------------------------------------------------
MasterLookAndFeel::MasterLookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, col::panel2);
    setColour (juce::ComboBox::textColourId, col::text);
    setColour (juce::ComboBox::outlineColourId, col::line2);
    setColour (juce::ComboBox::arrowColourId, col::text3);
    setColour (juce::PopupMenu::backgroundColourId, col::tipBg);
    setColour (juce::PopupMenu::textColourId, col::tipText);
    setColour (juce::PopupMenu::headerTextColourId, col::tipLabel);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xffd8e4f4));
    setColour (juce::PopupMenu::highlightedTextColourId, col::tipText);
    setColour (juce::TextButton::buttonColourId, col::panel2);
    setColour (juce::TextButton::textColourOffId, col::text);
    setColour (juce::TextButton::textColourOnId, juce::Colours::white);
    setColour (juce::TextEditor::backgroundColourId, col::sunk);
    setColour (juce::TextEditor::textColourId, col::text);
    setColour (juce::TextEditor::outlineColourId, col::line2);
    setColour (juce::TextEditor::focusedOutlineColourId, col::blue);
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::AlertWindow::backgroundColourId, col::panel);
    setColour (juce::AlertWindow::textColourId, col::text);
    setColour (juce::AlertWindow::outlineColourId, col::line2);
    setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3a414d));
    setColour (juce::ListBox::backgroundColourId, col::panel);
    setColour (juce::ToggleButton::textColourId, col::text2);
    setColour (juce::ToggleButton::tickColourId, juce::Colours::white);
    setColour (juce::ToggleButton::tickDisabledColourId, col::text3);
}

void MasterLookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (box.findColour (juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.setFont (font (10.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")), juce::Rectangle<int> (w - 16, 0, 12, h), juce::Justification::centred);
}

void MasterLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 20, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setColour (juce::Label::textColourId, box.findColour (juce::ComboBox::textColourId));
}

void MasterLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();
    const auto* mb = dynamic_cast<Btn*> (&b);
    juce::Colour fill = col::panel2;
    if (on)
        fill = (mb != nullptr && mb->accent) ? col::selBlue : col::selBlue;
    if (b.getProperties().contains ("monitor") && on)
        fill = col::mon;
    if (over && ! on)
        fill = fill.brighter (0.08f);
    if (down)
        fill = fill.brighter (0.15f);
    g.setColour (fill);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (on ? fill.brighter (0.25f) : col::line2);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
}

void MasterLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setColour (b.getToggleState() ? juce::Colours::white : (b.isEnabled() ? col::text : col::text3));
    g.setFont (font (13.0f, b.getToggleState()));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void MasterLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (col::tipBg);
    g.setColour (juce::Colour (0xffc9ced6));
    g.drawRect (0, 0, w, h);
}

void MasterLookAndFeel::drawTickBox (juce::Graphics& g, juce::Component&, float x, float y, float w, float h, bool ticked, bool, bool, bool)
{
    const juce::Rectangle<float> r (x, y, w, h);
    g.setColour (ticked ? col::selBlue : col::panel2);
    g.fillRoundedRectangle (r.reduced (1.0f), 3.0f);
    g.setColour (col::line2);
    g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 1.0f);
    if (ticked)
    {
        g.setColour (juce::Colours::white);
        g.setFont (font (h * 0.8f, true));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")), r, juce::Justification::centred);
    }
}

// ---- Knob ----------------------------------------------------------------------------------------
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& l, juce::Colour c, bool bi)
    : apvts (state), id (paramId), label (l), colour (c), bipolar (bi)
{
    param = apvts.getParameter (id);
    jassert (param != nullptr);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
    attachment->sendInitialUpdate();
    setRepaintsOnMouseActivity (true);
}

void Knob::rebind (const juce::String& paramId)
{
    id = paramId;
    param = apvts.getParameter (id);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float) { repaint(); });
    attachment->sendInitialUpdate();
    repaint();
}

float Knob::valueNorm() const { return param != nullptr ? param->getValue() : 0.0f; }

void Knob::setNorm (float v)
{
    if (param == nullptr)
        return;
    attachment->setValueAsPartOfGesture (param->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, v)));
    if (onUserChange)
        onUserChange();
}

void Knob::resized()
{
    valueArea = getLocalBounds().withTrimmedTop ((int) dial + 2).removeFromTop (16);
}

void Knob::updateMeter()
{
    if (! meterLevel || param == nullptr)
        return;
    const float v = meterLevel();
    const auto range = param->getNormalisableRange();
    float next = shownLevel;
    if (std::isnan (v))
        next = std::numeric_limits<float>::quiet_NaN();
    else
    {
        const float span = range.end - range.start;
        const float clamped = juce::jlimit (range.start, range.end, v);
        // fast rise, slow fall (about 20 % of the scale per second at 30 Hz)
        next = std::isnan (shownLevel) || clamped > shownLevel || meterInverse ? clamped : std::max (clamped, shownLevel - span * 0.007f);
    }
    const bool changed = std::isnan (next) != std::isnan (shownLevel) || (! std::isnan (next) && std::abs (next - shownLevel) > 1.0e-4f);
    shownLevel = next;
    if (changed)
        repaint();
}

void Knob::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    if (isMouseOverOrDragging() || hovered)
    {
        g.setColour (juce::Colour (0xff262b34));
        g.fillRoundedRectangle (b.reduced (1.0f), 6.0f);
    }
    const bool ring = hasRing();
    // with a signal ring the dial is drawn smaller so that the ring fits the same footprint
    const float s = ring ? dial - 5.0f : dial;
    const float cx = b.getCentreX(), cy = 2.0f + dial / 2;
    const float r = s / 2 - 3;
    const float v = valueNorm();
    const float a = juce::degreesToRadians (-135.0f + 270.0f * v);
    const float a0 = juce::degreesToRadians (-135.0f), a1 = juce::degreesToRadians (135.0f), aMid = 0.0f;

    if (ring && param != nullptr)
    {
        // outer ring: the live signal on the threshold scale (green below, red above the threshold)
        const float rr = r + 4.5f, mw = 2.5f;
        juce::Path back;
        back.addCentredArc (cx, cy, rr, rr, 0.0f, a0, a1, true);
        g.setColour (juce::Colour (0xff20252d));
        g.strokePath (back, juce::PathStrokeType (mw));
        if (! std::isnan (shownLevel))
        {
            const float ln = param->convertTo0to1 (shownLevel);
            const float la = juce::degreesToRadians (-135.0f + 270.0f * ln);
            const auto seg = [&] (float from, float to, juce::Colour c)
            {
                if (to - from < 0.01f)
                    return;
                juce::Path p;
                p.addCentredArc (cx, cy, rr, rr, 0.0f, from, to, true);
                g.setColour (c);
                g.strokePath (p, juce::PathStrokeType (mw));
            };
            if (meterInverse)
                seg (a0, la, la < a ? col::bad : col::ok);
            else
            {
                seg (a0, std::min (la, a), col::ok);
                seg (a, la, col::bad);
            }
        }
        g.setColour (juce::Colours::white);
        g.drawLine (cx + (rr - mw / 2 - 1) * std::sin (a), cy - (rr - mw / 2 - 1) * std::cos (a), cx + (rr + mw / 2 + 1) * std::sin (a), cy - (rr + mw / 2 + 1) * std::cos (a), 1.5f);
    }

    juce::Path track;
    track.addCentredArc (cx, cy, r, r, 0.0f, a0, a1, true);
    g.setColour (juce::Colour (0xff2b313b));
    g.strokePath (track, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path arc;
    if (bipolar)
        arc.addCentredArc (cx, cy, r, r, 0.0f, std::min (aMid, a), std::max (aMid, a), true);
    else
        arc.addCentredArc (cx, cy, r, r, 0.0f, a0, a, true);
    g.setColour (isEnabled() ? colour : colour.withAlpha (0.4f));
    if (std::abs (a - (bipolar ? aMid : a0)) > 0.01f)
        g.strokePath (arc, juce::PathStrokeType (3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (juce::Colour (0xff2a2f39));
    g.fillEllipse (juce::Rectangle<float> (r * 2 - 12, r * 2 - 12).withCentre ({ cx, cy }));
    g.setColour (juce::Colour (0xff3a414d));
    g.drawEllipse (juce::Rectangle<float> (r * 2 - 12, r * 2 - 12).withCentre ({ cx, cy }), 1.0f);
    g.setColour (col::text);
    g.drawLine (cx, cy, cx + (r - 7) * std::sin (a), cy - (r - 7) * std::cos (a), 2.0f);

    const auto text = textOverride ? textOverride() : (param != nullptr ? param->getCurrentValueAsText() : juce::String());
    g.setFont (semibold (12.5f));
    g.setColour (col::text);
    g.drawText (text, juce::Rectangle<float> (0, dial + 3, b.getWidth(), 15), juce::Justification::centred, false);
    g.setFont (font (12.0f));
    g.setColour (col::text3);
    g.drawText (label, juce::Rectangle<float> (0, dial + 18, b.getWidth(), 14), juce::Justification::centred, false);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (menuItems.empty())
        {
            showEditor();
            return;
        }
        juce::PopupMenu m;
        m.addItem (1, "Type a value...");
        for (size_t i = 0; i < menuItems.size(); ++i)
            m.addItem ((int) i + 2, menuItems[i].text, true, menuItems[i].ticked ? menuItems[i].ticked() : false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe = juce::Component::SafePointer<Knob> (this)] (int r)
        {
            if (safe == nullptr || r <= 0)
                return;
            if (r == 1)
                safe->showEditor();
            else if (r - 2 < (int) safe->menuItems.size() && safe->menuItems[(size_t) r - 2].action)
                safe->menuItems[(size_t) r - 2].action();
            safe->repaint();
        });
        return;
    }
    lastDrag = e.position;
    dragging = true;
    attachment->beginGesture();
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;
    const float sens = e.mods.isShiftDown() ? 0.0008f : 0.005f; // Shift: fine adjustment
    const float delta = ((lastDrag.y - e.position.y) + (e.position.x - lastDrag.x) * 0.5f) * sens;
    lastDrag = e.position;
    setNorm (valueNorm() + delta);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (dragging)
        attachment->endGesture();
    dragging = false;
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (valueArea.contains (e.getPosition()))
    {
        showEditor();
        return;
    }
    if (param != nullptr) // double-click resets to default
    {
        attachment->beginGesture();
        attachment->setValueAsPartOfGesture (param->convertFrom0to1 (param->getDefaultValue()));
        attachment->endGesture();
    }
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const float step = (e.mods.isShiftDown() ? 0.002f : 0.02f) * (w.deltaY > 0 ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f);
    attachment->beginGesture();
    setNorm (valueNorm() + step);
    attachment->endGesture();
}

void Knob::showEditor()
{
    if (param == nullptr)
        return;
    editor = std::make_unique<juce::TextEditor>();
    addAndMakeVisible (*editor);
    editor->setBounds (valueArea.expanded (2, 1));
    editor->setFont (font (12.5f));
    editor->setJustification (juce::Justification::centred);
    editor->setText (param->getCurrentValueAsText().retainCharacters ("0123456789.-+" + minusSign()).replace (minusSign(), "-"), false);
    editor->selectAll();
    editor->grabKeyboardFocus();
    auto commit = [this]
    {
        if (editor == nullptr)
            return;
        const auto text = editor->getText().replace (minusSign(), "-");
        auto value = text.getFloatValue();
        if (param->getCurrentValueAsText().containsIgnoreCase ("kHz") && ! text.containsIgnoreCase ("hz"))
            value *= 1000.0f;
        if (text.isNotEmpty())
        {
            attachment->beginGesture();
            attachment->setValueAsPartOfGesture (param->getNormalisableRange().snapToLegalValue (value));
            attachment->endGesture();
        }
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)] { if (safe) safe->editor.reset(); });
    };
    editor->onReturnKey = commit;
    editor->onFocusLost = commit;
    editor->onEscapeKey = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Knob> (this)] { if (safe) safe->editor.reset(); }); };
}

// ---- Buttons, dropdowns, segmented, LED ---------------------------------------------------------------
Btn::Btn (const juce::String& text, bool toggle) : juce::TextButton (text)
{
    setClickingTogglesState (toggle);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Btn::attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    setClickingTogglesState (true);
    attachment = std::make_unique<juce::ButtonParameterAttachment> (*state.getParameter (paramId), *this);
}

Dropdown::Dropdown() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

void Dropdown::attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    auto* p = state.getParameter (paramId);
    clear (juce::dontSendNotification);
    addItemList (p->getAllValueStrings(), 1);
    attachment = std::make_unique<juce::ComboBoxParameterAttachment> (*p, *this);
}

Segmented::Segmented (juce::StringArray opts) : options (std::move (opts)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

void Segmented::attach (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    auto* p = state.getParameter (paramId);
    attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { selected = juce::roundToInt (v); repaint(); });
    attachment->sendInitialUpdate();
    onChange = [this] (int i) { attachment->setValueAsCompleteGesture ((float) i); };
}

void Segmented::setSelected (int index, bool notify)
{
    selected = juce::jlimit (0, options.size() - 1, index);
    repaint();
    if (notify && onChange)
        onChange (selected);
}

int Segmented::preferredWidth (float px) const
{
    int w = 0;
    const auto f = font (px);
    for (const auto& o : options)
        w += juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, o)) + 18;
    return w;
}

void Segmented::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (col::sunk);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col::line2);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    const auto f = font (13.0f);
    float total = 0.0f;
    std::vector<float> widths;
    for (const auto& o : options)
    {
        widths.push_back (juce::GlyphArrangement::getStringWidth (f, o) + 18.0f);
        total += widths.back();
    }
    const float scale = r.getWidth() / std::max (1.0f, total);
    float x = r.getX();
    for (int i = 0; i < options.size(); ++i)
    {
        const float w = widths[(size_t) i] * scale;
        const juce::Rectangle<float> cell (x, r.getY(), w, r.getHeight());
        if (i == selected)
        {
            g.setColour (col::selBlue);
            g.fillRoundedRectangle (cell.reduced (1.0f), 4.0f);
        }
        if (i > 0)
        {
            g.setColour (col::line2);
            g.drawVerticalLine ((int) x, r.getY() + 3, r.getBottom() - 3);
        }
        g.setColour (i == selected ? juce::Colours::white : (isEnabled() ? col::text2 : col::text3));
        g.setFont (f);
        g.drawText (options[i], cell, juce::Justification::centred, false);
        x += w;
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;
    const auto f = font (13.0f);
    float total = 0.0f;
    for (const auto& o : options)
        total += juce::GlyphArrangement::getStringWidth (f, o) + 18.0f;
    const float scale = (float) getWidth() / std::max (1.0f, total);
    float x = 0.0f;
    for (int i = 0; i < options.size(); ++i)
    {
        x += (juce::GlyphArrangement::getStringWidth (f, options[i]) + 18.0f) * scale;
        if (e.position.x < x)
        {
            setSelected (i);
            return;
        }
    }
}

Led::Led (juce::AudioProcessorValueTreeState& state, const juce::String& paramId)
{
    attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (paramId), [this] (float v) { on = v >= 0.5f; repaint(); });
    attachment->sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Led::paint (juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const juce::Rectangle<float> r (c.x - 3.5f, c.y - 3.5f, 7.0f, 7.0f);
    if (on)
    {
        g.setColour (col::ok.withAlpha (0.35f));
        g.fillEllipse (r.expanded (3.0f));
    }
    g.setColour (on ? col::ok : juce::Colour (0xff3a404b));
    g.fillEllipse (r);
}

void Led::mouseDown (const juce::MouseEvent&) { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

juce::Rectangle<int> paintPanelHeader (juce::Graphics& g, juce::Rectangle<int> panel, const juce::String& title, const juce::String& sub, int reserveRight)
{
    auto head = panel.removeFromTop (32);
    g.setColour (col::line);
    g.drawHorizontalLine (head.getBottom() - 1, (float) head.getX() + 1, (float) head.getRight() - 1);
    auto t = head.reduced (12, 0);
    g.setColour (col::text);
    g.setFont (semibold (14.0f));
    const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (semibold (14.0f), title)) + 8;
    g.drawText (title, t.removeFromLeft (tw), juce::Justification::centredLeft, false);
    if (sub.isNotEmpty())
    {
        g.setColour (col::text3);
        g.setFont (font (12.5f));
        g.drawText (sub, t.withTrimmedRight (reserveRight), juce::Justification::centredLeft, true);
    }
    return panel;
}

// ---- Tooltip bubble --------------------------------------------------------------------------------
void TipBubble::setContent (juce::String t, juce::String v, std::vector<Section> s, juce::String f)
{
    title = std::move (t);
    values = std::move (v);
    sections = std::move (s);
    footer = std::move (f);
}

int TipBubble::contentHeight (int w) const
{
    int h = 10 + 18 + 4;
    const int textW = w - 24 - 82;
    for (const auto& s : sections)
    {
        juce::AttributedString as;
        as.append (s.text, font (12.0f));
        juce::TextLayout tl;
        tl.createLayout (as, (float) textW);
        h += (int) std::ceil (tl.getHeight()) + 6;
    }
    if (footer.isNotEmpty())
        h += 26;
    return h + 8;
}

void TipBubble::place (juce::Rectangle<int> target, juce::Rectangle<int> bounds)
{
    const int w = width;
    const int h = contentHeight (w);
    int x = juce::jlimit (bounds.getX() + 8, bounds.getRight() - w - 8, target.getCentreX() - w / 2);
    above = target.getY() - h - 12 >= bounds.getY() + 8;
    int y = above ? target.getY() - h - 10 : std::min (bounds.getBottom() - h - 8, target.getBottom() + 10);
    setBounds (x, y, w, h + 8);
    arrowX = target.getCentreX() - x;
    repaint();
}

void TipBubble::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto box = above ? r.withTrimmedBottom (8) : r.withTrimmedTop (8);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (box.translated (0, 3), 8.0f);
    g.setColour (col::tipBg);
    g.fillRoundedRectangle (box, 8.0f);
    juce::Path arrow;
    const float ax = juce::jlimit (12.0f, r.getWidth() - 12.0f, (float) arrowX);
    if (above)
        arrow.addTriangle (ax - 7, box.getBottom(), ax + 7, box.getBottom(), ax, box.getBottom() + 7);
    else
        arrow.addTriangle (ax - 7, box.getY(), ax + 7, box.getY(), ax, box.getY() - 7);
    g.fillPath (arrow);

    auto c = box.reduced (12.0f, 10.0f);
    auto head = c.removeFromTop (18);
    g.setColour (col::tipText);
    g.setFont (font (13.0f, true));
    g.drawText (title, head, juce::Justification::centredLeft, true);
    g.setColour (juce::Colour (0xff3f4652));
    g.setFont (semibold (12.0f));
    g.drawText (values, head, juce::Justification::centredRight, true);
    c.removeFromTop (4);
    for (const auto& s : sections)
    {
        juce::AttributedString as;
        as.append (s.text, font (12.0f), col::tipText);
        juce::TextLayout tl;
        tl.createLayout (as, c.getWidth() - 82);
        const float h = std::ceil (tl.getHeight());
        auto row = c.removeFromTop (h + 6);
        g.setColour (s.warning ? col::tipWarn : col::tipLabel);
        g.setFont (font (12.0f, true));
        g.drawText (s.label, row.withWidth (80).withHeight (16), juce::Justification::topLeft, false);
        tl.draw (g, row.withTrimmedLeft (82).withHeight (h));
    }
    if (footer.isNotEmpty())
    {
        auto f = c.removeFromTop (24);
        g.setColour (juce::Colour (0xffd3d8df));
        g.drawHorizontalLine ((int) f.getY() + 4, f.getX(), f.getRight());
        g.setColour (col::tipLabel);
        g.setFont (font (12.0f));
        g.drawText (footer, f.withTrimmedTop (8), juce::Justification::centredLeft, true);
    }
}

juce::String tonalAdviceTip (int band, double dev, double corridor, const juce::String& eqStep)
{
    return "a:" + juce::String (band) + "|" + juce::String (dev, 2) + "|" + juce::String (corridor, 2) + "|" + eqStep;
}

void fillTip (TipBubble& bubble, const juce::String& tip, juce::Component* source, juce::AudioProcessorValueTreeState& state)
{
    using namespace tips;
    bubble.width = 356;
    const auto kind = tip.upToFirstOccurrenceOf (":", false, false);
    const auto key = tip.fromFirstOccurrenceOf (":", false, false);
    static const std::map<juce::String, juce::String> moduleOn { { "in", "in_on" }, { "eq", "eq_on" }, { "mb", "mb_on" }, { "le", "lem_on" },
                                                                 { "bc", "bus_on" }, { "sat", "sat_on" }, { "st", "st_on" }, { "cl", "clip_on" },
                                                                 { "li", "lim_on" }, { "out", "out_on" } };
    const auto moduleText = [] (const juce::String& m) -> std::pair<juce::String, juce::String>
    {
        for (const auto& mt : modules())
            if (m == mt.key)
                return { u (mt.name), u (mt.text) };
        return { m, {} };
    };
    const auto isOff = [&] (const juce::String& m)
    {
        const auto it = moduleOn.find (m);
        return it != moduleOn.end() && state.getRawParameterValue (it->second)->load() < 0.5f;
    };
    const juce::String advice = u ("\xd0\xa1\xd0\xbe\xd0\xb2\xd0\xb5\xd1\x82");
    const juce::String warning = u ("\xd0\x92\xd0\xbd\xd0\xb8\xd0\xbc\xd0\xb0\xd0\xbd\xd0\xb8\xd0\xb5");

    if (kind == "k")
    {
        const auto m = key.upToFirstOccurrenceOf (".", false, false);
        const auto [modName, modText] = moduleText (m);
        for (const auto& k : knobs())
            if (key == k.id)
            {
                std::vector<TipBubble::Section> s;
                if (isOff (m))
                    s.push_back ({ u ("\xd0\xa1\xd1\x82\xd0\xb0\xd1\x82\xd1\x83\xd1\x81"), u ("\xd0\x9c\xd0\xbe\xd0\xb4\xd1\x83\xd0\xbb\xd1\x8c \xd0\xb2\xd1\x8b\xd0\xba\xd0\xbb\xd1\x8e\xd1\x87\xd0\xb5\xd0\xbd (Bypassed). \xd0\xa0\xd1\x83\xd1\x87\xd0\xba\xd0\xb0 \xd1\x81\xd0\xb5\xd0\xb9\xd1\x87\xd0\xb0\xd1\x81 \xd0\xbd\xd0\xb5 \xd0\xb8\xd0\xb7\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x8f\xd0\xb5\xd1\x82 \xd0\xb7\xd0\xb2\xd1\x83\xd0\xba."), true });
                s.push_back ({ u ("\xd0\x9c\xd0\xbe\xd0\xb4\xd1\x83\xd0\xbb\xd1\x8c"), modText, false });
                s.push_back ({ u ("\xd0\xa0\xd1\x83\xd1\x87\xd0\xba\xd0\xb0"), u (k.text), false });
                if (auto* knob = dynamic_cast<Knob*> (source); knob != nullptr && knob->hasRing())
                    for (const auto& c : controls())
                        if (juce::String (c.id) == (knob->meterInverse ? "thr.ring.inv" : "thr.ring"))
                            s.push_back ({ u ("\xd0\x98\xd0\xbd\xd0\xb4\xd0\xb8\xd0\xba\xd0\xb0\xd1\x82\xd0\xbe\xd1\x80"), u (c.text), false });
                s.push_back ({ u (k.kind), u (k.tip), u (k.kind) == warning });
                bubble.setContent (modName + " \xc2\xb7 " + u (k.label), u (k.range), s,
                                   u ("\xd0\x94\xd0\xb2\xd0\xbe\xd0\xb9\xd0\xbd\xd0\xbe\xd0\xb9 \xd0\xba\xd0\xbb\xd0\xb8\xd0\xba \xe2\x80\x94 \xd1\x81\xd0\xb1\xd1\x80\xd0\xbe\xd1\x81 \xc2\xb7 Shift \xe2\x80\x94 \xd1\x82\xd0\xbe\xd1\x87\xd0\xbd\xd0\xbe"));
                return;
            }
        // generic knob without an approved text: name and range only
        if (auto* knob = dynamic_cast<Knob*> (source))
            if (const auto* spec = params::find (knob->getParamId()))
            {
                bubble.setContent (spec->name, params::formatValue (*spec, spec->minValue) + " \xe2\x80\xa6 " + params::formatValue (*spec, spec->maxValue),
                                   { { u ("\xd0\x9c\xd0\xbe\xd0\xb4\xd1\x83\xd0\xbb\xd1\x8c"), moduleText (m).second, false } },
                                   u ("\xd0\x94\xd0\xb2\xd0\xbe\xd0\xb9\xd0\xbd\xd0\xbe\xd0\xb9 \xd0\xba\xd0\xbb\xd0\xb8\xd0\xba \xe2\x80\x94 \xd1\x81\xd0\xb1\xd1\x80\xd0\xbe\xd1\x81 \xc2\xb7 Shift \xe2\x80\x94 \xd1\x82\xd0\xbe\xd1\x87\xd0\xbd\xd0\xbe"));
                return;
            }
    }
    if (kind == "m")
    {
        const auto [modName, modText] = moduleText (key);
        std::vector<TipBubble::Section> s;
        const bool off = isOff (key);
        if (off)
            s.push_back ({ u ("\xd0\xa1\xd1\x82\xd0\xb0\xd1\x82\xd1\x83\xd1\x81"), u ("\xd0\x9c\xd0\xbe\xd0\xb4\xd1\x83\xd0\xbb\xd1\x8c \xd0\xb2\xd1\x8b\xd0\xba\xd0\xbb\xd1\x8e\xd1\x87\xd0\xb5\xd0\xbd (Bypassed). \xd0\x9e\xd0\xbd \xd0\xbd\xd0\xb5 \xd0\xb8\xd0\xb7\xd0\xbc\xd0\xb5\xd0\xbd\xd1\x8f\xd0\xb5\xd1\x82 \xd0\xb7\xd0\xb2\xd1\x83\xd0\xba."), true });
        s.push_back ({ u ("\xd0\xa7\xd1\x82\xd0\xbe \xd0\xb4\xd0\xb5\xd0\xbb\xd0\xb0\xd0\xb5\xd1\x82"), modText, false });
        s.push_back ({ advice, u ("\xd0\x97\xd0\xb5\xd0\xbb\xd1\x91\xd0\xbd\xd0\xb0\xd1\x8f \xd1\x82\xd0\xbe\xd1\x87\xd0\xba\xd0\xb0 \xd0\xbf\xd0\xbe\xd0\xba\xd0\xb0\xd0\xb7\xd1\x8b\xd0\xb2\xd0\xb0\xd0\xb5\xd1\x82, \xd1\x87\xd1\x82\xd0\xbe \xd0\xbc\xd0\xbe\xd0\xb4\xd1\x83\xd0\xbb\xd1\x8c \xd0\xb2\xd0\xba\xd0\xbb\xd1\x8e\xd1\x87\xd1\x91\xd0\xbd. \xd0\x9a\xd0\xbb\xd0\xb8\xd0\xba \xd0\xbf\xd0\xbe \xd1\x82\xd0\xbe\xd1\x87\xd0\xba\xd0\xb5 \xd0\xb2\xd0\xba\xd0\xbb\xd1\x8e\xd1\x87\xd0\xb0\xd0\xb5\xd1\x82 \xd0\xb8 \xd0\xb2\xd1\x8b\xd0\xba\xd0\xbb\xd1\x8e\xd1\x87\xd0\xb0\xd0\xb5\xd1\x82 \xd0\xbc\xd0\xbe\xd0\xb4\xd1\x83\xd0\xbb\xd1\x8c. \xd0\xa1\xd1\x80\xd0\xb0\xd0\xb2\xd0\xbd\xd0\xb8\xd0\xb2\xd0\xb0\xd0\xb9\xd1\x82\xd0\xb5 \xd0\xb7\xd0\xb2\xd1\x83\xd0\xba \xd0\xbd\xd0\xb0 \xd0\xbe\xd0\xb4\xd0\xbd\xd0\xbe\xd0\xbc \xd0\xb8 \xd1\x82\xd0\xbe\xd0\xbc \xd0\xb6\xd0\xb5 \xd0\xbc\xd0\xb5\xd1\x81\xd1\x82\xd0\xb5 \xd0\xbf\xd0\xb5\xd1\x81\xd0\xbd\xd0\xb8."), false });
        bubble.setContent (modName, off ? juce::String ("off") : juce::String ("on"), s,
                           u ("\xd0\x9a\xd0\xbb\xd0\xb8\xd0\xba \xd0\xbf\xd0\xbe \xd1\x82\xd0\xbe\xd1\x87\xd0\xba\xd0\xb5 \xe2\x80\x94 \xd0\xb2\xd0\xba\xd0\xbb/\xd0\xb2\xd1\x8b\xd0\xba\xd0\xbb \xc2\xb7 \xd0\x94\xd0\xb2\xd0\xbe\xd0\xb9\xd0\xbd\xd0\xbe\xd0\xb9 \xd0\xba\xd0\xbb\xd0\xb8\xd0\xba \xd0\xbf\xd0\xbe \xd0\xb8\xd0\xbc\xd0\xb5\xd0\xbd\xd0\xb8 \xe2\x80\x94 \xd0\xb2\xd1\x81\xd0\xb5 \xd0\xbf\xd0\xb0\xd1\x80\xd0\xb0\xd0\xbc\xd0\xb5\xd1\x82\xd1\x80\xd1\x8b"));
        return;
    }
    if (kind == "a")
    {
        // tonal balance advice (mockup v7)
        const auto parts = juce::StringArray::fromTokens (key, "|", "");
        const int b = juce::jlimit (0, analysis::numTonalBands - 1, parts[0].getIntValue());
        const double dev = parts[1].getDoubleValue(), cor = parts[2].getDoubleValue();
        const auto& adv = tonalAdvice()[(size_t) b];
        const bool up = dev > 0.0;
        const bool bad = std::abs (dev) > 2.5;
        juce::StringArray steps;
        if (parts[3].isNotEmpty())
            steps.add (parts[3]);
        for (const auto* st : up ? adv.upSteps : adv.dnSteps)
            if (steps.size() < 3)
                steps.add (u (st));
        juce::String list;
        for (int i = 0; i < steps.size(); ++i)
            list << (i > 0 ? "\n" : "") << juce::String (i + 1) << ". " << steps[i];
        const auto status = (up ? u ("\xd0\x92\xd1\x8b\xd1\x88\xd0\xb5 \xd1\x86\xd0\xb5\xd0\xbb\xd0\xb8 \xd0\xbd\xd0\xb0 ") : u ("\xd0\x9d\xd0\xb8\xd0\xb6\xd0\xb5 \xd1\x86\xd0\xb5\xd0\xbb\xd0\xb8 \xd0\xbd\xd0\xb0 ")) + juce::String (std::abs (dev), 1) + u (" dB. \xd0\x9d\xd0\xbe\xd1\x80\xd0\xbc\xd0\xb0: \xc2\xb1") + juce::String (cor, 1) + " dB.";
        std::vector<TipBubble::Section> s { { u ("\xd0\xa1\xd1\x82\xd0\xb0\xd1\x82\xd1\x83\xd1\x81"), status, bad }, { u ("\xd0\xa1\xd0\xbb\xd1\x8b\xd1\x88\xd0\xbd\xd0\xbe"), u (up ? adv.upHeard : adv.dnHeard), false }, { u ("\xd0\x98\xd1\x81\xd0\xbf\xd1\x80\xd0\xb0\xd0\xb2\xd1\x8c\xd1\x82\xd0\xb5"), list, false } };
        bubble.width = 410;
        bubble.setContent (juce::String (analysis::tonalBandName (b)) + u (" \xc2\xb7 ") + u (analysis::tonalBandRange (b)), fmtSigned (dev) + " dB", s, u ("\xd0\x9c\xd0\xb5\xd0\xbd\xd1\x8f\xd0\xb9\xd1\x82\xd0\xb5 \xd0\xbd\xd0\xb0 1 dB \xd0\xb7\xd0\xb0 \xd1\x88\xd0\xb0\xd0\xb3. \xd0\x9d\xd1\x83\xd0\xb6\xd0\xbd\xd0\xbe \xd0\xb1\xd0\xbe\xd0\xbb\xd1\x8c\xd1\x88\xd0\xb5 3 dB \xe2\x80\x94 \xd0\xb8\xd1\x81\xd0\xbf\xd1\x80\xd0\xb0\xd0\xb2\xd1\x8c\xd1\x82\xd0\xb5 \xd0\xbc\xd0\xb8\xd0\xba\xd1\x81."));
        return;
    }
    if (kind == "c")
    {
        const auto id = key.upToFirstOccurrenceOf (":", false, false);
        const auto n = key.fromFirstOccurrenceOf (":", false, false);
        for (const auto& c : controls())
            if (id == c.id)
            {
                const auto f = [&] (const char* s) { return u (s).replace ("{n}", n); };
                std::vector<TipBubble::Section> s { { u ("\xd0\xa7\xd1\x82\xd0\xbe \xd0\xb4\xd0\xb5\xd0\xbb\xd0\xb0\xd0\xb5\xd1\x82"), f (c.text), false },
                                                    { u (c.kind), f (c.tip), u (c.kind) == warning } };
                bubble.setContent (f (c.title), f (c.values), s, u (c.gesture));
                return;
            }
    }
    // free text: "t:Title\nBody"
    const auto title = key.upToFirstOccurrenceOf ("\n", false, false);
    const auto body = key.fromFirstOccurrenceOf ("\n", false, false);
    bubble.setContent (title, {}, body.isNotEmpty() ? std::vector<TipBubble::Section> { { "", body, false } } : std::vector<TipBubble::Section> {}, {});
}
} // namespace mm::ui
