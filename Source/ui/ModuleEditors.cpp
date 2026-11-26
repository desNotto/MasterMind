#include "ui/ModuleEditors.h"
#include "ui/Locale.h"

#include "dsp/SatStereo.h"
#include "state/Parameters.h"

#include <algorithm>
#include <limits>

namespace mm::ui
{
namespace
{
constexpr int headH = 32;
constexpr int knobH = 64;
const juce::Colour satColours[3] = { juce::Colour (0xff4fc6e0), juce::Colour (0xffff9f43), juce::Colour (0xffb78cff) };

float noSignal() { return std::numeric_limits<float>::quiet_NaN(); }

juce::String hzShort (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 0 : 1) + "k" : juce::String (juce::roundToInt (f)); }
juce::String hzText (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }

/** Small painted element (readouts, section boxes). */
class Painter final : public juce::Component
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<int>)> draw;
    void paint (juce::Graphics& g) override
    {
        if (draw)
            draw (g, getLocalBounds());
    }
};

void drawSectionBox (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r.toFloat(), 7.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, 1.0f);
}

/** Gain reduction of the last 10 seconds (mockup v6, Bus Comp, Clipper, Limiter). */
class GrHistory final : public juce::Component
{
public:
    GrHistory (std::function<float()> src, float rangeDb, juce::String n) : source (std::move (src)), range (rangeDb), note (std::move (n)) { buf.fill (0.0f); }
    void push()
    {
        buf[(size_t) pos] = std::min (0.0f, source());
        pos = (pos + 1) % (int) buf.size();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        drawSectionBox (g, getLocalBounds());
        auto r = getLocalBounds().reduced (10, 6);
        auto head = r.removeFromTop (18);
        g.setColour (col::text);
        g.setFont (semibold (13.0f));
        g.drawText (tr ("Gain reduction, last 10 s"), head, juce::Justification::centredLeft);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (note, head, juce::Justification::centredRight);
        r.removeFromTop (4);
        auto plot = r.withTrimmedRight (34).toFloat();
        if (plot.getHeight() < 16)
            return;
        g.setColour (col::sunk);
        g.fillRect (plot);
        for (int i = 0; i <= 2; ++i)
        {
            const float y = plot.getY() + plot.getHeight() * i / 2.0f;
            g.setColour (col::line);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (col::text3);
            g.drawText (i == 0 ? juce::String ("0 dB") : fmtMinus (-range * i / 2.0f, 0) + " dB", juce::Rectangle<float> (plot.getRight() + 3, y - 7, 34, 14), juce::Justification::left);
        }
        juce::Path p;
        const int n = (int) buf.size();
        p.startNewSubPath (plot.getX(), plot.getY());
        for (int i = 0; i < n; ++i)
        {
            const float v = buf[(size_t) ((pos + i) % n)];
            p.lineTo (plot.getX() + plot.getWidth() * i / (n - 1.0f), plot.getY() + juce::jlimit (0.0f, 1.0f, -v / range) * plot.getHeight());
        }
        p.lineTo (plot.getRight(), plot.getY());
        p.closeSubPath();
        g.setColour (col::orange.withAlpha (0.35f));
        g.fillPath (p);
        g.setColour (col::orange);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }
    std::function<float()> source;
    float range;
    juce::String note;
    std::array<float, 300> buf {};
    int pos = 0;
};

/** Ten band numbers of the Dynamic EQ (click selects, right-click switches the band on or off). */
class BandChips final : public juce::Component
{
public:
    explicit BandChips (Context& c) : ctx (c) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        const float w = (getWidth() - 36.0f) / 10.0f;
        for (int b = 1; b <= 10; ++b)
        {
            const juce::Rectangle<float> r ((b - 1) * (w + 4.0f), 0.0f, w, (float) getHeight());
            const bool used = eqedit::bandUsed (ctx, b);
            const bool sel = ctx.selectedEqBand == b;
            g.setColour (sel ? col::yellow : col::sunk);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (sel ? col::yellow : (used ? juce::Colour (0xff6b5d2a) : col::line2));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (sel ? juce::Colour (0xff1b1600) : (used ? col::text : col::text3));
            g.setFont (font (12.5f, sel));
            g.drawText (juce::String (b), r, juce::Justification::centred);
        }
    }
    int bandAt (float x) const { return juce::jlimit (1, 10, 1 + (int) (x / ((getWidth() + 4.0f) / 10.0f))); }
    void mouseMove (const juce::MouseEvent& e) override { setTip (*this, "c:eq.band:" + juce::String (bandAt (e.position.x))); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int b = bandAt (e.position.x);
        if (e.mods.isPopupMenu())
        {
            ctx.setParam ("eq_on", 1.0f);
            ctx.setParam (params::eq (b, "on"), ctx.param (params::eq (b, "on")) >= 0.5f ? 0.0f : 1.0f);
        }
        ctx.selectedEqBand = b;
        repaint();
    }
    Context& ctx;
};
} // namespace

// ==================================================================================================
const std::array<ModuleInfo, 10>& moduleList()
{
    static const std::array<ModuleInfo, 10> list { { { "in", "Input", "in_on" }, { "eq", "Dynamic EQ", "eq_on" }, { "mb", "Multiband", "mb_on" },
                                                     { "le", "Low-End", "lem_on" }, { "bc", "Bus Comp", "bus_on" }, { "sat", "Saturation", "sat_on" },
                                                     { "st", "Stereo", "st_on" }, { "cl", "Clipper", "clip_on" }, { "li", "Limiter", "lim_on" },
                                                     { "out", "Output", "out_on" } } };
    return list;
}

int knobWidthFor (const juce::String& label) { return std::max (52, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.0f), label)) + 10); }

int layoutKnobRow (juce::Rectangle<int> row, const std::vector<Knob*>& ks, int gap)
{
    int x = row.getX();
    for (auto* k : ks)
    {
        const int w = (int) k->getProperties().getWithDefault ("w", 56);
        k->setBounds (x, row.getY(), w, (int) k->preferredHeight());
        x += w + gap;
    }
    return x - row.getX();
}

// ==================================================================================================
class ModuleBar::Tile final : public juce::Component
{
public:
    Tile (Context& c, int i) : ctx (c), idx (i), led (c.apvts, moduleList()[(size_t) i].onParam)
    {
        addAndMakeVisible (led);
        const juce::String key = moduleList()[(size_t) i].key;
        setTip (*this, "m:" + key);
        setTip (led, "m:" + key);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void resized() override { led.setBounds (getWidth() - 20, 4, 16, 16); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasClicked())
            ctx.selectedModule = idx;
    }

