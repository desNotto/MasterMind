#include "ui/EqControls.h"

#include "dsp/DynamicEq.h"
#include "state/Parameters.h"

namespace mm::ui
{
namespace eqedit
{
namespace
{
float defaultOf (const juce::String& id)
{
    const auto* s = params::find (id);
    return s != nullptr ? s->defaultValue : 0.0f;
}

dsp::EqKind kindOf (const Context& ctx, int band)
{
    return dsp::DynamicEq::kindOf (band - 1, juce::roundToInt (ctx.param (params::eq (band, "type"))));
}

const char* const fields[] = { "on", "type", "slope", "freq", "gain", "q", "place", "dyn", "thr", "range", "mode", "att", "rel" };
} // namespace

void set (Context& ctx, const juce::String& id, float plain) { ctx.setParam (id, plain); }

bool bandUsed (const Context& ctx, int band)
{
    if (ctx.param (params::eq (band, "on")) >= 0.5f)
        return true;
    for (const auto* f : { "type", "freq", "gain", "q", "place", "dyn" })
    {
        const auto id = params::eq (band, f);
        if (std::abs (ctx.param (id) - defaultOf (id)) > (juce::String (f) == "freq" ? 0.5f : 0.005f))
            return true;
    }
    return false;
}

bool isCut (const Context& ctx, int band)
{
    const auto k = kindOf (ctx, band);
    return k == dsp::EqKind::lowCut || k == dsp::EqKind::highCut;
}

bool isNotch (const Context& ctx, int band) { return kindOf (ctx, band) == dsp::EqKind::notch; }

bool hasGain (const Context& ctx, int band) { return ! isCut (ctx, band) && ! isNotch (ctx, band); }

void deleteBand (Context& ctx, int band)
{
    for (const auto* f : fields)
    {
        const auto id = params::eq (band, f);
        if (params::find (id) != nullptr)
            ctx.setParam (id, defaultOf (id));
    }
    if (ctx.selectedEqBand == band)
        for (int b = 1; b <= 10; ++b)
            if (b != band && bandUsed (ctx, b))
            {
                ctx.selectedEqBand = b;
                break;
            }
}

int addBandAt (Context& ctx, double freq, double gainDb)
{
    freq = juce::jlimit (20.0, 20000.0, freq);
    const bool edgeLo = freq < 30.0, edgeHi = freq > 14000.0;
    int band = 0;
    juce::String message;
    if (edgeLo && ! bandUsed (ctx, 1))
    {
        band = 1;
        ctx.setParam (params::eq (1, "type"), 1.0f); // Low cut
    }
    else if (edgeHi && ! bandUsed (ctx, 10))
    {
        band = 10;
        ctx.setParam (params::eq (10, "type"), 1.0f); // High cut
    }
    else
    {
        for (int b = 2; b <= 9 && band == 0; ++b)
            if (! bandUsed (ctx, b))
                band = b;
        if (band == 0)
        {
            if (ctx.notify)
                ctx.notify ("All 10 bands are in use. Delete a band first.");
            return 0;
        }
        if (edgeLo)
            message = "Band 1 (the only low cut band) is in use: a low shelf was added.";
        if (edgeHi)
            message = "Band 10 (the only high cut band) is in use: a high shelf was added.";
        ctx.setParam (params::eq (band, "type"), edgeLo ? 1.0f : (edgeHi ? 2.0f : 0.0f));
        ctx.setParam (params::eq (band, "gain"), edgeLo || edgeHi ? 0.0f : (float) juce::jlimit (-18.0, 18.0, std::round (gainDb * 2.0) / 2.0));
        ctx.setParam (params::eq (band, "q"), 1.0f);
    }
    ctx.setParam ("eq_on", 1.0f);
    ctx.setParam (params::eq (band, "on"), 1.0f);
    ctx.setParam (params::eq (band, "freq"), (float) (ctx.eqSnap ? snapToNote (freq) : freq));
    ctx.selectedEqBand = band;
    ctx.selectedModule = 1;
    if (message.isNotEmpty() && ctx.notify)
        ctx.notify (message);
    return band;
}

double snapToNote (double f) { return 440.0 * std::pow (2.0, std::round (12.0 * std::log2 (f / 440.0)) / 12.0); }

double parseFrequency (const juce::String& raw)
{
    const auto t = raw.trim().replace (",", ".").replace (minusSign(), "-");
    if (t.isEmpty())
        return std::nan ("");
    const auto c0 = juce::CharacterFunctions::toUpperCase (t[0]);
    if (c0 >= 'A' && c0 <= 'G')
    {
        static const int steps[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        int semis = steps[c0 - 'A'];
        int pos = 1;
        if (t[pos] == '#')
        {
            ++semis;
            ++pos;
        }
        else if (t[pos] == 'b')
        {
            --semis;
            ++pos;
        }
        const auto oct = t.substring (pos).trim();
        if (oct.isEmpty() || ! oct.containsOnly ("-0123456789"))
            return std::nan ("");
        const int midi = (oct.getIntValue() + 1) * 12 + semis;
        return 440.0 * std::pow (2.0, (midi - 69) / 12.0);
    }
    const auto num = t.retainCharacters ("0123456789.");
    if (num.isEmpty())
        return std::nan ("");
    double v = num.getDoubleValue();
    const auto unit = t.fromFirstOccurrenceOf (num, false, false).trim().toLowerCase();
    if (unit.startsWith ("k"))
        v *= 1000.0;
    else if (unit.isNotEmpty() && ! unit.startsWith ("hz"))
        return std::nan ("");
    return v;
}

juce::String noteText (double f)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double n = 12.0 * std::log2 (f / 440.0) + 69.0;
    const int r = juce::roundToInt (n);
    const int cents = juce::roundToInt ((n - r) * 100.0);
    return juce::String (names[((r % 12) + 12) % 12]) + juce::String (r / 12 - 1) + " " + (cents < 0 ? minusSign() : juce::String ("+")) + juce::String (std::abs (cents)) + " ct";
}

juce::String freqText (double f)
{
    if (f >= 1000.0)
        return juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz";
    return juce::String (f, f < 100.0 ? 1 : 0) + " Hz";
}
} // namespace eqedit

// ---- ValueField ------------------------------------------------------------------------------------------
ValueField::ValueField (Context& c, juce::String paramId, bool frequency) : ctx (c), id (std::move (paramId)), freq (frequency)
{
    setEditable (true, false, false);
    setJustificationType (juce::Justification::centredRight);
    setFont (semibold (12.5f));
    setColour (juce::Label::textColourId, col::text);
    setColour (juce::Label::backgroundWhenEditingColourId, col::sunk);
    setColour (juce::Label::textWhenEditingColourId, juce::Colours::white);
    setColour (juce::Label::outlineWhenEditingColourId, col::yellow);
    setMouseCursor (juce::MouseCursor::IBeamCursor);
    setTip (*this, "c:eq.field");
    setText (display(), juce::dontSendNotification);
    startTimerHz (10);
}

void ValueField::rebind (const juce::String& paramId)
{
    id = paramId;
    setText (display(), juce::dontSendNotification);
}

juce::String ValueField::display() const
{
    if (freq)
        return eqedit::freqText (ctx.param (id));
    if (auto* p = ctx.apvts.getParameter (id))
        return p->getCurrentValueAsText();
    return {};
}

void ValueField::timerCallback()
{
    if (! isBeingEdited())
    {
        const auto d = display();
        if (d != getText())
            setText (d, juce::dontSendNotification);
    }
}

void ValueField::editorShown (juce::TextEditor* ed)
{
    if (ed == nullptr)
        return;
    ed->setJustification (juce::Justification::centredRight);
    ed->setFont (semibold (12.5f));
    ed->setText (getText().upToFirstOccurrenceOf (" ", false, false), false);
    ed->selectAll();
}

void ValueField::textWasEdited()
{
    const auto t = getText();
    if (freq)
    {
        const double f = eqedit::parseFrequency (t);
        if (std::isnan (f) || f < 20.0 || f > 20000.0)
        {
            if (ctx.notify)
                ctx.notify ("Type 20 Hz - 20 kHz, or a note such as E1.");
        }
        else
            ctx.setParam (id, (float) f);
    }
    else
    {
        const auto clean = t.replace (minusSign(), "-").replace (",", ".").retainCharacters ("-+0123456789.");
        if (clean.isNotEmpty())
            if (auto* p = ctx.apvts.getParameter (id))
                ctx.setParam (id, p->getNormalisableRange().snapToLegalValue (clean.getFloatValue()));
    }
    setText (display(), juce::dontSendNotification);
    if (onCommitted)
        onCommitted();
}

void ValueField::paint (juce::Graphics& g)
{
    if (! isBeingEdited())
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (col::sunk);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (isMouseOver() ? juce::Colour (0xff6b5d2a) : col::line2);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
        g.setColour (col::text);
        g.setFont (semibold (12.5f));
        g.drawText (getText(), getLocalBounds().reduced (6, 0), juce::Justification::centredRight, true);
    }
}