    void paint (juce::Graphics& g) override
    {
        const bool sel = ctx.selectedModule == idx, on = led.isOn();
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (sel ? juce::Colour (0xff25221d) : col::panel2);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (sel ? col::orange : col::line);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        const float a = on ? 1.0f : 0.55f;
        static const char* shortNames[] = { "Input", "Dyn EQ", "Multiband", "Low-End", "Bus Comp", "Saturation", "Stereo", "Clipper", "Limiter", "Output" };
        auto in = getLocalBounds().reduced (8, 5);
        g.setColour (col::text.withAlpha (a));
        g.setFont (semibold (13.0f));
        g.drawText (shortNames[idx], in.removeFromTop (17).withTrimmedRight (14), juce::Justification::centredLeft, true);
        auto line = in.removeFromTop (17);
        g.setFont (font (12.5f));
        if (! on)
        {
            g.setColour (col::text3.withAlpha (a));
            g.drawText ("Off", line, juce::Justification::centredLeft);
        }
        else
        {
            const int pw = prefix.isEmpty() ? 0 : juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.5f), prefix + " "));
            g.setColour (col::text3);
            g.drawText (prefix, line.removeFromLeft (pw), juce::Justification::centredLeft);
            g.setColour (valueAccent ? col::orange : col::text3);
            g.setFont (font (12.5f, valueAccent));
            g.drawText (value, line, juce::Justification::centredLeft, true);
        }
        if (! compact)
        {
            auto bar = getLocalBounds().reduced (8, 0).removeFromBottom (9).removeFromTop (4).toFloat();
            g.setColour (col::sunk);
            g.fillRoundedRectangle (bar, 2.0f);
            if (on && gr > 0.0f)
            {
                g.setColour (col::orange);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, gr)), 2.0f);
            }
        }
    }

    void refresh()
    {
        const auto& chain = ctx.proc.getChain();
        prefix.clear();
        value.clear();
        valueAccent = false;
        gr = 0.0f;
        const auto grText = [&] (float db, float range)
        {
            prefix = idx == 7 ? "Peak" : "GR";
            value = fmtMinus (db) + " dB";
            valueAccent = db < -0.05f;
            gr = -db / range;
        };
        switch (idx)
        {
            case 0:
                if (ctx.param ("hpf_on") >= 0.5f)
                    prefix = "HPF", value = juce::String (juce::roundToInt (ctx.param ("hpf_freq"))) + " Hz";
                else
                    prefix = "Trim", value = fmtSigned (ctx.param ("in_trim")) + " dB";
                break;
            case 1:
            {
                int used = 0;
                float maxDyn = 0.0f;
                for (int b = 1; b <= 10; ++b)
                {
                    if (! eqedit::bandUsed (ctx, b))
                        continue;
                    ++used;
                    if (ctx.param (params::eq (b, "dyn")) >= 0.5f && ctx.param (params::eq (b, "on")) >= 0.5f)
                    {
                        const float d = chain.getEq().effectiveGainDb (b - 1) - ctx.param (params::eq (b, "gain"));
                        if (std::abs (d) > std::abs (maxDyn))
                            maxDyn = d;
                    }
                }
                if (std::abs (maxDyn) > 0.05f)
                {
                    prefix = "Dyn";
                    value = fmtSigned (maxDyn) + " dB";
                    valueAccent = true;
                }
                else
                    value = juce::String (used) + (used == 1 ? " band" : " bands");
                break;
            }
            case 2:
            {
                float m = 0.0f;
                for (int b = 0; b < 5; ++b)
                    m = std::min (m, chain.getMultiband().gainReductionDb (b));
                grText (m, 6.0f);
                break;
            }
            case 3: prefix = "Mono", value = juce::String (juce::roundToInt (ctx.param ("lem_mono_freq"))) + " Hz"; break;
            case 4: grText (chain.getBusComp().gainReductionDb(), 6.0f); break;
            case 5:
            {
                float m = 0.0f;
                for (int b = 1; b <= 3; ++b)
                    m = std::max (m, ctx.param (params::sat (b, "drive")));
                prefix = "Max";
                value = juce::String (m, 1) + " dB";
                valueAccent = m > 0.05f;
                break;
            }
            case 6:
                prefix = "Width";
                value = juce::String (juce::roundToInt (ctx.param ("st_mid_width"))) + " / " + juce::String (juce::roundToInt (ctx.param ("st_high_width"))) + "%";
                break;
            case 7: grText (chain.getClipper().peakReductionDb(), 6.0f); break;
            case 8: grText (chain.getLimiter().gainReductionDb(), 6.0f); break;
            default: prefix = "TP", value = fmtLufs (ctx.out().truePeak); break;
        }
        repaint();
    }

    Context& ctx;
    int idx;
    Led led;
    juce::String prefix, value;
    bool valueAccent = false, compact = false;
    float gr = 0.0f;
};

ModuleBar::ModuleBar (Context& c) : ctx (c)
{
    for (int i = 0; i < 10; ++i)
    {
        tiles.push_back (std::make_unique<Tile> (ctx, i));
        addAndMakeVisible (*tiles.back());
    }
}

ModuleBar::~ModuleBar() = default;

void ModuleBar::setCompact (bool c)
{
    compact = c;
    for (auto& t : tiles)
        t->compact = c;
    resized();
}

void ModuleBar::update()
{
    for (auto& t : tiles)
        t->refresh();
    if (shownSelection != ctx.selectedModule)
    {
        shownSelection = ctx.selectedModule;
        resized();
    }
}

void ModuleBar::resized()
{
    const int gap = 6;
    if (! compact)
    {
        const float w = (getWidth() - gap * 9.0f) / 10.0f;
        for (int i = 0; i < 10; ++i)
        {
            tiles[(size_t) i]->setBounds (juce::roundToInt (i * (w + gap)), 0, juce::roundToInt (w), getHeight());
            tiles[(size_t) i]->setVisible (true);
        }
        return;
    }
    // compact: fixed-width tiles, the selected one stays in view
    const int w = 96, visible = std::max (1, (getWidth() + gap) / (w + gap));
    const int first = juce::jlimit (0, std::max (0, 10 - visible), ctx.selectedModule - visible / 2);
    for (int i = 0; i < 10; ++i)
    {
        const int x = (i - first) * (w + gap);
        tiles[(size_t) i]->setBounds (x, 0, w, getHeight());
        tiles[(size_t) i]->setVisible (x >= 0 && x + w <= getWidth());
    }
}

// ==================================================================================================
ModuleEditor::ModuleEditor (Context& c, int moduleIndex, juce::String sub) : ctx (c), index (moduleIndex), subtitle (std::move (sub))
{
    led = std::make_unique<Led> (ctx.apvts, moduleList()[(size_t) index].onParam);
    addAndMakeVisible (*led);
    setTip (*led, "m:" + juce::String (moduleList()[(size_t) index].key));
}

ModuleEditor::~ModuleEditor() = default;

bool ModuleEditor::moduleOn() const { return ctx.param (moduleList()[(size_t) index].onParam) >= 0.5f; }

void ModuleEditor::setCompact (bool c)
{
    if (c == compact)
        return;
    compact = c;
    compactChanged();
    resized();
}

void ModuleEditor::addRight (juce::Component& c, int width)
{
    if (c.getParentComponent() != this)
        addAndMakeVisible (c);
    right.push_back ({ &c, width });
}

Knob& ModuleEditor::makeKnob (juce::Component& parent, const juce::String& id, const juce::String& label, const juce::String& tipId, juce::Colour colour, bool bipolar)
{
    auto k = std::make_unique<Knob> (ctx.apvts, id, label, colour, bipolar);
    setTip (*k, "k:" + (tipId.isNotEmpty() ? tipId : id));
    k->getProperties().set ("w", knobWidthFor (label));
    auto& ref = *k;
    parent.addAndMakeVisible (ref);
    knobs.push_back (&ref);
    owned.push_back (std::move (k));
    return ref;
}

void ModuleEditor::update()
{
    for (auto* k : knobs)
        k->updateMeter();
    const bool on = moduleOn();
    if (on != lastOn)
    {
        lastOn = on;
        for (auto* child : getChildren())
        {
            const bool isRight = std::any_of (right.begin(), right.end(), [child] (const auto& p) { return p.first == child; });
            if (child != led.get() && ! isRight)
                child->setAlpha (on ? 1.0f : 0.55f);
        }
    }
}

void ModuleEditor::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    auto head = getLocalBounds().removeFromTop (headH);
    g.setColour (col::line);
    g.drawHorizontalLine (head.getBottom() - 1, 1.0f, (float) getWidth() - 1.0f);
    auto t = head.withTrimmedLeft (30);
    int rightEdge = getWidth() - 10;
    for (const auto& [c, w] : right)
        rightEdge = std::min (rightEdge, c->getX() - 8);
    t.setRight (rightEdge);
    const juce::String title = moduleList()[(size_t) index].name;
    g.setColour (col::text);
    g.setFont (semibold (14.0f));
    const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (semibold (14.0f), title)) + 10;
    g.drawText (title, t.removeFromLeft (tw), juce::Justification::centredLeft, false);
    if (subtitle.isNotEmpty() && ! compact)
    {
        g.setColour (col::text3);
        g.setFont (font (12.5f));
        g.drawText (tr (subtitle), t, juce::Justification::centredLeft, true);
    }
}

void ModuleEditor::resized()
{
    led->setBounds (10, 9, 14, 14);
    auto head = getLocalBounds().removeFromTop (headH).reduced (10, 4);
    for (const auto& [c, w] : right)
    {
        if (! c->isVisible())
            continue;
        c->setBounds (head.removeFromRight (w).withSizeKeepingCentre (w, 24));
        head.removeFromRight (6);
    }
    layoutBody (getLocalBounds().withTrimmedTop (headH).reduced (10, 8));
}

// ==================================================================================================
namespace
{
struct KSpec
{
    const char* id;
    const char* label;
    const char* tip;
    bool bipolar = false;
};

/** Knob row, header controls and an optional gain reduction history (Input, Low-End, Bus Comp,
    Stereo, Clipper, Limiter, Output). */
class GenericEditor final : public ModuleEditor
{
public:
    GenericEditor (Context& c, int idx, const juce::String& sub, juce::Colour colour, const std::vector<KSpec>& specs) : ModuleEditor (c, idx, sub)
    {
        for (const auto& s : specs)
            row.push_back (&makeKnob (*this, s.id, s.label, s.tip, colour, s.bipolar));
    }

    using ModuleEditor::addRight;
    using ModuleEditor::ctx;
    using ModuleEditor::own;

    Knob* knob (const char* id)
    {
        for (auto* k : row)
            if (k->getParamId() == id)
                return k;
        return nullptr;
    }

    Btn& toggle (const juce::String& text, const char* paramId, const juce::String& tip, int width)
    {
        auto& b = own (std::make_unique<Btn> (text, true));
        if (paramId != nullptr)
            b.attach (ctx.apvts, paramId);
        setTip (b, tip);
        addRight (b, width);
        return b;
    }

    Dropdown& dropdown (const char* paramId, const juce::String& tip, int width)
    {
        auto& d = own (std::make_unique<Dropdown>());
        d.attach (ctx.apvts, paramId);
        setTip (d, tip);
        addRight (d, width);
        return d;
    }

    void readout (std::function<juce::String()> text, int width)
    {
        auto& p = own (std::make_unique<Painter>());
        p.setInterceptsMouseClicks (false, false);
        p.draw = [text] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            const auto t = text();
            const auto label = t.upToFirstOccurrenceOf (" ", false, false), val = t.fromFirstOccurrenceOf (" ", false, false);
            g.setFont (font (12.5f));
            const int lw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.5f, true), val)) + 4;
            g.setColour (col::orange);
            g.setFont (font (12.5f, true));
            g.drawText (val, r.removeFromRight (lw), juce::Justification::centredRight);
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText (label, r, juce::Justification::centredRight);
        };
        readouts.push_back (&p);
        addRight (p, width);
    }

    void history (std::function<float()> src, float range, const juce::String& note)
    {
        hist = &own (std::make_unique<GrHistory> (std::move (src), range, note));
    }

    int naturalHeight (int) const override { return headH + 16 + knobH + (hist != nullptr ? 8 + 110 : 0) + 20; }

    void update() override
    {
        ModuleEditor::update();
        if (hist != nullptr)
            hist->push();
        for (auto* r : readouts)
            r->repaint();
        if (onUpdate)
            onUpdate();
    }

    void layoutBody (juce::Rectangle<int> body) override
    {
        layoutKnobRow (body.removeFromTop (knobH), row);
        hintArea = body.removeFromBottom (16);
        body.removeFromBottom (4);
        body.removeFromTop (8);
        if (hist != nullptr)
            hist->setBounds (body);
    }

    void paint (juce::Graphics& g) override
    {
        ModuleEditor::paint (g);
        if (! compact)
        {
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText (tr ("All parameters of the module are here. Hover any control for advice."), hintArea, juce::Justification::centredLeft, true);
        }
    }

    std::vector<Knob*> row;
    std::vector<Painter*> readouts;
    GrHistory* hist = nullptr;
    juce::Rectangle<int> hintArea;
    std::function<void()> onUpdate;
};

// ---- Dynamic EQ ------------------------------------------------------------------------------------------
class EqEditor final : public ModuleEditor
{
public:
    explicit EqEditor (Context& c) : ModuleEditor (c, 1, "10 bands, dynamic per band"), fields (c, false)
    {
        phase.attach (ctx.apvts, "eq_phase");
        setTip (phase, "c:eq.phase");
        addRight (phase, 196);
        bandBox = &own (std::make_unique<Painter>());
        resBox = &own (std::make_unique<Painter>());
        bandBox->setInterceptsMouseClicks (false, false);
        resBox->setInterceptsMouseClicks (false, false);
        chips = &own (std::make_unique<BandChips> (ctx));
        addAndMakeVisible (fields);

        // resonance suppressor
        resLed = &own (std::make_unique<Led> (ctx.apvts, "rs_on"));
        setTip (*resLed, "t:Resonance suppressor\nCuts narrow ringing peaks between Low and High.");
        delta = &own (std::make_unique<Btn> ("Delta", true));
        delta->attach (ctx.apvts, "rs_delta");
        setTip (*delta, "t:Delta\nYou hear only what the suppressor removes.");
        resRead = &own (std::make_unique<Painter>());
        resRead->setInterceptsMouseClicks (false, false);
        resRead->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            const auto v = fmtMinus (ctx.proc.getChain().getEq().resonance.maxReductionDb()) + " dB";
            const int vw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.5f, true), v)) + 4;
            g.setColour (col::orange);
            g.setFont (font (12.5f, true));
            g.drawText (v, r.removeFromRight (vw), juce::Justification::centredRight);
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText ("Res", r, juce::Justification::centredRight);
        };
        for (auto [id, label] : { std::pair { "rs_low", "Low" }, { "rs_high", "High" }, { "rs_depth", "Depth" }, { "rs_sharp", "Sharpness" }, { "rs_speed", "Speed" }, { "rs_mix", "Mix" } })
            resKnobs.push_back (&makeKnob (*this, id, label, {}, col::orange));
        bandBox->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            drawSectionBox (g, r);
            auto t = r.reduced (10, 0).removeFromTop (28);
            g.setColour (col::text);
            g.setFont (semibold (13.0f));
            const auto title = "Band " + juce::String (shownBand);
            const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (semibold (13.0f), title)) + 10;
            g.drawText (title, t.removeFromLeft (tw), juce::Justification::centredLeft);
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            if (bandRight.getX() > t.getX())
                g.drawText (tr ("select it on the spectrum or with the numbers"), t.withRight (bandRight.getX() - 8), juce::Justification::centredLeft, true);
        };
        resBox->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            drawSectionBox (g, r);
            auto t = r.withTrimmedLeft (30).withTrimmedRight (10).removeFromTop (28);
            g.setColour (col::text);
            g.setFont (semibold (13.0f));
            const juce::String title = "Resonance suppressor";
            const int tw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (semibold (13.0f), title)) + 10;
            g.drawText (title, t.removeFromLeft (tw), juce::Justification::centredLeft);
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText (tr ("cuts narrow ringing peaks"), t.withRight (delta->getX() - 8), juce::Justification::centredLeft, true);
        };
        rebuildBand();
    }

    int naturalHeight (int) const override { return headH + 8 + 24 + 8 + 104 + 8 + 104 + 8; }

    void update() override
    {
        if (! eqedit::bandUsed (ctx, ctx.selectedEqBand))
            for (int b = 1; b <= 10; ++b)
                if (eqedit::bandUsed (ctx, b))
                {
                    ctx.selectedEqBand = b;
                    break;
                }
        const int b = ctx.selectedEqBand;
        const bool cut = eqedit::isCut (ctx, b), dyn = ctx.param (params::eq (b, "dyn")) >= 0.5f && ! cut;
        if (b != shownBand || cut != shownCut || dyn != shownDyn)
        {
            rebuildBand();
            resized();
        }
        for (auto* k : bandKnobs)
            k->updateMeter();
        fields.update();
        chips->repaint();
        resRead->repaint();
        ModuleEditor::update();
    }

    void rebuildBand()
    {
        const int b = juce::jlimit (1, 10, ctx.selectedEqBand);
        shownBand = b;
        shownCut = eqedit::isCut (ctx, b);
        shownDyn = ctx.param (params::eq (b, "dyn")) >= 0.5f && ! shownCut;
        bandKnobs.clear();
        bandRightCtl.clear();
        bandComps.clear();
        const auto knob = [this, b] (const char* field, const char* label, const char* tip, bool bi = false) -> Knob&
        {
            auto k = std::make_unique<Knob> (ctx.apvts, params::eq (b, field), label, col::yellow, bi);
            setTip (*k, juce::String ("k:") + tip);
            k->getProperties().set ("w", knobWidthFor (label));
            auto& ref = *k;
            addAndMakeVisible (ref);
            bandKnobs.push_back (&ref);
            bandComps.push_back (std::move (k));
            return ref;
        };
        const auto ctl = [this] (std::unique_ptr<juce::Component> c, int w)
        {
            addAndMakeVisible (*c);
            bandRightCtl.push_back ({ c.get(), w });
            bandComps.push_back (std::move (c));
        };
        knob ("freq", "Freq", "eq.freq");
        if (shownCut)
            knob ("slope", "Slope", "eq.slope");
        else
        {
            if (! eqedit::isNotch (ctx, b))
                knob ("gain", "Gain", "eq.gain", true);
            knob ("q", "Q", "eq.q");
        }
        if (shownDyn)
        {
            auto& thr = knob ("thr", "Thresh", "eq.thr");
            thr.meterLevel = [this, b]
            {
                if (! moduleOn() || ctx.param (params::eq (b, "on")) < 0.5f)
                    return noSignal();
                const float v = ctx.proc.getChain().getEq().detectorLevelDb (b - 1);
                return v <= -99.0f ? noSignal() : v;
            };
            knob ("range", "Range", "eq.range");
            knob ("att", "Attack", "eq.att");
            knob ("rel", "Release", "eq.rel");
        }
        auto on = std::make_unique<Btn> ("On", true);
        on->attach (ctx.apvts, params::eq (b, "on"));
        setTip (*on, "t:Band on/off\nSwitches the band on and off. Its settings stay.");
        ctl (std::move (on), 40);
        auto type = std::make_unique<Dropdown>();
        type->attach (ctx.apvts, params::eq (b, "type"));
        setTip (*type, "t:Band type\nBell, shelf or notch. Band 1 can be a low cut, band 10 a high cut.");
        ctl (std::move (type), 104);
        auto place = std::make_unique<Dropdown>();
        place->attach (ctx.apvts, params::eq (b, "place"));
        setTip (*place, "t:Placement\nStereo, Mid, Side, Left or Right: where the band works.");
        ctl (std::move (place), 84);
        if (! shownCut)
        {
            auto dynB = std::make_unique<Btn> ("Dynamic", true);
            dynB->attach (ctx.apvts, params::eq (b, "dyn"));
            setTip (*dynB, "t:Dynamic\nThe band works only when the level in the band is above its threshold.");
            ctl (std::move (dynB), 74);
            auto mode = std::make_unique<Dropdown>();
            mode->attach (ctx.apvts, params::eq (b, "mode"));
            setTip (*mode, "t:Dynamic mode\nCompress: the band cuts when the level is above the threshold. Expand: it boosts.");
            ctl (std::move (mode), 98);
        }
        fields.setBand (b);
        fields.toFront (false);
        if (bandBox != nullptr)
            bandBox->repaint();
    }

    void layoutBody (juce::Rectangle<int> r) override
    {
        auto top = r.removeFromTop (24);
        chips->setBounds (top.removeFromLeft (334));
        chipHint = top.withTrimmedLeft (12);
        r.removeFromTop (8);
        auto band = r.removeFromTop (104);
        bandBox->setBounds (band);
        {
            auto in = band.reduced (10, 4);
            auto title = in.removeFromTop (24);
            for (auto it = bandRightCtl.rbegin(); it != bandRightCtl.rend(); ++it)
            {
                it->first->setBounds (title.removeFromRight (it->second).withSizeKeepingCentre (it->second, 22));
                title.removeFromRight (6);
            }
            bandRight = title.withLeft (title.getRight());
            in.removeFromTop (6);
            const auto fs = fields.preferredSize();
            fields.setBounds (in.removeFromRight (fs.x).withSizeKeepingCentre (fs.x, fs.y));
            std::vector<Knob*> statics, dyns;
            for (size_t i = 0; i < bandKnobs.size(); ++i)
                (i < (shownCut ? 2u : (eqedit::isNotch (ctx, shownBand) ? 2u : 3u)) ? statics : dyns).push_back (bandKnobs[i]);
            auto kr = in.removeFromTop (knobH);
            const int used = layoutKnobRow (kr, statics);
            layoutKnobRow (kr.withTrimmedLeft (used + 14), dyns);
        }
        r.removeFromTop (8);
        auto res = r.removeFromTop (104);
        resBox->setBounds (res);
        {
            auto in = res.reduced (10, 4);
            auto title = in.removeFromTop (24);
            resLed->setBounds (title.removeFromLeft (14).withSizeKeepingCentre (14, 14));
            resRead->setBounds (title.removeFromRight (90));
            title.removeFromRight (6);
            delta->setBounds (title.removeFromRight (56).withSizeKeepingCentre (56, 22));
            in.removeFromTop (6);
            layoutKnobRow (in.removeFromTop (knobH), resKnobs);
        }
    }

    void paint (juce::Graphics& g) override
    {
        ModuleEditor::paint (g);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (tr ("Click a number to select") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + tr ("edit points on the spectrum"), chipHint, juce::Justification::centredLeft, true);
    }

    Segmented phase { { "Min phase", "Linear phase" } };
    BandFields fields;
    BandChips* chips = nullptr;
    Painter *bandBox = nullptr, *resBox = nullptr, *resRead = nullptr;
    Led* resLed = nullptr;
    Btn* delta = nullptr;
    std::vector<Knob*> resKnobs, bandKnobs;
    std::vector<std::pair<juce::Component*, int>> bandRightCtl;
    std::vector<std::unique_ptr<juce::Component>> bandComps;
    int shownBand = -1;
    bool shownCut = false, shownDyn = false;
    juce::Rectangle<int> chipHint, bandRight;
};