// ---- PowerToggle -----------------------------------------------------------------------------------------
void PowerToggle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    r = r.withSizeKeepingCentre (std::min (r.getWidth(), r.getHeight()), std::min (r.getWidth(), r.getHeight()));
    g.setColour (on ? col::yellow : col::panel2);
    g.fillEllipse (r);
    g.setColour (on ? col::yellow : col::line2);
    g.drawEllipse (r, 1.0f);
    const auto c = r.getCentre();
    const float s = r.getWidth() * 0.27f;
    juce::Path p;
    p.addCentredArc (c.x, c.y, s, s, 0.0f, juce::degreesToRadians (40.0f), juce::degreesToRadians (320.0f), true);
    g.setColour (on ? juce::Colour (0xff1b1600) : col::text3);
    g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine (c.x, c.y - s * 1.25f, c.x, c.y - s * 0.2f, 1.6f);
}

void PowerToggle::mouseDown (const juce::MouseEvent&)
{
    if (onClick)
        onClick();
}

// ---- BandFields ------------------------------------------------------------------------------------------
BandFields::BandFields (Context& c, bool panelLayout) : ctx (c), panel (panelLayout)
{
    snap.setClickingTogglesState (false);
    snap.onClick = [this]
    {
        ctx.eqSnap = ! ctx.eqSnap;
        if (ctx.eqSnap && band > 0)
            eqedit::set (ctx, params::eq (band, "freq"), (float) eqedit::snapToNote (ctx.param (params::eq (band, "freq"))));
        update();
    };
    setTip (snap, "c:eq.snap");
    del.onClick = [this]
    {
        if (band > 0)
            eqedit::deleteBand (ctx, band);
    };
    setTip (del, "c:eq.del");
    power.onClick = [this]
    {
        if (band > 0)
            eqedit::set (ctx, params::eq (band, "on"), ctx.param (params::eq (band, "on")) >= 0.5f ? 0.0f : 1.0f);
        power.repaint();
    };
    setTip (power, "c:eq.pow");
    if (panel)
    {
        for (juce::Component* w : { (juce::Component*) &power, (juce::Component*) &type, (juce::Component*) &place, (juce::Component*) &dynamic })
            addAndMakeVisible (*w);
        setTip (type, "t:Band type\nBell, shelf or notch. Band 1 can be a low cut, band 10 a high cut.");
        setTip (place, "t:Placement\nStereo, Mid, Side, Left or Right: where the band works.");
        setTip (dynamic, "t:Dynamic\nThe band works only when the level in the band is above its threshold.");
    }
    addAndMakeVisible (snap);
    addAndMakeVisible (del);
    addChildComponent (slope);
}