// ---- Multiband -------------------------------------------------------------------------------------------
class XoBar final : public juce::Component
{
public:
    explicit XoBar (Context& c) : ctx (c) { setTip (*this, "t:Crossovers\nDrag a yellow handle to move a crossover frequency."); }
    float xOf (float f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * getWidth(); }
    float fOf (float x) const { return (float) (20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) x / getWidth()))); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::sunk);
        g.fillRoundedRectangle (r, 6.0f);
        static const char* names[] = { "Low", "Lo-mid", "Mid", "Hi-mid", "High" };
        float edges[6] = { 20.0f, 0, 0, 0, 0, 20000.0f };
        for (int i = 1; i <= 4; ++i)
            edges[i] = ctx.param (params::mbXo (i));
        for (int b = 0; b < 5; ++b)
        {
            const float x0 = xOf (edges[b]), x1 = xOf (edges[b + 1]);
            const float gr = std::max (0.0f, -ctx.proc.getChain().getMultiband().gainReductionDb (b));
            g.setColour (col::orange.withAlpha (0.28f));
            g.fillRect (juce::Rectangle<float> (x0, 0.0f, x1 - x0, r.getHeight() * juce::jlimit (0.0f, 1.0f, gr / 7.0f)));
            auto t = juce::Rectangle<float> (x0 + (b == 0 ? 6.0f : 44.0f), 0.0f, x1 - x0 - (b == 0 ? 8.0f : 46.0f), r.getHeight());
            g.setColour (col::text);
            g.setFont (semibold (12.5f));
            const int nw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (semibold (12.5f), names[b])) + 5;
            g.drawText (names[b], t.removeFromLeft ((float) nw), juce::Justification::centredLeft, true);
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText ((gr > 0.05f ? minusSign() : juce::String()) + juce::String (gr, 1) + " dB", t, juce::Justification::centredLeft, true);
        }
        for (int i = 1; i <= 4; ++i)
        {
            const float x = xOf (edges[i]);
            g.setColour (col::yellow);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, r.getHeight());
            const auto label = hzShort (edges[i]);
            const float w = juce::GlyphArrangement::getStringWidth (font (12.0f, true), label) + 10.0f;
            const juce::Rectangle<float> pill (x + 3.0f, 5.0f, w, 17.0f);
            g.fillRoundedRectangle (pill, 4.0f);
            g.setColour (juce::Colour (0xff1b1600));
            g.setFont (font (12.0f, true));
            g.drawText (label, pill, juce::Justification::centred);
        }
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    int handleAt (float x) const
    {
        for (int i = 1; i <= 4; ++i)
        {
            const float hx = xOf (ctx.param (params::mbXo (i)));
            if (x >= hx - 6.0f && x <= hx + 40.0f)
                return i;
        }
        return 0;
    }
    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (handleAt (e.position.x) > 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = handleAt (e.position.x);
        if (drag > 0)
        {
            ctx.apvts.getParameter (params::mbXo (drag))->beginChangeGesture();
            grab = e.position.x - xOf (ctx.param (params::mbXo (drag)));
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag <= 0)
            return;
        const float lo = drag > 1 ? ctx.param (params::mbXo (drag - 1)) * 1.25f : 30.0f;
        const float hi = drag < 4 ? ctx.param (params::mbXo (drag + 1)) / 1.25f : 16000.0f;
        const float f = juce::jlimit (lo, hi, fOf (e.position.x - grab));
        auto* p = ctx.apvts.getParameter (params::mbXo (drag));
        p->setValueNotifyingHost (p->convertTo0to1 (f));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag > 0)
            ctx.apvts.getParameter (params::mbXo (drag))->endChangeGesture();
        drag = 0;
    }
    Context& ctx;
    int drag = 0;
    float grab = 0.0f;
};

class BandColumn final : public juce::Component
{
public:
    BandColumn (Context& c, int bandIndex, std::function<Knob& (juce::Component&, const juce::String&, const juce::String&, const juce::String&, juce::Colour, bool)> mk)
        : ctx (c), band (bandIndex)
    {
        for (auto [b, field, tip] : { std::tuple { &solo, "solo", "t:Solo\nYou hear only this band." }, { &bypass, "bypass", "t:Bypass\nThe band passes without compression." },
                                      { &ms, "mode", "t:Mid/Side\nOn: the band compresses mid and side separately. Off: stereo." } })
        {
            b->attach (ctx.apvts, params::mb (band, field));
            setTip (*b, tip);
            addAndMakeVisible (*b);
        }
        static const std::tuple<const char*, const char*, bool> fields[] = { { "thr", "Thresh", false }, { "ratio", "Ratio", false }, { "makeup", "Makeup", true },
                                                                              { "att", "Attack", false }, { "rel", "Release", false }, { "knee", "Knee", false },
                                                                              { "mix", "Mix", false }, { "link", "Link", false }, { "trim", "Trim", true } };
        for (const auto& [field, label, bi] : fields)
            ks.push_back (&mk (*this, params::mb (band, field), label, "mb." + juce::String (label).toLowerCase(), col::orange, bi));
        ks[0]->meterLevel = [this]
        {
            if (ctx.param ("mb_on") < 0.5f || ctx.param (params::mb (band, "bypass")) >= 0.5f)
                return noSignal();
            const float v = ctx.proc.getChain().getMultiband().bandLevelDb (band - 1);
            return v <= -99.0f ? noSignal() : v;
        };
        auto* rel = ks[4];
        rel->textOverride = [this, rel] { return ctx.param (params::mb (band, "rel_auto")) >= 0.5f ? juce::String ("Auto") : ctx.apvts.getParameter (rel->getParamId())->getCurrentValueAsText(); };
        rel->onUserChange = [this] { ctx.setParam (params::mb (band, "rel_auto"), 0.0f); };
        rel->menuItems.push_back ({ "Auto release", [this] { return ctx.param (params::mb (band, "rel_auto")) >= 0.5f; },
                                    [this] { ctx.setParam (params::mb (band, "rel_auto"), ctx.param (params::mb (band, "rel_auto")) >= 0.5f ? 0.0f : 1.0f); } });
    }

    void paint (juce::Graphics& g) override
    {
        const bool isSolo = ctx.param (params::mb (band, "solo")) >= 0.5f;
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (! oneRow)
        {
            g.setColour (col::panel2);
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (isSolo ? col::yellow : col::line);
            g.drawRoundedRectangle (r, 7.0f, 1.0f);
            static const char* names[] = { "Low", "Lo-mid", "Mid", "Hi-mid", "High" };
            auto t = headArea;
            g.setColour (col::text);
            g.setFont (semibold (13.0f));
            const int nw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (semibold (13.0f), names[band - 1])) + 5;
            g.drawText (names[band - 1], t.removeFromLeft (nw), juce::Justification::centredLeft);
            const float lo = band > 1 ? ctx.param (params::mbXo (band - 1)) : 0.0f, hi = band < 5 ? ctx.param (params::mbXo (band)) : 0.0f;
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText (band == 1 ? "<" + hzShort (hi) : (band == 5 ? ">" + hzShort (lo) : hzShort (lo) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + hzShort (hi)), t, juce::Justification::centredLeft, true);
        }
        // gain reduction bar
        const float gr = std::max (0.0f, -ctx.proc.getChain().getMultiband().gainReductionDb (band - 1));
        auto row = grArea;
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText ("GR", row.removeFromLeft (22), juce::Justification::centredLeft);
        auto val = row.removeFromRight (50);
        g.setColour (col::orange);
        g.setFont (font (12.0f, true));
        g.drawText ((gr > 0.05f ? minusSign() : juce::String()) + juce::String (gr, 1) + " dB", val, juce::Justification::centredRight);
        auto track = row.withSizeKeepingCentre (row.getWidth() - 6, 6).toFloat();
        g.setColour (col::sunk);
        g.fillRoundedRectangle (track, 3.0f);
        g.setColour (col::orange);
        const float w = track.getWidth() * juce::jlimit (0.0f, 1.0f, gr / 10.0f);
        g.fillRoundedRectangle (track.withX (track.getRight() - w).withWidth (w), 3.0f);
    }

    void resized() override
    {
        if (oneRow)
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (20);
            ms.setBounds (top.removeFromRight (34));
            top.removeFromRight (4);
            bypass.setBounds (top.removeFromRight (22));
            top.removeFromRight (4);
            solo.setBounds (top.removeFromRight (22));
            top.removeFromRight (10);
            grArea = top;
            r.removeFromTop (8);
            const int w = std::min (64, r.getWidth() / 9);
            for (int i = 0; i < 9; ++i)
                ks[(size_t) i]->setBounds (r.getX() + i * w, r.getY(), w, knobH);
            return;
        }
        auto r = getLocalBounds().reduced (6, 4);
        auto top = r.removeFromTop (20);
        ms.setBounds (top.removeFromRight (34));
        top.removeFromRight (3);
        bypass.setBounds (top.removeFromRight (20));
        top.removeFromRight (3);
        solo.setBounds (top.removeFromRight (20));
        headArea = top;
        r.removeFromTop (3);
        grArea = r.removeFromTop (12);
        r.removeFromTop (3);
        const int cw = r.getWidth() / 3;
        for (int i = 0; i < 9; ++i)
            ks[(size_t) i]->setBounds (r.getX() + (i % 3) * cw, r.getY() + (i / 3) * knobH, cw, knobH);
    }

    void refresh()
    {
        setAlpha (ctx.param (params::mb (band, "bypass")) >= 0.5f ? 0.6f : 1.0f);
        repaint();
    }

    Context& ctx;
    int band;
    bool oneRow = false;
    Btn solo { "S", true }, bypass { "B", true }, ms { "MS", true };
    std::vector<Knob*> ks;
    juce::Rectangle<int> headArea, grArea;
};

class MultibandEditor final : public ModuleEditor
{
public:
    explicit MultibandEditor (Context& c) : ModuleEditor (c, 2, "5-band dynamics, drag the yellow handles to move the crossovers"), xo (c)
    {
        soloOff.onClick = [this]
        {
            for (int b = 1; b <= 5; ++b)
                ctx.setParam (params::mb (b, "solo"), 0.0f);
        };
        setTip (soloOff, "t:Solo off\nSwitches off the solo of every band.");
        addRight (soloOff, 72);
        xoType.attach (ctx.apvts, "mb_xover_type");
        setTip (xoType, "t:Crossover type\nLinkwitz-Riley: no latency. Linear phase: no phase shift, adds latency.");
        addRight (xoType, 132);
        addChildComponent (bandSel);
        bandSel.onChange = [this] (int i)
        {
            compactBand = i;
            compactChanged();
            resized();
        };
        setTip (bandSel, "t:Band\nSelects the band whose controls are shown.");
        addAndMakeVisible (xo);
        const auto mk = [this] (juce::Component& parent, const juce::String& id, const juce::String& label, const juce::String& tip, juce::Colour colour, bool bi) -> Knob&
        { return makeKnob (parent, id, label, tip, colour, bi); };
        for (int b = 1; b <= 5; ++b)
        {
            cols.push_back (std::make_unique<BandColumn> (ctx, b, mk));
            addAndMakeVisible (*cols.back());
        }
    }

    void compactChanged() override
    {
        soloOff.setVisible (! compact);
        xoType.setVisible (! compact);
        bandSel.setVisible (compact);
        xo.setVisible (! compact);
        for (int b = 0; b < 5; ++b)
        {
            cols[(size_t) b]->oneRow = compact;
            cols[(size_t) b]->setVisible (! compact || b == compactBand);
        }
        right.clear();
        if (compact)
            addRight (bandSel, 300);
        else
        {
            addRight (soloOff, 72);
            addRight (xoType, 132);
        }
    }

    int naturalHeight (int) const override { return compact ? headH + 8 + 20 + 8 + knobH + 8 : headH + 8 + 28 + 6 + 234 + 8; }

    void update() override
    {
        ModuleEditor::update();
        xo.repaint();
        for (auto& c : cols)
            c->refresh();
    }

    void layoutBody (juce::Rectangle<int> r) override
    {
        if (compact)
        {
            cols[(size_t) compactBand]->setBounds (r.removeFromTop (20 + 8 + knobH));
            return;
        }
        xo.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);
        const int gap = 8;
        const int w = (r.getWidth() - 4 * gap) / 5;
        for (int b = 0; b < 5; ++b)
            cols[(size_t) b]->setBounds (r.getX() + b * (w + gap), r.getY(), b == 4 ? r.getRight() - (r.getX() + 4 * (w + gap)) : w, std::min (r.getHeight(), 234));
    }

    XoBar xo;
    Btn soloOff { "Solo off" };
    Dropdown xoType;
    Segmented bandSel { { "Low", "Lo-mid", "Mid", "Hi-mid", "High" } };
    std::vector<std::unique_ptr<BandColumn>> cols;
    int compactBand = 0;
};