void BandFields::setBand (int b)
{
    if (b == band)
        return;
    band = b;
    rebuild();
}

void BandFields::rebuild()
{
    if (band <= 0)
        return;
    if (panel)
    {
        type.attach (ctx.apvts, params::eq (band, "type"));
        place.attach (ctx.apvts, params::eq (band, "place"));
        dynamic.attach (ctx.apvts, params::eq (band, "dyn"));
    }
    if (band == 1 || band == 10)
        slope.attach (ctx.apvts, params::eq (band, "slope"));
    setTip (slope, "t:Slope\nHow steep the cut is, in dB per octave.");
    const auto mk = [this] (std::unique_ptr<ValueField>& f, const char* field, bool isFreq)
    {
        if (f == nullptr)
        {
            f = std::make_unique<ValueField> (ctx, params::eq (band, field), isFreq);
            addAndMakeVisible (*f);
        }
        else
            f->rebind (params::eq (band, field));
    };
    mk (freqField, "freq", true);
    mk (gainField, "gain", false);
    mk (qField, "q", false);
    cutShown = ! eqedit::isCut (ctx, band); // force a layout pass in update()
    update();
}

juce::Point<int> BandFields::preferredSize() const { return panel ? juce::Point<int> { 474, 66 } : juce::Point<int> { 292, 62 }; }