// ---- Saturation ------------------------------------------------------------------------------------------
class SatGraph final : public juce::Component
{
public:
    explicit SatGraph (Context& c) : ctx (c) { setTip (*this, "c:sat.graph"); }
    float xOf (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * getWidth(); }
    double fOf (float x) const { return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) x / getWidth())); }
    float plotBottom() const { return getHeight() - 14.0f; }
    float yDrive (float d) const { return plotBottom() - 4.0f - d / 24.0f * (plotBottom() - 26.0f); }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::sunk);
        g.fillRoundedRectangle (r, 6.0f);
        const float pb = plotBottom();
        g.setFont (font (11.0f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xOf (f);
            g.setColour (juce::Colour (0xff1b1f26));
            g.drawVerticalLine ((int) x, 0.0f, pb);
            g.setColour (col::text3);
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x + 3, pb, 30, 14), juce::Justification::centredLeft);
        }
        const float edges[4] = { 20.0f, ctx.param ("sat_xo1_freq"), ctx.param ("sat_xo2_freq"), 20000.0f };
        const auto& snap = ctx.snap;
        const auto& spec = snap.out.specRt;
        static const char* names[] = { "Low", "Mid", "High" };
        static const char* types[] = { "Tube", "Tape", "Transformer", "Diode" };
        for (int b = 0; b < 3; ++b)
        {
            const float x0 = xOf (edges[b]), x1 = xOf (edges[b + 1]);
            const auto c = satColours[b];
            g.setColour (c.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (x0, 0.0f, x1 - x0, pb));
            if (! spec.empty() && spec.size() == snap.freqs.size())
            {
                juce::Path p;
                bool started = false;
                for (size_t i = 0; i < spec.size(); ++i)
                {
                    const double f = snap.freqs[i];
                    if (f < edges[b] || f > edges[b + 1])
                        continue;
                    const float y = juce::jlimit (2.0f, pb, 6.0f + (-6.0f - spec[i]) / 72.0f * (pb - 8.0f));
                    if (! started)
                    {
                        p.startNewSubPath (xOf (f), y);
                        started = true;
                    }
                    else
                        p.lineTo (xOf (f), y);
                }
                if (started)
                {
                    auto fill = p;
                    fill.lineTo (x1, pb);
                    fill.lineTo (x0, pb);
                    fill.closeSubPath();
                    g.setColour (c.withAlpha (0.16f));
                    g.fillPath (fill);
                    g.setColour (c.withAlpha (0.85f));
                    g.strokePath (p, juce::PathStrokeType (1.4f));
                }
            }
            const float d = ctx.param (params::sat (b + 1, "drive"));
            const float y = yDrive (d);
            g.setColour (c.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (x0 + 4, y, x1 - x0 - 8, pb - y));
            g.setColour (c);
            g.drawLine (x0 + 4, y, x1 - 4, y, 2.0f);
            const float cx = (x0 + x1) / 2;
            g.fillEllipse (cx - 6, y - 6, 12, 12);
            g.setColour (col::sunk);
            g.drawEllipse (cx - 6, y - 6, 12, 12, 2.0f);
            const auto label = juce::String (names[b]) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + types[juce::jlimit (0, 3, juce::roundToInt (ctx.param (params::sat (b + 1, "type"))))]
                               + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (d, 1) + " dB";
            const float lw = juce::GlyphArrangement::getStringWidth (font (11.5f, true), label) + 14.0f;
            auto pill = juce::Rectangle<float> (cx - lw / 2, y - 25, lw, 17).constrainedWithin (juce::Rectangle<float> (x0 + 2, 0, x1 - x0 - 4, pb));
            g.setColour (col::sunk);
            g.fillRoundedRectangle (pill, 4.0f);
            g.setColour (c);
            g.drawRoundedRectangle (pill, 4.0f, 1.0f);
            g.setFont (font (11.5f, true));
            g.drawText (label, pill, juce::Justification::centred, true);
        }
        for (int i = 1; i <= 2; ++i)
        {
            const float x = xOf (edges[i]);
            g.setColour (col::yellow);
            g.fillRect (x - 1.0f, 0.0f, 2.0f, pb);
            const auto t = hzText (edges[i]);
            const float w = juce::GlyphArrangement::getStringWidth (font (11.5f, true), t) + 12.0f;
            const juce::Rectangle<float> pill (x - w / 2, pb - 18, w, 16);
            g.fillRoundedRectangle (pill, 4.0f);
            g.setColour (juce::Colour (0xff1b1600));
            g.setFont (font (11.5f, true));
            g.drawText (t, pill, juce::Justification::centred);
        }
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    int hit (juce::Point<float> p, bool& isXo) const
    {
        const float e1 = xOf (ctx.param ("sat_xo1_freq")), e2 = xOf (ctx.param ("sat_xo2_freq"));
        isXo = true;
        if (std::abs (p.x - e1) < 6.0f)
            return 1;
        if (std::abs (p.x - e2) < 6.0f)
            return 2;
        isXo = false;
        const float edges[4] = { 0.0f, e1, e2, (float) getWidth() };
        for (int b = 0; b < 3; ++b)
            if (p.x > edges[b] && p.x < edges[b + 1] && std::abs (p.y - yDrive (ctx.param (params::sat (b + 1, "drive")))) < 9.0f)
                return b + 1;
        return 0;
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        bool xoHit = false;
        const int h = hit (e.position, xoHit);
        setMouseCursor (h == 0 ? juce::MouseCursor::NormalCursor : (xoHit ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragIndex = hit (e.position, dragXo);
        if (dragIndex > 0)
            ctx.apvts.getParameter (id())->beginChangeGesture();
    }
    juce::String id() const { return dragXo ? "sat_xo" + juce::String (dragIndex) + "_freq" : params::sat (dragIndex, "drive"); }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragIndex <= 0)
            return;
        auto* p = ctx.apvts.getParameter (id());
        float v;
        if (dragXo)
            v = (float) (dragIndex == 1 ? juce::jlimit (60.0, 500.0, fOf (e.position.x)) : juce::jlimit (1000.0, 10000.0, fOf (e.position.x)));
        else
            v = juce::jlimit (0.0f, 24.0f, std::round ((plotBottom() - 4.0f - e.position.y) / (plotBottom() - 26.0f) * 48.0f) / 2.0f);
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragIndex > 0)
            ctx.apvts.getParameter (id())->endChangeGesture();
        dragIndex = 0;
    }
    Context& ctx;
    int dragIndex = 0;
    bool dragXo = false;
};

class TransferCurve final : public juce::Component
{
public:
    TransferCurve (Context& c, int b) : ctx (c), band (b) { setTip (*this, "c:sat.curve"); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        const auto colour = satColours[band - 1];
        g.setColour (col::sunk);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        const float half = r.getWidth() / 2 - 6.0f;
        const auto c = r.getCentre();
        const int type = juce::roundToInt (ctx.param (params::sat (band, "type")));
        const double drive = std::pow (10.0, ctx.param (params::sat (band, "drive")) / 20.0) * 1.2;
        const double bias = ctx.param (params::sat (band, "bias")) / 100.0 * 0.5;
        const double mix = ctx.param (params::sat (band, "mix")) / 100.0;
        const auto raw = [&] (double x) { return dsp::Saturation::shape (type, x * drive, bias) - dsp::Saturation::shape (type, 0.0, bias); };
        const double norm = std::max (1.0e-6, std::max (std::abs (raw (1.0)), std::abs (raw (-1.0))));
        const auto fn = [&] (double x) { return mix * raw (x) / norm + (1.0 - mix) * x; };
        const float lv = level;
        g.setColour (colour.withAlpha (0.07f));
        g.fillRect (juce::Rectangle<float> (c.x - lv * half, r.getY() + 1, 2 * lv * half, r.getHeight() - 2));
        g.setColour (juce::Colour (0xff232831));
        g.drawHorizontalLine ((int) c.y, r.getX() + 4, r.getRight() - 4);
        g.drawVerticalLine ((int) c.x, r.getY() + 4, r.getBottom() - 4);
        const float dash[] = { 3.0f, 3.0f };
        g.setColour (juce::Colour (0xff3a414d));
        g.drawDashedLine (juce::Line<float> (c.x - half, c.y + half, c.x + half, c.y - half), dash, 2, 1.0f);
        const auto curve = [&] (double lim)
        {
            juce::Path p;
            bool started = false;
            for (int k = 0; k <= 60; ++k)
            {
                const double x = -1.0 + 2.0 * k / 60.0;
                if (std::abs (x) > lim + 1.0e-9)
                    continue;
                const float px = c.x + (float) x * half, py = c.y - (float) juce::jlimit (-1.2, 1.2, fn (x)) * half;
                started ? p.lineTo (px, py) : p.startNewSubPath (px, py);
                started = true;
            }
            return p;
        };
        g.setColour (colour.withAlpha (0.45f));
        g.strokePath (curve (1.0), juce::PathStrokeType (1.5f));
        if (lv > 0.02f)
        {
            g.setColour (colour);
            g.strokePath (curve (lv), juce::PathStrokeType (2.6f));
        }
    }
    void refresh()
    {
        const bool on = ctx.param ("sat_on") >= 0.5f;
        const float target = on ? juce::jlimit (0.0f, 1.0f, ctx.proc.getChain().getSaturation().bandPeak (band - 1)) : 0.0f;
        level = target > level ? target : std::max (target, level - 0.02f);
        repaint();
    }
    Context& ctx;
    int band;
    float level = 0.0f;
};

class TypeList final : public juce::Component
{
public:
    TypeList (Context& c, int b) : ctx (c), band (b)
    {
        setTip (*this, "c:sat.type");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paint (juce::Graphics& g) override
    {
        static const char* types[] = { "Tube", "Tape", "Transformer", "Diode" };
        const int sel = juce::roundToInt (ctx.param (params::sat (band, "type")));
        const float rh = getHeight() / 4.0f;
        for (int i = 0; i < 4; ++i)
        {
            const juce::Rectangle<float> row (0.0f, i * rh + 1.0f, (float) getWidth(), rh - 2.0f);
            const bool on = i == sel;
            if (on)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRoundedRectangle (row, 4.0f);
            }
            const auto dot = juce::Rectangle<float> (row.getX() + 6, row.getCentreY() - 4.5f, 9, 9);
            g.setColour (on ? satColours[band - 1] : juce::Colour (0xff4a515d));
            if (on)
                g.fillEllipse (dot);
            else
                g.drawEllipse (dot, 1.5f);
            g.setColour (on ? juce::Colours::white : col::text3);
            g.setFont (font (12.5f, on));
            g.drawText (types[i], row.withTrimmedLeft (22), juce::Justification::centredLeft, true);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, 3, (int) (e.position.y / (getHeight() / 4.0f)));
        ctx.setParam (params::sat (band, "type"), (float) i);
        repaint();
    }
    Context& ctx;
    int band;
};

class SatCard final : public juce::Component
{
public:
    SatCard (Context& c, int b, std::function<Knob& (juce::Component&, const juce::String&, const juce::String&, const juce::String&, juce::Colour, bool)> mk)
        : ctx (c), band (b), curve (c, b), types (c, b)
    {
        addAndMakeVisible (curve);
        addAndMakeVisible (types);
        const auto colour = satColours[b - 1];
        drive = &mk (*this, params::sat (b, "drive"), "Drive", "sat.drive", colour, false);
        drive->setDialSize (44.0f);
        drive->getProperties().set ("w", 66);
        asym = &mk (*this, params::sat (b, "bias"), "Asymmetry", "sat.asym", colour, false);
        mix = &mk (*this, params::sat (b, "mix"), "Mix", "sat.mix", colour, false);
        out = &mk (*this, params::sat (b, "out"), "Output", "sat.out", colour, true);
    }
    void paint (juce::Graphics& g) override
    {
        const auto colour = satColours[band - 1];
        auto r = getLocalBounds().toFloat();
        g.setColour (col::panel2);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r.removeFromTop (3.0f).reduced (4.0f, 0.0f), 1.5f);
        if (horizontal)
            return;
        static const char* names[] = { "Low", "Mid", "High" };
        auto h = headArea;
        g.setFont (semibold (13.5f));
        const int nw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (semibold (13.5f), names[band - 1])) + 6;
        g.drawText (names[band - 1], h.removeFromLeft (nw), juce::Justification::centredLeft);
        const float x1 = ctx.param ("sat_xo1_freq"), x2 = ctx.param ("sat_xo2_freq");
        const auto range = band == 1 ? "< " + hzText (x1) : (band == 2 ? hzText (x1) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x93 ")) + hzText (x2) : "> " + hzText (x2));
        const auto dv = juce::String (ctx.param (params::sat (band, "drive")), 1) + " dB";
        const int dw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.5f, true), dv)) + 4;
        g.setColour (colour);
        g.setFont (font (12.5f, true));
        g.drawText (dv, h.removeFromRight (dw), juce::Justification::centredRight);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText ("Drive", h.removeFromRight (38), juce::Justification::centredRight);
        g.drawText (range, h, juce::Justification::centredLeft, true);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0).withTrimmedTop (5).withTrimmedBottom (3);
        if (horizontal)
        {
            r = r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), 80));
            curve.setBounds (r.removeFromLeft (70).withSizeKeepingCentre (70, 70));
            r.removeFromLeft (10);
            types.setBounds (r.removeFromLeft (118).withSizeKeepingCentre (118, 80));
            r.removeFromLeft (8);
            layoutKnobRow (r.withSizeKeepingCentre (r.getWidth(), 76), { drive, asym, mix, out }, 6);
            return;
        }
        headArea = r.removeFromTop (18);
        r.removeFromTop (4);
        auto body = r.removeFromTop (80);
        curve.setBounds (body.removeFromLeft (80));
        body.removeFromLeft (8);
        drive->setBounds (body.removeFromRight (66).withHeight (76));
        body.removeFromRight (4);
        types.setBounds (body.withSizeKeepingCentre (body.getWidth(), 80));
        r.removeFromTop (2);
        const int w = r.getWidth() / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto* k = i == 0 ? asym : (i == 1 ? mix : out);
            k->setBounds (r.getX() + i * w + (w - 62) / 2, r.getY(), 62, knobH);
        }
    }
    Context& ctx;
    int band;
    bool horizontal = false;
    TransferCurve curve;
    TypeList types;
    Knob *drive = nullptr, *asym = nullptr, *mix = nullptr, *out = nullptr;
    juce::Rectangle<int> headArea;
};

class SaturationEditor final : public ModuleEditor
{
public:
    explicit SaturationEditor (Context& c) : ModuleEditor (c, 5, "three bands, oversampled, drag a band up or down to set its drive"), graph (c)
    {
        note.setInterceptsMouseClicks (false, false);
        note.draw = [] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText (tr ("Oversampling follows Quality"), r, juce::Justification::centredRight, true);
        };
        addRight (note, 200);
        addChildComponent (bandSel);
        bandSel.onChange = [this] (int i)
        {
            compactBand = i;
            compactChanged();
            resized();
        };
        addAndMakeVisible (graph);
        const auto mk = [this] (juce::Component& parent, const juce::String& id, const juce::String& label, const juce::String& tip, juce::Colour colour, bool bi) -> Knob&
        { return makeKnob (parent, id, label, tip, colour, bi); };
        for (int b = 1; b <= 3; ++b)
        {
            cards.push_back (std::make_unique<SatCard> (ctx, b, mk));
            addAndMakeVisible (*cards.back());
        }
    }

    void compactChanged() override
    {
        right.clear();
        note.setVisible (! compact);
        bandSel.setVisible (compact);
        addRight (compact ? (juce::Component&) bandSel : (juce::Component&) note, compact ? 190 : 200);
        for (int b = 0; b < 3; ++b)
        {
            cards[(size_t) b]->horizontal = compact;
            cards[(size_t) b]->setVisible (! compact || b == compactBand);
        }
    }

    int naturalHeight (int) const override { return compact ? headH + 8 + 64 + 6 + 90 + 8 : headH + 8 + 78 + 8 + 180 + 8; }

    void update() override
    {
        ModuleEditor::update();
        graph.repaint();
        for (auto& c : cards)
        {
            c->curve.refresh();
            c->types.repaint();
            c->repaint();
        }
    }

    void layoutBody (juce::Rectangle<int> r) override
    {
        if (compact)
        {
            graph.setBounds (r.removeFromTop (64));
            r.removeFromTop (6);
            cards[(size_t) compactBand]->setBounds (r.removeFromTop (std::min (r.getHeight(), 90)));
            return;
        }
        graph.setBounds (r.removeFromTop (78));
        r.removeFromTop (8);
        const int gap = 8, w = (r.getWidth() - 2 * gap) / 3;
        for (int b = 0; b < 3; ++b)
            cards[(size_t) b]->setBounds (r.getX() + b * (w + gap), r.getY(), b == 2 ? r.getRight() - (r.getX() + 2 * (w + gap)) : w, std::min (r.getHeight(), 180));
    }

    SatGraph graph;
    Painter note;
    Segmented bandSel { { "Low", "Mid", "High" } };
    std::vector<std::unique_ptr<SatCard>> cards;
    int compactBand = 0;
};
} // namespace