void BandFields::update()
{
    if (band <= 0)
        return;
    const bool cut = eqedit::isCut (ctx, band), notch = eqedit::isNotch (ctx, band);
    if (cut != cutShown || notch != notchShown)
    {
        cutShown = cut;
        notchShown = notch;
        slope.setVisible (cut);
        gainField->setVisible (! cut && ! notch);
        qField->setVisible (! cut);
        dynamic.setVisible (panel && ! cut);
        resized();
    }
    power.on = ctx.param (params::eq (band, "on")) >= 0.5f;
    snap.setToggleState (ctx.eqSnap, juce::dontSendNotification);
    const auto n = eqedit::noteText (ctx.param (params::eq (band, "freq")));
    if (n != noteShown)
    {
        noteShown = n;
        repaint();
    }
    power.repaint();
}

void BandFields::paint (juce::Graphics& g)
{
    if (panel)
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.fillRoundedRectangle (r.translated (0, 3), 8.0f);
        g.setColour (juce::Colour (0xff20242c));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xff7a6a2c));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (col::yellow);
        g.setFont (semibold (13.0f));
        g.drawText ("Band " + juce::String (band), titleArea, juce::Justification::centredLeft);
    }
    else
    {
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (col::panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
    }
    g.setFont (font (12.5f));
    g.setColour (col::text3);
    g.drawText ("Freq", labelFreq, juce::Justification::centredLeft);
    if (! notchShown)
        g.drawText (cutShown ? "Slope" : "Gain", labelGain, juce::Justification::centredLeft);
    if (! cutShown)
        g.drawText ("Q", labelQ, juce::Justification::centredLeft);
    g.setColour (col::yellow);
    g.setFont (semibold (12.5f));
    g.drawText (noteShown, noteArea, juce::Justification::centredLeft);
}

void BandFields::resized()
{
    if (freqField == nullptr)
        return;
    const int h = 22;
    auto r = getLocalBounds().reduced (8, 6);
    auto row1 = r.removeFromTop (h);
    r.removeFromTop (panel ? 8 : 6);
    auto row2 = r.removeFromTop (h);
    const auto take = [] (juce::Rectangle<int>& row, int w) { auto a = row.removeFromLeft (w); row.removeFromLeft (6); return a; };
    if (panel)
    {
        power.setBounds (take (row1, 22));
        titleArea = take (row1, 56);
        type.setBounds (take (row1, 104));
        place.setBounds (take (row1, 80));
        if (! cutShown)
            dynamic.setBounds (take (row1, 74));
        snap.setBounds (take (row1, 66));
        del.setBounds (row1.removeFromRight (24));
        labelFreq = take (row2, 34);
        freqField->setBounds (take (row2, 76));
        noteArea = take (row2, 70);
    }
    else
    {
        labelFreq = take (row1, 40);
        freqField->setBounds (take (row1, 76));
        noteArea = take (row1, 66);
        del.setBounds (row1.removeFromRight (24));
        row1.removeFromRight (6);
        snap.setBounds (row1.removeFromRight (66));
    }
    labelGain = take (row2, panel ? 38 : 40);
    if (cutShown)
        slope.setBounds (take (row2, 110));
    else if (! notchShown)
        gainField->setBounds (take (row2, 76));
    labelQ = take (row2, 16);
    qField->setBounds (take (row2, 60));
}
} // namespace mm::ui