// ==================================================================================================
std::unique_ptr<ModuleEditor> createModuleEditor (Context& ctx, int index)
{
    const auto& chain = ctx.proc.getChain();
    switch (index)
    {
        case 1: return std::make_unique<EqEditor> (ctx);
        case 2: return std::make_unique<MultibandEditor> (ctx);
        case 5: return std::make_unique<SaturationEditor> (ctx);
        default: break;
    }
    std::unique_ptr<GenericEditor> e;
    const auto magenta = col::magenta, orange = col::orange, blue = col::blue;
    switch (index)
    {
        case 0:
        {
            e = std::make_unique<GenericEditor> (ctx, 0, "trim, polarity and the subsonic filter", orange,
                                                 std::vector<KSpec> { { "in_trim", "Trim", "in.trim", true }, { "hpf_freq", "Subsonic", "in.sub" }, { "hpf_slope", "Slope", "in.slope" } });
            e->knob ("hpf_freq")->setColour (col::yellow);
            e->knob ("hpf_slope")->setColour (col::text2);
            e->toggle ("HPF + DC block", "hpf_on", "t:Subsonic HPF\nTurns the subsonic filter on and off. The 5 Hz DC blocker is always on.", 118);
            e->toggle (juce::String (juce::CharPointer_UTF8 ("\xc3\x98 R")), "in_pol_r", "c:in.polR", 44);
            e->toggle (juce::String (juce::CharPointer_UTF8 ("\xc3\x98 L")), "in_pol_l", "c:in.polL", 44);
            break;
        }
        case 3:
        {
            e = std::make_unique<GenericEditor> (ctx, 3, "mono maker, phase rotator, translator", blue,
                                                 std::vector<KSpec> { { "lem_mono_freq", "Mono", "le.mono" }, { "lem_mono_slope", "Slope", "" }, { "lem_mono_amount", "Amount", "le.amount" },
                                                                      { "lem_rot_freq", "Rot freq", "" }, { "lem_rot_angle", "Rotate", "le.rotate" },
                                                                      { "lem_tr_amount", "Translator", "le.trans" }, { "lem_tr_mix", "2nd/3rd", "" } });
            e->readout ([&chain] { const float tl = chain.getLowEnd().translatorLevelDb(); return tl > -90.0f ? "Tr " + fmtMinus (tl, 0) + " dB" : juce::String ("Tr -"); }, 80);
            e->toggle ("Translator", "lem_tr_on", "t:Translator\nAdds harmonics to notes 30-90 Hz so that small speakers play them.", 84);
            e->toggle ("Rotator", "lem_rot_on", "t:Phase rotator\nTurns the phase of the low end so that the waveform becomes more symmetric.", 70);
            e->toggle ("Mono", "lem_mono_on", "t:Mono maker\nMakes the low end mono below the Mono frequency.", 56);
            break;
        }
        case 4:
        {
            e = std::make_unique<GenericEditor> (ctx, 4, "glue compressor for the whole mix", orange,
                                                 std::vector<KSpec> { { "bus_thr", "Thresh", "bc.thr" }, { "bus_ratio", "Ratio", "bc.ratio" }, { "bus_att", "Attack", "bc.attack" },
                                                                      { "bus_rel", "Release", "bc.release" }, { "bus_sc_hpf", "SC HPF", "bc.schpf" }, { "bus_mix", "Mix", "bc.mix" },
                                                                      { "bus_detect", "Peak/RMS", "" }, { "bus_link", "Link", "" } });
            auto* ed = e.get();
            e->knob ("bus_thr")->meterLevel = [ed, &chain]
            {
                if (ed->ctx.param ("bus_on") < 0.5f)
                    return noSignal();
                const float v = chain.getBusComp().detectorLevelDb();
                return v <= -99.0f ? noSignal() : v;
            };
            auto* rel = e->knob ("bus_rel");
            rel->textOverride = [ed, rel] { return ed->ctx.param ("bus_rel_auto") >= 0.5f ? juce::String ("Auto") : ed->ctx.apvts.getParameter (rel->getParamId())->getCurrentValueAsText(); };
            rel->onUserChange = [ed] { ed->ctx.setParam ("bus_rel_auto", 0.0f); };
            e->toggle ("Auto makeup", "bus_makeup_auto", "t:Auto makeup\nRaises the output by the average gain reduction.", 98);
            e->toggle ("Auto release", "bus_rel_auto", "t:Auto release\nThe compressor sets the release time from the music.", 98);
            auto& knee = e->toggle ("Soft knee", nullptr, "t:Soft knee\nOn: soft knee, gentle start of compression. Off: hard knee.", 84);
            knee.setClickingTogglesState (false);
            knee.onClick = [ed] { ed->ctx.setParam ("bus_knee", ed->ctx.param ("bus_knee") >= 0.5f ? 0.0f : 1.0f); };
            auto* kneePtr = &knee;
            e->onUpdate = [ed, kneePtr] { kneePtr->setToggleState (ed->ctx.param ("bus_knee") < 0.5f, juce::dontSendNotification); };
            auto& model = e->own (std::make_unique<Segmented> (juce::StringArray { "VCA", "Punch" }));
            model.attach (ctx.apvts, "bus_model");
            setTip (model, "c:bc.model");
            e->addRight (model, 110);
            e->history ([&chain] { return chain.getBusComp().gainReductionDb(); }, 6.0f, "typical: 1-2 dB");
            break;
        }
        case 6:
        {
            e = std::make_unique<GenericEditor> (ctx, 6, "width per band and the correlation guard", orange,
                                                 std::vector<KSpec> { { "st_mid_width", "Mid width", "st.mid" }, { "st_high_width", "High width", "st.high" }, { "st_split_freq", "Split", "" },
                                                                      { "st_ms_balance", "M/S bal.", "", true }, { "st_guard_thr", "Guard thr", "st.guard", true } });
            auto* ed = e.get();
            auto* guard = e->knob ("st_guard_thr");
            guard->meterInverse = true;
            guard->meterLevel = [ed, &chain]
            {
                if (ed->ctx.param ("st_on") < 0.5f || ed->ctx.param ("st_guard_on") < 0.5f)
                    return noSignal();
                const float v = chain.getStereo().guardCorrelation();
                return v > 1.5f ? noSignal() : v;
            };
            e->toggle ("Correlation guard", "st_guard_on", "t:Correlation guard\nReduces the stereo width when the correlation falls below the threshold.", 136);
            break;
        }
        case 7:
        {
            e = std::make_unique<GenericEditor> (ctx, 7, "shaves drum peaks before the limiter", magenta,
                                                 std::vector<KSpec> { { "clip_thr", "Thresh", "cl.thr" }, { "clip_shape", "Shape", "" }, { "clip_mix", "Mix", "cl.mix" } });
            auto* ed = e.get();
            e->knob ("clip_thr")->meterLevel = [ed, &chain]
            {
                if (ed->ctx.param ("clip_on") < 0.5f)
                    return noSignal();
                const float v = chain.getClipper().inputPeakDb();
                return v <= -99.0f ? noSignal() : v;
            };
            e->readout ([&chain] { return "Peak " + fmtMinus (chain.getClipper().peakReductionDb()) + " dB"; }, 100);
            e->toggle ("Low protect", "clip_lowprot", "c:cl.lowprot", 92);
            e->dropdown ("clip_type", "c:cl.type", 100);
            e->history ([&chain] { return chain.getClipper().peakReductionDb(); }, 6.0f, "");
            break;
        }
        case 8:
        {
            e = std::make_unique<GenericEditor> (ctx, 8, "true-peak-safe look-ahead limiter", magenta,
                                                 std::vector<KSpec> { { "lim_drive", "Drive", "li.drive" }, { "lim_ceiling", "Ceiling", "li.ceil" }, { "lim_rel", "Release", "li.rel" },
                                                                      { "lim_lookahead", "Lookahead", "li.la" }, { "lim_link", "Link", "" }, { "lim_transient", "Transients", "" } });
            auto* ed = e.get();
            auto* rel = e->knob ("lim_rel");
            rel->textOverride = [ed, rel] { return ed->ctx.param ("lim_rel_auto") >= 0.5f ? juce::String ("Auto") : ed->ctx.apvts.getParameter (rel->getParamId())->getCurrentValueAsText(); };
            rel->onUserChange = [ed] { ed->ctx.setParam ("lim_rel_auto", 0.0f); };
            e->readout ([&chain] { return "GR " + fmtMinus (chain.getLimiter().gainReductionDb()) + " dB"; }, 90);
            e->toggle ("Auto release", "lim_rel_auto", "t:Auto release\nThe limiter sets the release time from the music.", 98);
            e->dropdown ("lim_mode", "c:li.mode", 116);
            e->history ([&chain] { return chain.getLimiter().gainReductionDb(); }, 12.0f, "");
            break;
        }
        default:
        {
            e = std::make_unique<GenericEditor> (ctx, 9, "final gain and dither", orange, std::vector<KSpec> { { "out_gain", "Gain", "out.gain", true } });
            auto* ed = e.get();
            e->readout ([ed] { return "TP " + fmtLufs (ed->ctx.out().truePeak); }, 80);
            e->dropdown ("quality", "t:Quality\nOversampling: Auto (2x in real time, 4x offline), Normal (2x), High (4x).", 96);
            e->dropdown ("out_bits", "t:Bit depth\nThe word length the dither is made for.", 84);
            e->dropdown ("out_dither", "c:out.dither", 130);
            break;
        }
    }
    return e;
}
} // namespace mm::ui
