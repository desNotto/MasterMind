#include "ui/ModuleEditors.h"
#include "ui/Locale.h"

#include "automatch/MatchEngine.h"
#include "dsp/ClipLimit.h"
#include "dsp/PhaseAuto.h"
#include "dsp/SatStereo.h"
#include "ui/EyesView.h"
#include "state/Parameters.h"

#include <algorithm>
#include <limits>
#include <thread>

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
                                                     { "st", "Stereo", "st_on" }, { "cl", "Inflator", "clip_on" }, { "li", "Limiter", "lim_on" },
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
        static const char* shortNames[] = { "Input", "Dyn EQ", "Multiband", "Low-End", "Bus Comp", "Saturation", "Stereo", "Inflator", "Limiter", "Output" };
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
            prefix = "GR";
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
            case 7:
            {
                const float e = ctx.param ("infl_effect");
                prefix = "Effect";
                value = juce::String (juce::roundToInt (e)) + "%";
                valueAccent = e > 0.5f;
                gr = juce::jlimit (0.0f, 1.0f, chain.getInflator().inflateDb() / 6.0f);
                break;
            }
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
        for (auto [id, label, tip] : { std::tuple { "rs_low", "Low", "eq.rslow" }, { "rs_high", "High", "eq.rshigh" }, { "rs_depth", "Depth", "eq.rsdepth" },
                                       { "rs_sharp", "Sharpness", "eq.rssharp" }, { "rs_speed", "Speed", "eq.rsspeed" }, { "rs_mix", "Mix", "eq.rsmix" } })
            resKnobs.push_back (&makeKnob (*this, id, label, tip, col::orange));
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
        // whole-number steps (mockup v12); Ratio keeps 0.5 steps so that 1.5:1 stays possible
        static const float stepOf[] = { 1.0f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
        for (int i = 0; i < 9; ++i)
        {
            auto* k = ks[(size_t) i];
            k->getProperties().set ("band", band - 1);
            k->valueStep = stepOf[i];
        }
        const auto whole = [] (const char* unit) { return [unit] (float v) { return fmtMinus (v, 0) + unit; }; };
        const auto signedWhole = [] (float v) { return (std::abs (v) < 0.5f ? juce::String ("0") : fmtSigned (v, 0)) + " dB"; };
        ks[0]->formatter = whole (" dB");
        ks[1]->formatter = [] (float v) { return (std::abs (v - std::round (v)) < 0.01f ? juce::String (juce::roundToInt (v)) : juce::String (v, 1)) + ":1"; };
        ks[2]->formatter = signedWhole;
        ks[3]->formatter = whole (" ms");
        ks[5]->formatter = whole (" dB");
        ks[6]->formatter = whole ("%");
        ks[7]->formatter = whole ("%");
        ks[8]->formatter = signedWhole;
        ks[0]->meterLevel = [this]
        {
            if (ctx.param ("mb_on") < 0.5f || ctx.param (params::mb (band, "bypass")) >= 0.5f)
                return noSignal();
            const float v = ctx.proc.getChain().getMultiband().bandLevelDb (band - 1);
            return v <= -99.0f ? noSignal() : v;
        };
        auto* rel = ks[4];
        rel->textOverride = [this] { return ctx.param (params::mb (band, "rel_auto")) >= 0.5f ? juce::String ("Auto") : fmtMinus (ctx.param (params::mb (band, "rel")), 0) + " ms"; };
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
            bool anySolo = false;
            for (int i = 1; i <= 3; ++i)
                anySolo = anySolo || ctx.param (params::sat (i, "solo")) >= 0.5f;
            const bool muted = ctx.param (params::sat (b + 1, "mute")) >= 0.5f, silent = muted || (anySolo && ctx.param (params::sat (b + 1, "solo")) < 0.5f);
            if (silent)
            {
                g.saveState();
                g.reduceClipRegion (juce::Rectangle<float> (x0, 0.0f, x1 - x0, pb).toNearestInt());
                g.setColour (juce::Colour (0xff2b313b));
                for (float k = x0 - pb; k < x1; k += 8.0f)
                    g.drawLine (k, pb, k + pb, 0.0f, 3.0f);
                g.restoreState();
                g.setColour (col::text2);
                g.setFont (font (11.0f, true));
                g.drawText (muted ? "MUTED" : "SOLO OFF", juce::Rectangle<float> (x1 - 76.0f, 2.0f, 72.0f, 14.0f), juce::Justification::centredRight);
            }
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
        for (auto [btn, field, tip] : { std::tuple { &solo, "solo", "c:sat.solo" }, { &mute, "mute", "c:sat.mute" } })
        {
            btn->attach (ctx.apvts, params::sat (b, field));
            setTip (*btn, tip);
            addAndMakeVisible (*btn);
        }
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
    /** The band is silent: muted, or another band is soloed (D-032). */
    bool silent() const
    {
        bool anySolo = false;
        for (int i = 1; i <= 3; ++i)
            anySolo = anySolo || ctx.param (params::sat (i, "solo")) >= 0.5f;
        return ctx.param (params::sat (band, "mute")) >= 0.5f || (anySolo && ctx.param (params::sat (band, "solo")) < 0.5f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0).withTrimmedTop (5).withTrimmedBottom (3);
        if (horizontal)
        {
            auto sm = r.removeFromRight (24);
            solo.setBounds (sm.withHeight (20).withY (r.getCentreY() - 22));
            mute.setBounds (sm.withHeight (20).withY (r.getCentreY() + 2));
            r.removeFromRight (6);
            r = r.withSizeKeepingCentre (r.getWidth(), std::min (r.getHeight(), 80));
            curve.setBounds (r.removeFromLeft (70).withSizeKeepingCentre (70, 70));
            r.removeFromLeft (10);
            types.setBounds (r.removeFromLeft (118).withSizeKeepingCentre (118, 80));
            r.removeFromLeft (8);
            layoutKnobRow (r.withSizeKeepingCentre (r.getWidth(), 76), { drive, asym, mix, out }, 6);
            return;
        }
        headArea = r.removeFromTop (18);
        {
            auto hb = headArea;
            mute.setBounds (hb.removeFromRight (22).withSizeKeepingCentre (22, 20));
            hb.removeFromRight (4);
            solo.setBounds (hb.removeFromRight (22).withSizeKeepingCentre (22, 20));
            headArea.removeFromRight (56);
        }
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
    Btn solo { "S", true }, mute { "M", true };
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
            c->setAlpha (c->silent() ? 0.55f : 1.0f);
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

// ---- Low-End (mockup v13) --------------------------------------------------------------------------------
const juce::Colour leBlue { 0xff4ea1ff }, leViolet { 0xffb78cff }, leCyan { 0xff7fd0ff };

/** Low spectrum 20-300 Hz: mid and side of the output, the mono zone, the side after the mono maker
    (estimated from the filter) and the handles for the mono and rotator frequencies. */
class LeGraph final : public juce::Component
{
public:
    explicit LeGraph (Context& c) : ctx (c) { setTip (*this, "c:le.graph"); }
    float xOf (double f) const { return (float) (std::log (f / 20.0) / std::log (15.0)) * getWidth(); }
    double fOf (float x) const { return 20.0 * std::pow (15.0, juce::jlimit (0.0, 1.0, (double) x / getWidth())); }
    float pb() const { return getHeight() - 14.0f; }
    float yOf (double db) const { return 8.0f + (float) ((topDb - db) / 48.0) * (pb() - 16.0f); }
    double topDb = -8.0; // follows the loudest point of the curves (6 dB steps)

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::sunk);
        g.fillRoundedRectangle (r, 6.0f);
        const float bottom = pb();
        g.setFont (font (10.5f));
        for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 150.0, 200.0, 300.0 })
        {
            const float x = xOf (f);
            g.setColour (juce::Colour (0xff1b1f26));
            g.drawVerticalLine ((int) x, 0.0f, bottom);
            g.setColour (col::text3);
            g.drawText (juce::String ((int) f), juce::Rectangle<float> (x + 2, bottom, 30, 14), juce::Justification::centredLeft);
        }
        const bool monoOn = ctx.param ("lem_mono_on") >= 0.5f;
        const float monoF = ctx.param ("lem_mono_freq"), rotF = ctx.param ("lem_rot_freq");
        const float amount = ctx.param ("lem_mono_amount") / 100.0f;
        const int order = 1 + juce::roundToInt (ctx.param ("lem_mono_slope")); // 12, 24, 48 dB/oct -> 1, 2, 3 (x2 poles)
        if (monoOn)
        {
            g.setColour (leBlue.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, xOf (monoF), bottom));
            g.setColour (juce::Colour (0xff7fb3ff));
            g.setFont (font (10.5f, true));
            g.drawText ("MONO < " + juce::String (juce::roundToInt (monoF)) + " Hz", juce::Rectangle<float> (6.0f, 3.0f, 120.0f, 14.0f), juce::Justification::centredLeft);
        }
        const auto& f = ctx.snap.lowFreqs;
        const auto& mid = ctx.snap.out.lowMid;
        const auto& side = ctx.snap.out.lowSide;
        {
            float peak = -200.0f;
            for (auto v : mid)
                peak = std::max (peak, v);
            for (auto v : side)
                peak = std::max (peak, v);
            if (peak > -150.0f)
                topDb = std::ceil ((peak + 3.0) / 6.0) * 6.0;
        }
        const auto curve = [&] (const std::vector<float>& v, auto&& adjust)
        {
            juce::Path p;
            for (size_t i = 0; i < v.size() && i < f.size(); ++i)
            {
                const float x = xOf (f[i]), y = juce::jlimit (0.0f, bottom, yOf (v[i] + adjust (f[i])));
                i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
            }
            return p;
        };
        if (! mid.empty() && ctx.snap.out.hasSignal)
        {
            g.setColour (col::orange);
            g.strokePath (curve (mid, [] (double) { return 0.0; }), juce::PathStrokeType (1.8f));
            g.setColour (col::magenta.withAlpha (0.85f));
            g.strokePath (curve (side, [] (double) { return 0.0; }), juce::PathStrokeType (1.4f));
            if (monoOn && ctx.param ("lem_on") >= 0.5f)
            {
                // side after the mono maker: low-pass share of the side removed by Amount
                juce::Path after = curve (side, [&] (double fr)
                {
                    const double lp = 1.0 / std::sqrt (1.0 + std::pow (fr / monoF, order == 3 ? 16.0 : 4.0 * order)); // 12, 24, 48 dB/oct
                    return 20.0 * std::log10 (std::max (1.0e-3, 1.0 - amount * lp));
                });
                juce::Path dashed;
                const float dash[] = { 4.0f, 3.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, after, dash, 2);
                g.setColour (col::magenta);
                g.fillPath (dashed);
            }
        }
        const auto pill = [&] (float x, float y, const juce::String& t, juce::Colour c, bool dark)
        {
            const float w = juce::GlyphArrangement::getStringWidth (font (10.5f, true), t) + 12.0f;
            const juce::Rectangle<float> rr (juce::jlimit (1.0f, (float) getWidth() - w - 1.0f, x - w / 2), y, w, 16.0f);
            g.setColour (c);
            g.fillRoundedRectangle (rr, 4.0f);
            g.setColour (dark ? juce::Colour (0xff06121f) : juce::Colours::white);
            g.setFont (font (10.5f, true));
            g.drawText (t, rr, juce::Justification::centred);
        };
        // rotator handle
        if (ctx.param ("lem_rot_on") >= 0.5f)
        {
            const float xr = xOf (rotF);
            const float dash[] = { 5.0f, 3.0f };
            g.setColour (leViolet);
            g.drawDashedLine (juce::Line<float> (xr, 0.0f, xr, bottom), dash, 2, 1.6f);
            pill (xr, 22.0f, "Rot " + juce::String (juce::roundToInt (rotF)) + " Hz", leViolet, false);
        }
        // mono handle
        {
            const float xm = xOf (monoF);
            g.setColour (monoOn ? leBlue : col::text3);
            g.fillRect (xm - 1.0f, 0.0f, 2.0f, bottom);
            static const char* slopes[] = { "12", "24", "48" };
            pill (xm, bottom - 18.0f, "Mono " + juce::String (juce::roundToInt (monoF)) + " Hz " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + slopes[juce::jlimit (0, 2, order - 1)]
                                         + " dB/oct " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + juce::String (juce::roundToInt (amount * 100.0f)) + "%",
                  monoOn ? leBlue : col::text3, true);
        }
        // legend
        auto lg = juce::Rectangle<float> ((float) getWidth() - 196.0f, 6.0f, 190.0f, 20.0f);
        g.setColour (col::sunk);
        g.fillRoundedRectangle (lg, 4.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (lg, 4.0f, 1.0f);
        g.setFont (font (10.5f));
        const auto item = [&] (float x, juce::Colour c, const char* t, bool dash)
        {
            g.setColour (c);
            if (dash)
            {
                const float d[] = { 4.0f, 3.0f };
                g.drawDashedLine (juce::Line<float> (lg.getX() + x, lg.getCentreY(), lg.getX() + x + 14.0f, lg.getCentreY()), d, 2, 1.2f);
            }
            else
                g.fillRect (juce::Rectangle<float> (lg.getX() + x, lg.getCentreY() - 1.5f, 14.0f, 3.0f));
            g.setColour (col::text2);
            g.drawText (t, juce::Rectangle<float> (lg.getX() + x + 18.0f, lg.getY(), 60.0f, lg.getHeight()), juce::Justification::centredLeft);
        };
        item (8.0f, col::orange, "Mid", false);
        item (56.0f, col::magenta, "Side", false);
        item (104.0f, col::magenta, "Side after", true);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    juce::String handleAt (float x) const
    {
        if (std::abs (x - xOf (ctx.param ("lem_mono_freq"))) < 7.0f)
            return "lem_mono_freq";
        if (ctx.param ("lem_rot_on") >= 0.5f && std::abs (x - xOf (ctx.param ("lem_rot_freq"))) < 7.0f)
            return "lem_rot_freq";
        return {};
    }
    void mouseMove (const juce::MouseEvent& e) override { setMouseCursor (handleAt (e.position.x).isNotEmpty() ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        drag = handleAt (e.position.x);
        if (drag.isNotEmpty())
            ctx.apvts.getParameter (drag)->beginChangeGesture();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (drag.isEmpty())
            return;
        auto* p = ctx.apvts.getParameter (drag);
        const auto range = p->getNormalisableRange();
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (range.start, range.end, (float) std::round (fOf (e.position.x)))));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (drag.isNotEmpty())
            ctx.apvts.getParameter (drag)->endChangeGesture();
        drag.clear();
    }
    Context& ctx;
    juce::String drag;
};

/** Phase dial (drag around it for the rotation) and the low-band waveform before (IN) and after (OUT). */
class LePhase final : public juce::Component
{
public:
    explicit LePhase (Context& c) : ctx (c)
    {
        setTip (*this, "c:le.phase");
        addAndMakeVisible (autoBtn);
        setTip (autoBtn, "c:le.auto");
        autoBtn.onClick = [this] { autoClicked(); };
    }
    void resized() override { autoBtn.setBounds (getWidth() - 52, 105, 46, 18); }

    /** Auto (mockup v18): two 2-second looks at the input low band, then the angle with the most
        symmetric peaks. A second click restores the previous rotator settings. */
    void autoClicked()
    {
        if (state == AutoState::applied)
        {
            ctx.setParam ("lem_rot_angle", prevAngle);
            ctx.setParam ("lem_rot_on", prevOn ? 1.0f : 0.0f);
            state = AutoState::idle;
            refreshButton();
            return;
        }
        if (state == AutoState::listening)
            return;
        segments.clear();
        if (! grab())
        {
            if (ctx.notify)
                ctx.notify (tr ("Play the loudest part of the song, then press Auto."));
            return;
        }
        state = AutoState::listening;
        listenSince = juce::Time::getMillisecondCounterHiRes();
        refreshButton();
    }
    bool grab()
    {
        const auto& w = ctx.snap.in.lowWave;
        float peak = 0.0f;
        for (auto v : w)
            peak = std::max (peak, std::abs (v));
        if (w.size() < 256 || peak < 1.0e-4f)
            return false;
        segments.push_back (w);
        return true;
    }
    void tick()
    {
        if (state == AutoState::listening && juce::Time::getMillisecondCounterHiRes() - listenSince >= 2000.0)
        {
            grab();
            const double fs = ctx.snap.sampleRate > 0.0 ? ctx.snap.sampleRate : 48000.0;
            const double lowRate = fs / std::max (16, (int) std::lround (fs / 1500.0)); // as the analysis
            const auto res = mm::dsp::phaseauto::search (segments, lowRate, ctx.param ("lem_rot_freq"), (int) (0.3 * lowRate));
            if (! res.valid)
            {
                state = AutoState::idle;
                if (ctx.notify)
                    ctx.notify (tr ("Play the loudest part of the song, then press Auto."));
            }
            else
            {
                prevAngle = ctx.param ("lem_rot_angle");
                prevOn = ctx.param ("lem_rot_on") >= 0.5f;
                ctx.setParam ("lem_on", 1.0f);
                ctx.setParam ("lem_rot_on", 1.0f);
                ctx.setParam ("lem_rot_angle", res.angle);
                appliedAngle = res.angle;
                state = AutoState::applied;
            }
            refreshButton();
        }
        // a manual change of the angle ends the Auto state
        if (state == AutoState::applied && std::abs (ctx.param ("lem_rot_angle") - appliedAngle) > 0.5f)
        {
            state = AutoState::idle;
            refreshButton();
        }
    }
    void refreshButton()
    {
        autoBtn.setButtonText (state == AutoState::applied ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 Auto"))
                                                           : (state == AutoState::listening ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) : juce::String ("Auto")));
        autoBtn.setToggleState (state == AutoState::applied, juce::dontSendNotification);
        repaint();
    }
    juce::Point<float> centre() const { return { getWidth() / 2.0f, 58.0f }; }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::sunk);
        g.fillRoundedRectangle (r, 6.0f);
        const auto c = centre();
        const float rad = 40.0f, angle = ctx.param ("lem_rot_angle");
        const bool on = ctx.param ("lem_rot_on") >= 0.5f;
        juce::Path back, arc;
        back.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        g.setColour (col::line2);
        g.strokePath (back, juce::PathStrokeType (6.0f));
        if (angle > 0.5f)
        {
            arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, 0.0f, juce::degreesToRadians (angle), true);
            g.setColour (on ? leViolet : col::text3);
            g.strokePath (arc, juce::PathStrokeType (6.0f));
        }
        const float a = juce::degreesToRadians (angle);
        g.setColour (col::text);
        g.drawLine (c.x, c.y, c.x + (rad - 10.0f) * std::sin (a), c.y - (rad - 10.0f) * std::cos (a), 2.5f);
        g.setFont (font (13.0f, true));
        g.drawText (juce::String (juce::roundToInt (angle)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), juce::Rectangle<float> (c.x - 30.0f, c.y + 6.0f, 60.0f, 16.0f), juce::Justification::centred);
        const auto wave = [&] (const std::vector<float>& w, float y, juce::Colour col2)
        {
            if (w.size() < 2)
                return;
            float peak = 1.0e-6f;
            for (auto v : w)
                peak = std::max (peak, std::abs (v));
            juce::Path p;
            const size_t n = std::min<size_t> (w.size(), 600);
            const size_t start = w.size() - n;
            for (size_t i = 0; i < n; ++i)
            {
                const float x = 8.0f + (float) i / (float) (n - 1) * (getWidth() - 16.0f);
                const float yy = y - w[start + i] / peak * 11.0f;
                i == 0 ? p.startNewSubPath (x, yy) : p.lineTo (x, yy);
            }
            g.setColour (col2);
            g.strokePath (p, juce::PathStrokeType (1.3f));
        };
        g.setFont (font (10.5f));
        g.setColour (col::text3);
        g.drawText ("before " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + "peaks " + fmtSigned (ctx.snap.in.asymmetry, 0) + "%", juce::Rectangle<float> (8.0f, 106.0f, r.getWidth() - 66.0f, 14.0f), juce::Justification::centredLeft);
        wave (ctx.snap.in.lowWave, 132.0f, col::text3);
        g.setColour (leViolet);
        g.drawText ("after " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7 ")) + "peaks " + fmtSigned (ctx.snap.out.asymmetry, 0) + "%", juce::Rectangle<float> (8.0f, 146.0f, r.getWidth() - 16.0f, 14.0f), juce::Justification::centredLeft);
        if (state == AutoState::applied)
            g.drawText ("Auto: " + juce::String (juce::roundToInt (appliedAngle)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), juce::Rectangle<float> (8.0f, 146.0f, r.getWidth() - 16.0f, 14.0f), juce::Justification::centredRight);
        wave (ctx.snap.out.lowWave, 170.0f, leViolet);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }
    float angleAt (juce::Point<float> p) const
    {
        const auto c = centre();
        float deg = juce::radiansToDegrees (std::atan2 (p.x - c.x, c.y - p.y));
        if (deg < 0.0f)
            deg += 360.0f;
        return std::round (deg);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = e.position.getDistanceFrom (centre()) < 56.0f;
        if (dragging)
        {
            ctx.apvts.getParameter ("lem_rot_angle")->beginChangeGesture();
            mouseDrag (e);
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;
        auto* p = ctx.apvts.getParameter ("lem_rot_angle");
        p->setValueNotifyingHost (p->convertTo0to1 (angleAt (e.position)));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            ctx.apvts.getParameter ("lem_rot_angle")->endChangeGesture();
        dragging = false;
    }
    Context& ctx;
    bool dragging = false;
    Btn autoBtn { "Auto" };
    enum class AutoState { idle, listening, applied } state = AutoState::idle;
    std::vector<std::vector<float>> segments;
    double listenSince = 0.0;
    float prevAngle = 0.0f, appliedAngle = 0.0f;
    bool prevOn = false;
};

/** Translator: the strongest low note and the harmonics the module adds (2nd and 3rd from
    Translator and 2nd/3rd), with the lower limit of a phone speaker. */
class LeTrans final : public juce::Component
{
public:
    explicit LeTrans (Context& c) : ctx (c) { setTip (*this, "c:le.transg"); }
    float xOf (double f) const { return 8.0f + (float) (std::log (f / 20.0) / std::log (20.0)) * (getWidth() - 16.0f); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::sunk);
        g.fillRoundedRectangle (r, 6.0f);
        const float base = getHeight() - 22.0f;
        g.setFont (font (10.5f));
        g.setColour (col::text3);
        for (double f : { 50.0, 100.0, 200.0 })
            g.drawText (juce::String ((int) f), juce::Rectangle<float> (xOf (f) - 15.0f, base + 4.0f, 30.0f, 14.0f), juce::Justification::centred);
        const float dash[] = { 3.0f, 3.0f };
        g.setColour (col::text2);
        g.drawDashedLine (juce::Line<float> (xOf (100.0), 8.0f, xOf (100.0), base), dash, 2, 1.0f);
        g.drawText ("phone speaker", juce::Rectangle<float> (xOf (100.0) + 4.0f, 6.0f, 100.0f, 14.0f), juce::Justification::centredLeft);
        // strongest note 30-90 Hz in the output mid spectrum
        double f0 = 41.2;
        const auto& fr = ctx.snap.lowFreqs;
        const auto& mid = ctx.snap.out.lowMid;
        float best = -200.0f;
        for (size_t i = 0; i < fr.size() && i < mid.size(); ++i)
            if (fr[i] >= 30.0 && fr[i] <= 90.0 && mid[i] > best)
            {
                best = mid[i];
                f0 = fr[i];
            }
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int midi = juce::roundToInt (12.0 * std::log2 (f0 / 440.0) + 69.0);
        const auto noteName = juce::String (names[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
        const bool on = ctx.param ("lem_tr_on") >= 0.5f;
        const float amount = on ? ctx.param ("lem_tr_amount") / 100.0f : 0.0f, mix = ctx.param ("lem_tr_mix") / 100.0f;
        const float full = base - 30.0f;
        const auto bar = [&] (double f, float h, juce::Colour c, const juce::String& t)
        {
            const float x = xOf (f);
            g.setColour (c);
            g.fillRoundedRectangle (juce::Rectangle<float> (x - 7.0f, base - h, 14.0f, h), 2.0f);
            g.setFont (font (10.5f));
            g.drawText (t, juce::Rectangle<float> (x - 20.0f, base - h - 15.0f, 40.0f, 13.0f), juce::Justification::centred);
        };
        bar (f0, full, col::text3, noteName);
        if (amount > 0.001f)
        {
            bar (2.0 * f0, std::max (3.0f, full * amount * 2.0f * (1.0f - mix)), leBlue, "2nd");
            bar (3.0 * f0, std::max (3.0f, full * amount * 2.0f * mix), leCyan, "3rd");
        }
        g.setColour (col::line2);
        g.drawHorizontalLine ((int) base, 8.0f, r.getWidth() - 8.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }
    Context& ctx;
};

class LowEndEditor final : public ModuleEditor
{
public:
    explicit LowEndEditor (Context& c) : ModuleEditor (c, 3, "mono maker, phase rotator, translator, drag the handles on the graph"), graph (c), phase (c), trans (c)
    {
        auto& readout = own (std::make_unique<Painter>());
        readout.setInterceptsMouseClicks (false, false);
        readout.draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            const float tl = ctx.proc.getChain().getLowEnd().translatorLevelDb();
            const auto v = tl > -90.0f ? fmtMinus (tl, 0) + " dB" : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93"));
            const int vw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.5f, true), v)) + 4;
            g.setColour (col::orange);
            g.setFont (font (12.5f, true));
            g.drawText (v, r.removeFromRight (vw), juce::Justification::centredRight);
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText ("Tr", r, juce::Justification::centredRight);
        };
        readoutPtr = &readout;
        addRight (readout, 70);
        for (auto [btn, id, tip] : { std::tuple { &trBtn, "lem_tr_on", "t:Translator\nAdds harmonics to notes 30-90 Hz so that small speakers play them." },
                                     { &rotBtn, "lem_rot_on", "t:Phase rotator\nTurns the phase of the low end so that the waveform becomes more symmetric." },
                                     { &monoBtn, "lem_mono_on", "t:Mono maker\nMakes the low end mono below the Mono frequency." } })
        {
            btn->attach (ctx.apvts, id);
            setTip (*btn, tip);
        }
        addRight (trBtn, 84);
        addRight (rotBtn, 70);
        addRight (monoBtn, 56);
        for (auto* comp : { (juce::Component*) &graph, (juce::Component*) &phase, (juce::Component*) &trans })
            addAndMakeVisible (*comp);
        monoKnobs = { &makeKnob (*this, "lem_mono_freq", "Mono", "le.mono", leBlue), &makeKnob (*this, "lem_mono_slope", "Slope", "le.slope", leBlue),
                      &makeKnob (*this, "lem_mono_amount", "Amount", "le.amount", leBlue) };
        rotKnobs = { &makeKnob (*this, "lem_rot_freq", "Rot freq", "le.rotfreq", leViolet), &makeKnob (*this, "lem_rot_angle", "Rotate", "le.rotate", leViolet) };
        trKnobs = { &makeKnob (*this, "lem_tr_amount", "Translator", "le.trans", leCyan), &makeKnob (*this, "lem_tr_mix", "2nd/3rd", "le.trmix", leCyan) };
        // turning Rotate or Translator above zero switches that part on (D-022)
        rotKnobs[1]->onUserChange = [this] { if (ctx.param ("lem_rot_angle") > 0.5f) ctx.setParam ("lem_rot_on", 1.0f); };
        trKnobs[0]->onUserChange = [this] { if (ctx.param ("lem_tr_amount") > 0.5f) ctx.setParam ("lem_tr_on", 1.0f); };
    }
    int naturalHeight (int) const override { return compact ? headH + 8 + 120 + 8 + knobH + 8 : headH + 8 + 176 + 8 + knobH + 8; }
    void update() override
    {
        ModuleEditor::update();
        graph.repaint();
        phase.tick();
        phase.repaint();
        trans.repaint();
        readoutPtr->repaint();
    }
    void layoutBody (juce::Rectangle<int> r) override
    {
        auto top = r.removeFromTop (compact ? 120 : 176);
        if (compact)
        {
            phase.setVisible (false);
            trans.setVisible (false);
        }
        else
        {
            trans.setBounds (top.removeFromRight (200));
            top.removeFromRight (10);
            phase.setBounds (top.removeFromRight (160));
            top.removeFromRight (10);
            phase.setVisible (true);
            trans.setVisible (true);
        }
        graph.setBounds (top);
        r.removeFromTop (8);
        auto row = r.removeFromTop (knobH);
        row.removeFromLeft (layoutKnobRow (row, monoKnobs) + 14);
        row.removeFromLeft (layoutKnobRow (row, rotKnobs) + 14);
        layoutKnobRow (row, trKnobs);
    }
    LeGraph graph;
    LePhase phase;
    LeTrans trans;
    Btn monoBtn { "Mono", true }, rotBtn { "Rotator", true }, trBtn { "Translator", true };
    Painter* readoutPtr = nullptr;
    std::vector<Knob*> monoKnobs, rotKnobs, trKnobs;
};

// ---- Clipper (mockup v15) --------------------------------------------------------------------------------
const juce::Colour clMag { 0xffe35bd8 }, clRed { 0xffff5468 };

void drawBox (juce::Graphics& g, juce::Rectangle<float> r)
{
    g.setColour (col::sunk);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
}

/** Inflator waveform (mockup v17): 40 ms around the loudest hit of the last 0.3 s, before (after
    Input) and after the curve and Effect. Refreshed 5 times per second so that it can be read. */
class IfWave final : public juce::Component
{
public:
    explicit IfWave (Context& c) : ctx (c)
    {
        setTip (*this, "c:if.wave");
        ringIn.assign ((size_t) ringSize, 0.0f);
        ringOut.assign ((size_t) ringSize, 0.0f);
        tmpIn.assign (16384, 0.0f);
        tmpOut.assign (16384, 0.0f);
    }
    void pull()
    {
        auto& infl = const_cast<dsp::Inflator&> (ctx.proc.getChain().getInflator());
        const int n = infl.readCapture (tmpIn.data(), tmpOut.data(), (int) tmpIn.size());
        for (int i = 0; i < n; ++i)
        {
            ringIn[(size_t) pos] = tmpIn[(size_t) i];
            ringOut[(size_t) pos] = tmpOut[(size_t) i];
            pos = (pos + 1) % ringSize;
        }
        if (++frames % 6 == 0)
            freeze();
    }
    void freeze()
    {
        const double fs = ctx.proc.getSampleRate() > 0 ? ctx.proc.getSampleRate() : 48000.0;
        const int lat = ctx.proc.getChain().getInflator().latency();
        const int look = std::min (ringSize - 4096, (int) (0.3 * fs)), win = std::min (2048, (int) (0.04 * fs));
        int best = -1;
        float bestV = 0.0f;
        for (int k = lat + win; k < look; ++k)
        {
            const int idx = ((pos - k) % ringSize + ringSize) % ringSize;
            if (std::abs (ringIn[(size_t) idx]) > bestV)
            {
                bestV = std::abs (ringIn[(size_t) idx]);
                best = k;
            }
        }
        viewIn.assign ((size_t) win, 0.0f);
        viewOut.assign ((size_t) win, 0.0f);
        if (best < 0)
            return;
        for (int i = 0; i < win; ++i)
        {
            const int k = best + win / 3 - i; // the peak sits at one third of the window
            const int iIn = ((pos - k) % ringSize + ringSize) % ringSize;
            const int iOut = ((pos - (k - lat)) % ringSize + ringSize) % ringSize;
            viewIn[(size_t) (win - 1 - i)] = ringIn[(size_t) iIn];
            viewOut[(size_t) (win - 1 - i)] = ringOut[(size_t) iOut];
        }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        drawBox (g, r);
        const float mid = r.getCentreY() - 8.0f;
        float peak = 1.05f;
        for (auto v : viewIn)
            peak = std::max (peak, std::abs (v));
        const float amp = (r.getHeight() / 2.0f - 22.0f) / peak;
        const auto X = [&] (size_t i) { return 8.0f + (float) i / (float) std::max<size_t> (1, viewIn.size() - 1) * (r.getWidth() - 16.0f); };
        const auto Y = [&] (float v) { return mid - v * amp; };
        g.setColour (juce::Colour (0xff232831));
        g.drawHorizontalLine ((int) mid, 0.0f, r.getWidth());
        const float dash[] = { 4.0f, 4.0f };
        g.setColour (juce::Colour (0xff5a2a32));
        g.drawDashedLine (juce::Line<float> (0.0f, Y (1.0f), r.getWidth(), Y (1.0f)), dash, 2, 1.0f);
        g.drawDashedLine (juce::Line<float> (0.0f, Y (-1.0f), r.getWidth(), Y (-1.0f)), dash, 2, 1.0f);
        g.setFont (font (10.5f));
        g.setColour (juce::Colour (0xffff8a95));
        g.drawText ("0 dBFS", juce::Rectangle<float> (r.getWidth() - 80.0f, Y (1.0f) - 14.0f, 72.0f, 12.0f), juce::Justification::centredRight);
        if (ctx.param ("clip_on") < 0.5f || viewIn.empty())
        {
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText (ctx.param ("clip_on") < 0.5f ? "Inflator is off" : "Play the song to see the waveform", r, juce::Justification::centred);
            return;
        }
        juce::Path pi, po;
        for (size_t i = 0; i < viewIn.size(); ++i)
        {
            i == 0 ? pi.startNewSubPath (X (i), Y (viewIn[i])) : pi.lineTo (X (i), Y (viewIn[i]));
            i == 0 ? po.startNewSubPath (X (i), Y (viewOut[i])) : po.lineTo (X (i), Y (viewOut[i]));
        }
        g.setColour (clMag);
        g.strokePath (po, juce::PathStrokeType (2.2f));
        g.setColour (juce::Colour (0xffa4acb9));
        g.strokePath (pi, juce::PathStrokeType (1.0f));
        // legend and the gain of quiet parts and peaks from the current curve
        const float legY = r.getBottom() - 15.0f;
        g.setFont (font (10.5f));
        g.setColour (juce::Colour (0xffa4acb9));
        g.fillRect (10.0f, legY + 5.0f, 14.0f, 3.0f);
        g.setColour (col::text2);
        g.drawText ("before", juce::Rectangle<float> (28.0f, legY, 50.0f, 13.0f), juce::Justification::centredLeft);
        g.setColour (clMag);
        g.fillRect (74.0f, legY + 5.0f, 14.0f, 3.0f);
        g.setColour (col::text2);
        g.drawText ("after Inflator", juce::Rectangle<float> (92.0f, legY, 100.0f, 13.0f), juce::Justification::centredLeft);
        const double cv = ctx.param ("infl_curve"), e = ctx.param ("infl_effect") / 100.0;
        const bool clipOn = ctx.param ("infl_clip") > 0.5f;
        const auto gainAt = [&] (double a) { return 20.0 * std::log10 (std::abs (dsp::Inflator::transfer (a, cv, e, clipOn)) / a); };
        g.setColour (juce::Colour (0xffffb877));
        g.drawText ("quiet parts " + fmtSigned (gainAt (0.25)) + " dB" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "peaks " + fmtSigned (gainAt (0.95)) + " dB",
                    juce::Rectangle<float> (r.getWidth() - 250.0f, legY, 240.0f, 13.0f), juce::Justification::centredRight);
    }
    Context& ctx;
    static constexpr int ringSize = 32768;
    std::vector<float> ringIn, ringOut, tmpIn, tmpOut, viewIn, viewOut;
    int pos = 0, frames = 0;
};

/** Inflator transfer curve (mockup v17): grey Curve -50 and +50, orange the current curve, magenta
    with the current Effect, the white dot at the current input peak. */
class IfCurve final : public juce::Component
{
public:
    explicit IfCurve (Context& c) : ctx (c) { setTip (*this, "c:if.graph"); }
    void paint (juce::Graphics& g) override
    {
        const auto box = getLocalBounds().toFloat();
        drawBox (g, box);
        const float side = std::min (box.getWidth(), box.getHeight()) - 12.0f;
        const auto r = box.withSizeKeepingCentre (side, side);
        const float pad = 16.0f, S = side - pad - 4.0f;
        const auto P = [&] (double x, double y) { return juce::Point<float> (r.getX() + pad + (float) x * S, r.getY() + 4.0f + (1.0f - (float) std::min (1.02, y)) * S); };
        g.setColour (juce::Colour (0xff1b1f26));
        for (double v : { 0.25, 0.5, 0.75 })
        {
            g.drawLine (juce::Line<float> (P (v, 0.0), P (v, 1.0)));
            g.drawLine (juce::Line<float> (P (0.0, v), P (1.0, v)));
        }
        const float dash[] = { 4.0f, 4.0f };
        g.setColour (juce::Colour (0xff5d6573));
        g.drawDashedLine (juce::Line<float> (P (0.0, 0.0), P (1.0, 1.0)), dash, 2, 1.0f);
        const double cv = ctx.param ("infl_curve"), e = ctx.param ("infl_effect") / 100.0;
        const bool clipOn = ctx.param ("infl_clip") > 0.5f;
        const auto curve = [&] (std::function<double (double)> f, juce::Colour c, float w)
        {
            juce::Path p;
            for (int k = 0; k <= 60; ++k)
            {
                const double x = k / 60.0;
                k == 0 ? p.startNewSubPath (P (x, f (x))) : p.lineTo (P (x, f (x)));
            }
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (w));
        };
        curve ([] (double x) { return dsp::Inflator::shape (x, -50.0); }, juce::Colour (0xff6f7786).withAlpha (0.55f), 1.0f);
        curve ([] (double x) { return dsp::Inflator::shape (x, 50.0); }, juce::Colour (0xff6f7786).withAlpha (0.55f), 1.0f);
        curve ([cv] (double x) { return dsp::Inflator::shape (x, cv); }, col::orange.withAlpha (0.8f), 1.2f);
        curve ([cv, e, clipOn] (double x) { return dsp::Inflator::transfer (x, cv, e, clipOn); }, clMag, 2.2f);
        const float pk = ctx.proc.getChain().getInflator().inputPeakDb();
        if (ctx.param ("clip_on") > 0.5f && pk > -60.0f)
        {
            const double lv = std::min (1.0, std::pow (10.0, pk / 20.0));
            const auto d = P (lv, dsp::Inflator::transfer (lv, cv, e, clipOn));
            g.setColour (juce::Colours::white);
            g.fillEllipse (d.x - 4.0f, d.y - 4.0f, 8.0f, 8.0f);
        }
        g.setFont (font (10.0f));
        g.setColour (col::text3);
        g.drawText ("out", juce::Rectangle<float> (r.getX() + 2.0f, r.getY() + 4.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText (juce::String ("in ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x92")), juce::Rectangle<float> (r.getX() + pad, r.getBottom() - 14.0f, 40.0f, 12.0f), juce::Justification::centredLeft);
        g.setColour (col::text2);
        g.drawText ("Curve " + juce::String (cv > 0.5 ? "+" : "") + juce::String (juce::roundToInt (cv)), juce::Rectangle<float> (r.getRight() - 104.0f, P (0.0, 0.2).y, 100.0f, 12.0f), juce::Justification::centredRight);
        g.setColour (clMag);
        g.drawText ("with Effect " + juce::String (juce::roundToInt (e * 100.0)) + "%", juce::Rectangle<float> (r.getRight() - 124.0f, P (0.0, 0.1).y, 120.0f, 12.0f), juce::Justification::centredRight);
    }
    Context& ctx;
};

/** Odd harmonics the Inflator adds to a sine at the current input peak, and the inflate meter. */
class IfHarm final : public juce::Component
{
public:
    explicit IfHarm (Context& c) : ctx (c) { setTip (*this, "c:if.harm"); }
    void refresh()
    {
        const double cv = ctx.param ("infl_curve"), e = ctx.param ("infl_effect") / 100.0;
        const bool clipOn = ctx.param ("infl_clip") > 0.5f;
        const float pk = ctx.proc.getChain().getInflator().inputPeakDb();
        const double a = pk > -60.0f ? std::min (1.0, std::pow (10.0, pk / 20.0)) : 0.8;
        constexpr int N = 512;
        const auto amp = [&] (int h)
        {
            double re = 0.0, im = 0.0;
            for (int n = 0; n < N; ++n)
            {
                const double ph = juce::MathConstants<double>::twoPi * n / N;
                const double y = dsp::Inflator::transfer (a * std::sin (ph), cv, e, clipOn);
                re += y * std::cos (h * ph);
                im += y * std::sin (h * ph);
            }
            return 2.0 * std::hypot (re, im) / N;
        };
        const double f1 = std::max (1.0e-9, amp (1));
        for (int k = 0; k < 4; ++k)
            harmDb[(size_t) k] = (float) (20.0 * std::log10 (std::max (1.0e-9, amp (3 + 2 * k)) / f1));
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        drawBox (g, r);
        g.setFont (font (11.0f));
        g.setColour (col::text3);
        g.drawText ("added harmonics, dB" + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "loudness", r.withHeight (20.0f).reduced (8.0f, 0.0f).translated (0.0f, 3.0f), juce::Justification::centredLeft, true);
        const float barH = r.getHeight() - 62.0f, top = 28.0f;
        const float colW = (r.getWidth() - 16.0f) / 5.0f;
        static const char* names[] = { "3rd", "5th", "7th", "9th" };
        for (int k = 0; k < 5; ++k)
        {
            const float cx = r.getX() + 8.0f + colW * (k + 0.5f);
            const auto bar = juce::Rectangle<float> (cx - 8.0f, top, 16.0f, barH);
            g.setColour (col::sunk.darker (0.3f));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (col::line);
            g.drawRoundedRectangle (bar, 3.0f, 1.0f);
            float frac;
            juce::Colour c;
            juce::String val, name;
            if (k < 4)
            {
                const float db = harmDb[(size_t) k];
                frac = juce::jlimit (0.02f, 1.0f, (db + 80.0f) / 70.0f);
                c = db > -30.0f ? col::warn : col::info;
                val = juce::String (juce::roundToInt (db));
                name = names[k];
            }
            else
            {
                const float inf = ctx.param ("clip_on") > 0.5f ? ctx.proc.getChain().getInflator().inflateDb() : 0.0f;
                frac = juce::jlimit (0.02f, 1.0f, inf / 6.0f);
                c = col::ok;
                val = fmtSigned (inf);
                name = "Inflate";
            }
            g.setColour (c);
            g.fillRoundedRectangle (bar.withTop (bar.getBottom() - bar.getHeight() * frac), 3.0f);
            g.setColour (k < 4 ? col::text2 : col::ok);
            g.setFont (font (11.5f, true));
            g.drawText (val, juce::Rectangle<float> (cx - 30.0f, bar.getBottom() + 3.0f, 60.0f, 14.0f), juce::Justification::centred);
            g.setColour (col::text3);
            g.setFont (font (11.0f));
            g.drawText (name, juce::Rectangle<float> (cx - 30.0f, bar.getBottom() + 17.0f, 60.0f, 13.0f), juce::Justification::centred);
            if (k == 3)
            {
                g.setColour (col::line);
                g.drawVerticalLine (juce::roundToInt (cx + colW * 0.5f), top, top + barH);
            }
        }
    }
    Context& ctx;
    std::array<float, 4> harmDb { -100.0f, -100.0f, -100.0f, -100.0f };
};

/** Modes with pictures (clip_type or lim_mode): click selects. */
class ModeList final : public juce::Component
{
public:
    ModeList (Context& c, juce::String paramId, juce::StringArray n, juce::StringArray d, juce::String tip, std::function<void (juce::Graphics&, juce::Rectangle<float>, int)> thumb)
        : ctx (c), id (std::move (paramId)), names (std::move (n)), descs (std::move (d)), drawThumb (std::move (thumb))
    {
        setTip (*this, tip);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        drawBox (g, r);
        const int sel = juce::roundToInt (ctx.param (id));
        const float rowH = (r.getHeight() - 12.0f - (footer.isNotEmpty() ? 16.0f : 0.0f)) / names.size();
        for (int i = 0; i < names.size(); ++i)
        {
            auto row = juce::Rectangle<float> (6.0f, 6.0f + i * rowH, r.getWidth() - 12.0f, rowH - 2.0f);
            if (i == sel)
            {
                g.setColour (clMag.withAlpha (0.10f));
                g.fillRoundedRectangle (row, 5.0f);
                g.setColour (juce::Colour (0xff7a3374));
                g.drawRoundedRectangle (row, 5.0f, 1.0f);
            }
            auto thumb = row.removeFromLeft (row.getHeight()).reduced (3.0f);
            g.setColour (col::sunk);
            g.fillRoundedRectangle (thumb, 3.0f);
            g.setColour (col::line);
            g.drawRoundedRectangle (thumb, 3.0f, 1.0f);
            drawThumb (g, thumb, i);
            row.removeFromLeft (8.0f);
            g.setColour (i == sel ? juce::Colours::white : col::text2);
            g.setFont (semibold (12.5f));
            g.drawText (names[i], row.removeFromTop (row.getHeight() / 2.0f), juce::Justification::bottomLeft, true);
            g.setColour (col::text3);
            g.setFont (font (11.0f));
            g.drawText (descs[i], row, juce::Justification::topLeft, true);
        }
        if (footer.isNotEmpty())
        {
            g.setColour (col::text3);
            g.setFont (font (11.0f));
            g.drawText (footer, r.withTop (r.getBottom() - 18.0f).reduced (8.0f, 0.0f), juce::Justification::centredLeft, true);
        }
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const float rowH = (getHeight() - 12.0f - (footer.isNotEmpty() ? 16.0f : 0.0f)) / names.size();
        const int i = (int) ((e.position.y - 6.0f) / rowH);
        if (i >= 0 && i < names.size())
            ctx.setParam (id, (float) i);
        repaint();
    }
    Context& ctx;
    juce::String id, footer;
    juce::StringArray names, descs;
    std::function<void (juce::Graphics&, juce::Rectangle<float>, int)> drawThumb;
};

class InflatorEditor final : public ModuleEditor
{
public:
    explicit InflatorEditor (Context& c)
        : ModuleEditor (c, 7, "raises the quiet parts of the wave more than the peaks, denser and louder before the limiter"), wave (c), curve (c), harm (c)
    {
        auto& meter = own (std::make_unique<Painter>());
        meter.setInterceptsMouseClicks (false, false);
        meter.draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            const float inf = ctx.param ("clip_on") > 0.5f ? ctx.proc.getChain().getInflator().inflateDb() : 0.0f;
            const auto v = fmtSigned (inf) + " dB";
            const int vw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.5f, true), v)) + 4;
            g.setColour (col::ok);
            g.setFont (font (12.5f, true));
            g.drawText (v, r.removeFromRight (vw), juce::Justification::centredRight);
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText ("Inflate", r, juce::Justification::centredRight);
        };
        readout = &meter;
        addRight (meter, 110);
        split.attach (ctx.apvts, "infl_split");
        setTip (split, "c:if.split");
        addRight (split, 92);
        clip.attach (ctx.apvts, "infl_clip");
        setTip (clip, "c:if.clip");
        addRight (clip, 84);
        for (auto* comp : { (juce::Component*) &wave, (juce::Component*) &curve, (juce::Component*) &harm })
            addAndMakeVisible (*comp);
        auto& curveKnob = makeKnob (*this, "infl_curve", "Curve", "if.curve", clMag, true);
        curveKnob.formatter = [] (float v) { const int c = juce::roundToInt (v); return (c > 0 ? "+" : "") + juce::String (c); };
        curveKnob.valueStep = 1.0f;
        row = { &makeKnob (*this, "infl_input", "Input", "if.input", clMag), &makeKnob (*this, "infl_effect", "Effect", "if.effect", clMag),
                &curveKnob, &makeKnob (*this, "infl_output", "Output", "if.output", clMag) };
    }
    int naturalHeight (int) const override { return compact ? headH + 8 + 120 + 8 + knobH + 8 : headH + 8 + 176 + 8 + knobH + 8; }
    void update() override
    {
        ModuleEditor::update();
        wave.pull();
        curve.repaint();
        if (++frames % 6 == 0)
            harm.refresh();
        else
            harm.repaint();
        readout->repaint();
        repaint (hint);
    }
    void layoutBody (juce::Rectangle<int> r) override
    {
        auto top = r.removeFromTop (compact ? 120 : 176);
        curve.setVisible (! compact);
        harm.setVisible (! compact);
        if (! compact)
        {
            harm.setBounds (top.removeFromRight (230));
            top.removeFromRight (10);
            curve.setBounds (top.removeFromRight (176));
            top.removeFromRight (10);
        }
        wave.setBounds (top);
        r.removeFromTop (8);
        auto kr = r.removeFromTop (knobH);
        hint = kr.withTrimmedLeft (layoutKnobRow (kr, row) + 14).withWidth (440);
    }
    void paint (juce::Graphics& g) override
    {
        ModuleEditor::paint (g);
        if (compact || hint.isEmpty() || ctx.param ("infl_split") < 0.5f)
            return;
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawFittedText (juce::String ("Band split: below 240 Hz") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "240 Hz to 2.4 kHz"
                              + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "above 2.4 kHz, each band through the curve",
                          hint, juce::Justification::centredLeft, 2);
    }
    IfWave wave;
    IfCurve curve;
    IfHarm harm;
    Btn clip { "Clip 0 dB", true }, split { "Band split", true };
    Painter* readout = nullptr;
    std::vector<Knob*> row;
    juce::Rectangle<int> hint;
    int frames = 0;
};

// ---- Limiter (mockup v15) --------------------------------------------------------------------------------
/** Level over the last 6 s: input after Drive, output, the ceiling and the gain reduction strip on top. */
class LiTime final : public juce::Component
{
public:
    explicit LiTime (Context& c) : ctx (c)
    {
        setTip (*this, "c:li.time");
        in.fill (-100.0f);
        out.fill (-100.0f);
        gr.fill (0.0f);
    }
    void push()
    {
        const auto& lim = ctx.proc.getChain().getLimiter();
        const bool on = ctx.param ("lim_on") >= 0.5f;
        in[(size_t) pos] = on ? lim.inputPeakDb() : -100.0f;
        out[(size_t) pos] = on ? lim.outputPeakDb() : -100.0f;
        gr[(size_t) pos] = on ? -lim.gainReductionDb() : 0.0f;
        pos = (pos + 1) % n;
        repaint();
    }
    float maxGr() const { return *std::max_element (gr.begin(), gr.end()); }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        drawBox (g, r);
        const float x0 = 30.0f, x1 = r.getWidth() - 40.0f, grTop = 8.0f, grH = 40.0f, top = 56.0f, bottom = r.getHeight() - 16.0f;
        const auto Y = [&] (float db) { return top + (1.0f - juce::jlimit (-15.0f, 1.0f, db)) / 16.0f * (bottom - top); };
        const auto X = [&] (int i) { return x0 + (float) i / (n - 1) * (x1 - x0); };
        g.setColour (juce::Colour (0xff11141a));
        g.fillRect (juce::Rectangle<float> (x0, grTop, x1 - x0, grH));
        g.setFont (font (10.0f));
        for (int d : { 0, -3, -6, -9, -12, -15 })
        {
            g.setColour (juce::Colour (0xff1b1f26));
            g.drawHorizontalLine ((int) Y ((float) d), x0, x1);
            g.setColour (col::text3);
            g.drawText (d == 0 ? juce::String ("0") : minusSign() + juce::String (-d), juce::Rectangle<float> (x1 + 4.0f, Y ((float) d) - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
        }
        g.drawText ("0", juce::Rectangle<float> (x1 + 4.0f, grTop - 2.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText (minusSign() + "9", juce::Rectangle<float> (x1 + 4.0f, grTop + grH - 10.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
        juce::Path grFill, grLine, pIn, pOut;
        grFill.startNewSubPath (x0, grTop);
        for (int i = 0; i < n; ++i)
        {
            const int k = (pos + i) % n;
            const float yg = grTop + juce::jlimit (0.0f, 1.0f, gr[(size_t) k] / 9.0f) * grH;
            grFill.lineTo (X (i), yg);
            i == 0 ? grLine.startNewSubPath (X (i), yg) : grLine.lineTo (X (i), yg);
            // no line where there is no signal yet (history not filled, limiter off)
            if (in[(size_t) k] > -90.0f)
            {
                const bool fresh = i == 0 || in[(size_t) ((k + n - 1) % n)] <= -90.0f;
                fresh ? pIn.startNewSubPath (X (i), Y (in[(size_t) k])) : pIn.lineTo (X (i), Y (in[(size_t) k]));
                fresh ? pOut.startNewSubPath (X (i), Y (out[(size_t) k])) : pOut.lineTo (X (i), Y (out[(size_t) k]));
            }
        }
        grFill.lineTo (x1, grTop);
        grFill.closeSubPath();
        g.setColour (col::orange.withAlpha (0.35f));
        g.fillPath (grFill);
        g.setColour (col::orange);
        g.strokePath (grLine, juce::PathStrokeType (1.4f));
        g.setColour (juce::Colour (0xffffb877));
        g.drawText ("gain reduction, dB", juce::Rectangle<float> (x0 + 4.0f, grTop + grH - 14.0f, 140.0f, 12.0f), juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff8b93a1).withAlpha (0.85f));
        g.strokePath (pIn, juce::PathStrokeType (1.1f));
        g.setColour (clMag);
        g.strokePath (pOut, juce::PathStrokeType (1.8f));
        const float ceil = ctx.param ("lim_ceiling");
        const float dash[] = { 5.0f, 4.0f };
        g.setColour (clRed);
        g.drawDashedLine (juce::Line<float> (x0, Y (ceil), x1, Y (ceil)), dash, 2, 1.0f);
        g.setColour (juce::Colour (0xffff8a95));
        g.setFont (font (10.5f));
        g.drawText ("Ceiling " + fmtMinus (ceil) + " dBTP", juce::Rectangle<float> (x1 - 140.0f, Y (ceil) - 15.0f, 136.0f, 13.0f), juce::Justification::centredRight);
        g.setColour (col::text3);
        g.drawText (minusSign() + "6 s", juce::Rectangle<float> (x0, r.getBottom() - 15.0f, 40.0f, 13.0f), juce::Justification::centredLeft);
        g.drawText ("now", juce::Rectangle<float> (x1 - 40.0f, r.getBottom() - 15.0f, 40.0f, 13.0f), juce::Justification::centredRight);
        if (ctx.param ("lim_on") < 0.5f)
        {
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText ("Limiter is off", r, juce::Justification::centred);
        }
    }
    Context& ctx;
    static constexpr int n = 180; // 6 s at 30 Hz
    std::array<float, n> in {}, out {}, gr {};
    int pos = 0;
};

/** Band panel (mockup v16): reduction per band hangs down from 0 dB, attack emphasis rises above it.
    Values are smoothed for reading. Classic: one bar for the whole range. */
class LiBands final : public juce::Component
{
public:
    explicit LiBands (Context& c) : ctx (c) { setTip (*this, "c:li.bview"); }
    void refresh()
    {
        const auto* bank = ctx.proc.getChain().getLimiter().spectralBank();
        const bool on = ctx.param ("lim_on") >= 0.5f;
        bands = bank != nullptr ? bank->meterBands.load (std::memory_order_relaxed) : 0;
        for (int b = 0; b < dsp::SpectralBank::maxBands; ++b)
        {
            const float gr = bank != nullptr && on && b < bands ? -bank->bandGrDb[(size_t) b].get() : 0.0f;
            const float em = bank != nullptr && on && b < bands ? bank->bandEmphDb[(size_t) b].get() : 0.0f;
            shownGr[(size_t) b] = gr > shownGr[(size_t) b] ? gr : shownGr[(size_t) b] + 0.25f * (gr - shownGr[(size_t) b]);
            shownEm[(size_t) b] = em > shownEm[(size_t) b] ? em : shownEm[(size_t) b] + 0.25f * (em - shownEm[(size_t) b]);
        }
        const float wide = on ? -ctx.proc.getChain().getLimiter().gainReductionDb() : 0.0f;
        shownWide = wide > shownWide ? wide : shownWide + 0.25f * (wide - shownWide);
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        drawBox (g, r);
        const float x0 = 26.0f, x1 = r.getWidth() - 30.0f, zero = 46.0f, grBot = r.getHeight() - 30.0f, teTop = 10.0f;
        const float grMax = 9.0f, teMax = 6.0f;
        g.setFont (font (10.0f));
        for (int d : { 0, -3, -6, -9 })
        {
            const float y = zero + (-d / grMax) * (grBot - zero);
            g.setColour (d == 0 ? juce::Colour (0xff3a414d) : juce::Colour (0xff1b1f26));
            g.drawHorizontalLine (juce::roundToInt (y), x0, x1);
            g.setColour (col::text3);
            g.drawText (d == 0 ? juce::String ("0") : minusSign() + juce::String (-d), juce::Rectangle<float> (x1 + 4.0f, y - 6.0f, 24.0f, 12.0f), juce::Justification::centredLeft);
        }
        g.drawText ("+6", juce::Rectangle<float> (x1 + 4.0f, teTop, 24.0f, 12.0f), juce::Justification::centredLeft);
        const auto Ygr = [&] (float v) { return zero + std::min (v, grMax) / grMax * (grBot - zero); };
        if (bands == 0)
        {
            g.setColour (col::orange.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (x0 + 2.0f, zero, x1 - x0 - 4.0f, Ygr (shownWide) - zero));
            g.setColour (col::text2);
            g.setFont (font (11.0f));
            g.drawText ("Classic: one band for the whole range", juce::Rectangle<float> (x0, grBot - 30.0f, x1 - x0, 14.0f), juce::Justification::centred);
            g.setColour (col::text3);
            g.setFont (font (10.5f));
            g.drawText ("choose 8, 16 or 26 bands for band limiting", juce::Rectangle<float> (x0, grBot - 16.0f, x1 - x0, 14.0f), juce::Justification::centred);
        }
        else
        {
            const auto fc = dsp::SpectralBank::centres (bands == 8 ? 0 : (bands == 16 ? 1 : 2));
            const float bw = (x1 - x0) / (float) bands;
            for (int b = 0; b < bands; ++b)
            {
                const float x = x0 + b * bw + 1.0f, w = std::max (2.0f, bw - 2.0f);
                const float gr = shownGr[(size_t) b] + shownWide, te = std::min (teMax, shownEm[(size_t) b]);
                g.setColour (col::orange.withAlpha (fc[(size_t) b] < 120.0 ? 0.85f : 0.6f));
                g.fillRect (juce::Rectangle<float> (x, zero, w, Ygr (gr) - zero));
                const float yt = zero - te / teMax * (zero - teTop);
                g.setColour (col::ok.withAlpha (0.7f));
                g.fillRect (juce::Rectangle<float> (x, yt, w, zero - yt));
            }
            g.setColour (col::text3);
            g.setFont (font (10.0f));
            for (auto [label, f] : { std::pair { "50", 50.0 }, { "200", 200.0 }, { "1k", 1050.0 }, { "5k", 5000.0 }, { "15k", 15000.0 } })
            {
                int best = 0;
                for (int b = 0; b < bands; ++b)
                    if (std::abs (std::log (fc[(size_t) b] / f)) < std::abs (std::log (fc[(size_t) best] / f)))
                        best = b;
                const float x = x0 + (best + 0.5f) * bw;
                g.drawText (label, juce::Rectangle<float> (x - 16.0f, grBot + 4.0f, 32.0f, 12.0f), juce::Justification::centred);
            }
        }
        // legend
        const float ly = r.getHeight() - 12.0f;
        g.setColour (col::orange.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x0, ly, 10.0f, 7.0f));
        g.setColour (col::text2);
        g.setFont (font (10.0f));
        g.drawText ("GR per band", juce::Rectangle<float> (x0 + 14.0f, ly - 3.0f, 80.0f, 13.0f), juce::Justification::centredLeft);
        g.setColour (col::ok.withAlpha (0.7f));
        g.fillRect (juce::Rectangle<float> (x0 + 96.0f, ly, 10.0f, 7.0f));
        g.setColour (col::text2);
        g.drawText ("attack emphasis", juce::Rectangle<float> (x0 + 110.0f, ly - 3.0f, 100.0f, 13.0f), juce::Justification::centredLeft);
    }
    Context& ctx;
    int bands = 0;
    std::array<float, dsp::SpectralBank::maxBands> shownGr {}, shownEm {};
    float shownWide = 0.0f;
};

class LimiterEditor final : public ModuleEditor
{
public:
    explicit LimiterEditor (Context& c)
        : ModuleEditor (c, 8, "spectral limiter, bands like the ear hears, each band is limited on its own"), time (c), bandView (c),
          modes (c, "lim_mode", { "Transparent", "Balanced", "Aggressive" }, { "slow, clean, quieter", "for most mixes", "fast, loud, dense" }, "c:li.mode",
                 [] (juce::Graphics& g, juce::Rectangle<float> r, int i)
                 {
                     const float att = std::array<float, 3> { 0.5f, 0.35f, 0.2f }[(size_t) i], rel = std::array<float, 3> { 0.35f, 0.2f, 0.08f }[(size_t) i];
                     juce::Path p;
                     for (int k = 0; k <= 40; ++k)
                     {
                         const float t = k / 40.0f;
                         float v = 0.0f;
                         if (t > 0.2f && t < 0.2f + att * 0.3f)
                             v = (t - 0.2f) / (att * 0.3f);
                         else if (t >= 0.2f + att * 0.3f)
                             v = std::exp (-(t - 0.2f - att * 0.3f) / rel);
                         const juce::Point<float> pt (r.getX() + 2.0f + t * (r.getWidth() - 4.0f), r.getY() + 3.0f + v * (r.getHeight() - 8.0f));
                         k == 0 ? p.startNewSubPath (pt) : p.lineTo (pt);
                     }
                     g.setColour (col::orange);
                     g.strokePath (p, juce::PathStrokeType (1.6f));
                 })
    {
        modes.footer = " ";
        auto& gr = own (std::make_unique<Painter>());
        gr.setInterceptsMouseClicks (false, false);
        gr.draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            const auto v = fmtMinus (ctx.proc.getChain().getLimiter().gainReductionDb()) + " dB";
            const int vw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.5f, true), v)) + 4;
            g.setColour (col::orange);
            g.setFont (font (12.5f, true));
            g.drawText (v, r.removeFromRight (vw), juce::Justification::centredRight);
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText ("GR", r, juce::Justification::centredRight);
        };
        readout = &gr;
        addRight (gr, 90);
        relAuto.attach (ctx.apvts, "lim_rel_auto");
        setTip (relAuto, "t:Auto release\nThe limiter sets the release time from the music.");
        addRight (relAuto, 98);
        bandsBox.attach (ctx.apvts, "lim_bands");
        setTip (bandsBox, "c:li.bands");
        addRight (bandsBox, 92);
        auto& bandsLabel = own (std::make_unique<Painter>());
        bandsLabel.setInterceptsMouseClicks (false, false);
        bandsLabel.draw = [] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (col::text3);
            g.setFont (font (12.5f));
            g.drawText ("Bands", r, juce::Justification::centredRight);
        };
        addRight (bandsLabel, 44);
        for (auto* comp : { (juce::Component*) &time, (juce::Component*) &bandView, (juce::Component*) &modes })
            addAndMakeVisible (*comp);
        auto& rel = makeKnob (*this, "lim_rel", "Release", "li.rel", clMag);
        rel.textOverride = [this, &rel] { return ctx.param ("lim_rel_auto") >= 0.5f ? juce::String ("Auto") : ctx.apvts.getParameter (rel.getParamId())->getCurrentValueAsText(); };
        rel.onUserChange = [this] { ctx.setParam ("lim_rel_auto", 0.0f); };
        row = { &makeKnob (*this, "lim_drive", "Drive", "li.drive", clMag), &makeKnob (*this, "lim_ceiling", "Ceiling", "li.ceil", clMag), &rel,
                &makeKnob (*this, "lim_lookahead", "Lookahead", "li.la", clMag), &makeKnob (*this, "lim_link", "Link", "li.link", clMag) };
        // the loudness stages (D-042): spectral clipper, slow first stage, density
        for (auto [id, label, tip] : { std::tuple { "lim_clip", "Clip", "li.clip" }, { "lim_out_clip", "Out Clip", "li.oclip" }, { "lim_leveler", "Leveler", "li.lev" },
                                       { "lim_density", "Density", "li.dens" } })
        {
            auto& k = makeKnob (*this, id, label, tip, clMag);
            if (juce::String (id) == "lim_leveler" || juce::String (id) == "lim_density")
                k.valueStep = 1.0f;
            if (juce::String (id) == "lim_out_clip")
                k.textOverride = [this, &k]
                {
                    return ctx.param ("lim_bands") < 0.5f ? juce::String ("off") : ctx.apvts.getParameter (k.getParamId())->getCurrentValueAsText();
                };
            loudRow.push_back (&k);
        }
        // the band knobs (D-037, Soften D-042); in Classic they show "off"
        for (auto [id, label, tip] : { std::tuple { "lim_adapt_gain", "Adaptive Gain", "li.again" }, { "lim_adapt_speed", "Adaptive Speed", "li.aspeed" },
                                       { "lim_transient", "Transients", "li.trans" }, { "lim_soften", "Soften", "li.soft" }, { "lim_adapt_trans", "Adaptive Trans", "li.atrans" } })
        {
            auto& k = makeKnob (*this, id, label, tip, clMag);
            if (juce::String (id) != "lim_transient")
                k.textOverride = [this, &k]
                {
                    return ctx.param ("lim_bands") < 0.5f ? juce::String ("off") : ctx.apvts.getParameter (k.getParamId())->getCurrentValueAsText();
                };
            if (juce::String (id) == "lim_adapt_speed" || juce::String (id) == "lim_adapt_trans" || juce::String (id) == "lim_soften")
                k.valueStep = 1.0f;
            spectralRow.push_back (&k);
        }
    }
    // two knob rows (D-042): core and loudness stages, then the band knobs
    int naturalHeight (int) const override { return compact ? headH + 8 + 120 + 8 + 2 * knobH + 14 : headH + 8 + 176 + 8 + 2 * knobH + 14; }
    void update() override
    {
        ModuleEditor::update();
        time.push();
        bandView.refresh();
        const bool classic = ctx.param ("lim_bands") < 0.5f;
        for (size_t i = 0; i < spectralRow.size(); ++i)
            if (i != 2)
                spectralRow[i]->setAlpha (classic ? 0.45f : 1.0f);
        if (loudRow.size() > 1)
            loudRow[1]->setAlpha (classic ? 0.45f : 1.0f); // Out Clip works with the bands only
        const auto& o = ctx.out();
        modes.footer = "max " + fmtMinus (-time.maxGr()) + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "TP " + fmtLufs (o.truePeak)
                     + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + "overs " + juce::String ((juce::int64) o.truePeakOvers);
        modes.repaint();
        readout->repaint();
    }
    void layoutBody (juce::Rectangle<int> r) override
    {
        auto top = r.removeFromTop (compact ? 120 : 176);
        bandView.setVisible (! compact);
        modes.setVisible (! compact);
        if (! compact)
        {
            modes.setBounds (top.removeFromRight (206));
            top.removeFromRight (10);
            bandView.setBounds (top.removeFromRight (300));
            top.removeFromRight (10);
        }
        time.setBounds (top);
        r.removeFromTop (8);
        auto kr = r.removeFromTop (knobH);
        const int w = layoutKnobRow (kr, row);
        separatorX = kr.getX() + w + 12;
        layoutKnobRow (kr.withTrimmedLeft (w + 25), loudRow);
        r.removeFromTop (6);
        layoutKnobRow (r.removeFromTop (knobH), spectralRow);
    }
    void paint (juce::Graphics& g) override
    {
        ModuleEditor::paint (g);
        if (separatorX > 0 && ! row.empty())
        {
            g.setColour (col::line);
            g.drawVerticalLine (separatorX, (float) row.front()->getY() + 4.0f, (float) row.front()->getBottom() - 4.0f);
            if (! spectralRow.empty())
                g.drawHorizontalLine (spectralRow.front()->getY() - 3, (float) row.front()->getX(), (float) separatorX + 200.0f);
        }
    }
    LiTime time;
    LiBands bandView;
    ModeList modes;
    Dropdown bandsBox;
    std::vector<Knob*> spectralRow, loudRow;
    int separatorX = 0;
    Btn relAuto { "Auto release", true };
    Painter* readout = nullptr;
    std::vector<Knob*> row;
};

// ---- Output with the loudness target (mockup v15) --------------------------------------------------------
class OutputEditor final : public ModuleEditor, private juce::Timer
{
public:
    explicit OutputEditor (Context& c) : ModuleEditor (c, 9, "final gain, dither and the loudness target"), target (c, "out_target", false)
    {
        dither.attach (ctx.apvts, "out_dither");
        setTip (dither, "c:out.dither");
        bits.attach (ctx.apvts, "out_bits");
        setTip (bits, "t:Bit depth\nThe word length the dither is made for.");
        quality.attach (ctx.apvts, "quality");
        setTip (quality, "t:Quality\nOversampling: Auto (2x in real time, 4x offline), Normal (2x), High (4x).");
        addRight (quality, 124);
        addRight (bits, 84);
        addRight (dither, 122);
        box = &own (std::make_unique<Painter>());
        box->setInterceptsMouseClicks (false, false);
        box->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r) { paintBox (g, r); };
        addAndMakeVisible (target);
        setTip (target, "c:out.target");
        presets.onChange = [this] (int i)
        {
            static const float values[] = { -14.0f, -11.0f, -9.0f, -8.0f, -6.0f, -5.0f };
            ctx.setParam ("out_target", values[juce::jlimit (0, 5, i)]);
        };
        setTip (presets, "c:out.presets");
        addAndMakeVisible (presets);
        for (auto* b : { &reach, &stopBtn, &undoBtn })
            addChildComponent (*b);
        reach.setVisible (true);
        reach.accent = true;
        setTip (reach, "c:out.reach");
        reach.onClick = [this] { start(); };
        stopBtn.onClick = [this] { stopSearch(); finish ("Stopped. Undo restores the Inflator and the Limiter.", false); };
        undoBtn.onClick = [this] { undo(); };
        row = { &makeKnob (*this, "out_gain", "Gain", "out.gain", col::orange, true) };
        startTimerHz (10);
    }
    ~OutputEditor() override
    {
        stopTimer();
        if (state == State::running && search == nullptr)
            ctx.engine.cancelCapture();
        search.reset();
    }
    int naturalHeight (int) const override { return headH + 8 + 116 + 8 + knobH + 8; }

    void update() override
    {
        ModuleEditor::update();
        static const float values[] = { -14.0f, -11.0f, -9.0f, -8.0f, -6.0f, -5.0f };
        int sel = -1;
        for (int i = 0; i < 6; ++i)
            if (std::abs (ctx.param ("out_target") - values[i]) < 0.05f)
                sel = i;
        if (sel >= 0)
            presets.setSelected (sel, false);
        box->repaint();
        repaint (tpArea);
    }

    void layoutBody (juce::Rectangle<int> r) override
    {
        auto b = r.removeFromTop (116);
        box->setBounds (b);
        auto in = b.reduced (10, 6);
        in.removeFromTop (24);
        auto line = in.removeFromTop (26);
        targetLabel = line.removeFromLeft (46);
        target.setBounds (line.removeFromLeft (92).withSizeKeepingCentre (92, 22));
        line.removeFromLeft (10);
        presets.setBounds (line.removeFromLeft (220).withSizeKeepingCentre (220, 24));
        line.removeFromLeft (12);
        auto btn = line.removeFromRight (106).withSizeKeepingCentre (106, 24);
        reach.setBounds (btn);
        stopBtn.setBounds (btn.withSizeKeepingCentre (60, 24).withX (btn.getRight() - 60));
        undoBtn.setBounds (btn.withSizeKeepingCentre (60, 24).withX (btn.getRight() - 60));
        statusArea = line.withTrimmedRight (8);
        in.removeFromTop (6);
        noteArea = in;
        r.removeFromTop (8);
        auto kr = r.removeFromTop (knobH);
        tpArea = kr.withTrimmedLeft (layoutKnobRow (kr, row) + 12).withWidth (240);
    }

    void paint (juce::Graphics& g) override
    {
        ModuleEditor::paint (g);
        g.setColour (col::text3);
        g.setFont (font (12.5f));
        g.drawText ("Out true peak", tpArea.withWidth (100), juce::Justification::centredLeft);
        const auto tp = ctx.out().truePeak;
        g.setColour (tp <= ctx.param ("lim_ceiling") ? col::ok : col::bad);
        g.setFont (font (12.5f, true));
        g.drawText (fmtLufs (tp) + " dBTP", tpArea.withTrimmedLeft (100), juce::Justification::centredLeft);
    }

private:
    void paintBox (juce::Graphics& g, juce::Rectangle<int> r)
    {
        drawSectionBox (g, r.withPosition (0, 0));
        auto in = r.withPosition (0, 0).reduced (10, 6);
        auto title = in.removeFromTop (24);
        g.setColour (col::text);
        g.setFont (semibold (13.0f));
        g.drawText ("Loudness target", title.removeFromLeft (124), juce::Justification::centredLeft);
        g.setColour (col::text3);
        g.setFont (font (12.5f));
        g.drawText (tr ("the plugin finds the cleanest way to reach it"), title, juce::Justification::centredLeft, true);
        const auto local = [&] (juce::Rectangle<int> a) { return a - box->getPosition(); };
        g.drawText ("Target", local (targetLabel), juce::Justification::centredLeft);
        juce::String text = status;
        juce::Colour c = col::text2;
        if (state == State::idle && text.isEmpty())
        {
            text = tr ("Play the song, then press:");
            c = col::text3;
        }
        if (state == State::running && noSignal > 1.5)
            text = tr ("Waiting for audio. Start playback.");
        if (state == State::done && reached)
            c = col::ok;
        g.setColour (c);
        g.setFont (font (12.5f, state == State::running));
        g.drawText (text, local (statusArea), state == State::idle && status.isEmpty() ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawFittedText (tr ("12 s of the song are recorded, then a copy of the chain tries the Inflator, Clip, Leveler, Density and Soften with the Drive that reaches the target, and keeps the mix with the least distortion and pumping. True peak stays at the Ceiling."),
                          local (noteArea), juce::Justification::topLeft, 2);
    }

    enum class State
    {
        idle,
        running,
        done
    };

    // Reach target with a distortion budget (D-042): 12 s of the song are recorded on the analysis
    // thread, then LoudnessSearch tries the loudness stages on a copy of the chain in the background
    static constexpr const char* touched[] = { "clip_on", "infl_effect", "lim_on", "lim_drive", "lim_ceiling", "lim_clip", "lim_density", "lim_soften", "lim_leveler",
                                               "lim_mode", "lim_adapt_gain", "lim_out_clip" };
    void start()
    {
        snapshot.clear();
        for (const auto* id : touched)
            snapshot[id] = ctx.param (id);
        ctx.monitoring.referenceActive = false;
        ctx.monitoring.refPlaying = false;
        ctx.engine.startCapture (12.0);
        state = State::running;
        reached = false;
        lastCapture = 0.0;
        noSignal = 0.0;
        startedAt = juce::Time::getMillisecondCounterHiRes();
        status = tr ("Recording the song");
        showButtons();
    }
    void stopSearch()
    {
        if (search != nullptr)
        {
            search->signalThreadShouldExit();
            search.reset();
        }
        else
            ctx.engine.cancelCapture();
    }
    void finish (const juce::String& msg, bool ok)
    {
        state = State::done;
        reached = ok;
        status = msg;
        showButtons();
    }
    void undo()
    {
        for (const auto& [id, v] : snapshot)
            ctx.setParam (id, v);
        snapshot.clear();
        state = State::idle;
        status.clear();
        showButtons();
        if (ctx.notify)
            ctx.notify ("The Inflator and the Limiter are back.");
    }
    void showButtons()
    {
        reach.setVisible (state != State::running && (state == State::idle || snapshot.empty()));
        stopBtn.setVisible (state == State::running);
        undoBtn.setVisible (state == State::done && ! snapshot.empty());
        box->repaint();
    }
    juce::String clock() const
    {
        const int t = juce::roundToInt ((juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0);
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }
    void timerCallback() override
    {
        if (state != State::running)
            return;
        const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
        if (search == nullptr)
        {
            const double p = ctx.engine.captureProgress();
            noSignal = p > lastCapture ? 0.0 : noSignal + 0.1;
            lastCapture = p;
            status = tr ("Recording the song") + " " + juce::String (juce::roundToInt (p * 12.0)) + " / 12 s";
            if (p >= 1.0)
            {
                auto rec = ctx.engine.takeCapture();
                if (rec.getNumSamples() == 0)
                    return;
                const double fs = ctx.engine.getSampleRate();
                const int length = (int) (8.0 * fs), pre = (int) fs;
                const int win = AutoMatchStrip::loudestWindow (rec, length);
                const int from = std::max (0, win - pre);
                const int n = std::min (rec.getNumSamples() - from, length + (win - from));
                juce::AudioBuffer<float> excerpt (2, n);
                for (int c = 0; c < 2; ++c)
                    excerpt.copyFrom (c, 0, rec, std::min (c, rec.getNumChannels() - 1), from, n);
                automatch::ParamMap current;
                for (auto* prm : ctx.proc.getParameters())
                    if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                        current[rp->paramID] = rp->convertFrom0to1 (rp->getValue());
                const int threads = (int) std::max (1u, std::thread::hardware_concurrency()) - 2;
                search = std::make_unique<automatch::LoudnessSearch> (fs, current, std::move (excerpt), win - from, ctx.param ("out_target"), threads);
                search->startThread (juce::Thread::Priority::low);
                noSignal = 0.0;
            }
            return;
        }
        auto& sr = *search;
        if (! sr.done.load())
        {
            if (! sr.isThreadRunning())
            {
                search.reset();
                finish ("Stopped. Undo restores the Inflator and the Limiter.", false);
                return;
            }
            const int ev = sr.evals.load(), planned = std::max (1, sr.plannedEvals.load());
            status = tr ("Trying the loudness stages:") + " " + juce::String (std::min (100, 100 * ev / planned)) + "%";
            return;
        }
        for (const auto& [id, v] : sr.result)
            ctx.setParam (id, v);
        const auto r = sr.result;
        const auto v = [&r] (const char* id) { const auto it = r.find (id); return it != r.end() ? it->second : 0.0f; };
        juce::String msg = (sr.reached ? "Reached " : "Stopped at ") + fmtMinus (sr.reachedLufs) + " LUFS in " + clock() + dot + "Inflator "
                         + juce::String (juce::roundToInt (v ("infl_effect"))) + "%" + dot + "Clip " + juce::String (v ("lim_clip"), 1) + " dB" + dot + "Leveler "
                         + juce::String (juce::roundToInt (v ("lim_leveler"))) + "%" + dot + "Density " + juce::String (juce::roundToInt (v ("lim_density"))) + "%";
        if (v ("lim_soften") > 0.5f)
            msg << dot << "Soften " << juce::roundToInt (v ("lim_soften")) << "%";
        msg << dot << "Drive " << fmtSigned (v ("lim_drive")) << " dB";
        if (! sr.reached)
            msg << ". " << tr ("The target cannot be reached cleanly: this is the loudest result.");
        const bool ok = sr.reached;
        search.reset();
        finish (msg, ok);
    }

    ValueField target;
    Segmented presets { { juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "14", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "11",
                          juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "9", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "8",
                          juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "6", juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) + "5" } };
    Btn reach { "Reach target" }, stopBtn { "Stop" }, undoBtn { "Undo" };
    Dropdown dither, bits, quality;
    Painter* box = nullptr;
    std::vector<Knob*> row;
    juce::Rectangle<int> targetLabel, statusArea, noteArea, tpArea;
    State state = State::idle;
    bool reached = false;
    juce::String status;
    std::map<juce::String, float> snapshot;
    double noSignal = 0.0, startedAt = 0.0, lastCapture = 0.0;
    std::unique_ptr<automatch::LoudnessSearch> search;
};

// ---- Stereo (mockup v12) ---------------------------------------------------------------------------------
/** Width per band: the module's setting (left axis, 0-200 %) against the measured width of your mix and
    of the reference (right axis, analysis width 0-100 %, D-033). Drag a band for its width, the yellow
    handle for the split frequency. */
class StGraph final : public juce::Component
{
public:
    explicit StGraph (Context& c) : ctx (c) { setTip (*this, "c:st.graph"); }
    float xOf (double f) const { return (float) (std::log (f / 20.0) / std::log (1000.0)) * getWidth(); }
    double fOf (float x) const { return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) x / getWidth())); }
    float pb() const { return getHeight() - 14.0f; }
    float ySetting (float w) const { return pb() - w / 200.0f * (pb() - 16.0f); }
    float yMeasured (double w) const { return pb() - (float) juce::jlimit (0.0, 100.0, w) / 100.0f * (pb() - 16.0f); }
    float lowSplit() const { return ctx.param ("lem_on") >= 0.5f && ctx.param ("lem_mono_on") >= 0.5f ? ctx.param ("lem_mono_freq") : 120.0f; }

    /** Measured widths: zone 1 (mid) averages the 150-500 Hz and 0.5-4 kHz analysis bands, zone 2 (high) is > 4 kHz. */
    std::pair<double, double> measured (const std::array<analysis::TapAnalyzer::StereoBand, analysis::numStereoBands>& b) const
    {
        return { 0.5 * (b[1].widthPercent + b[2].widthPercent), b[3].widthPercent };
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        g.setColour (col::sunk);
        g.fillRoundedRectangle (r, 6.0f);
        const float bottom = pb();
        g.setFont (font (11.0f));
        for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
        {
            const float x = xOf (f);
            g.setColour (juce::Colour (0xff1b1f26));
            g.drawVerticalLine ((int) x, 0.0f, bottom);
            g.setColour (col::text3);
            g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x + 3, bottom, 30, 14), juce::Justification::centredLeft);
        }
        const float dash[] = { 3.0f, 4.0f };
        g.setColour (col::line2);
        g.drawDashedLine (juce::Line<float> (0.0f, ySetting (100.0f), (float) getWidth(), ySetting (100.0f)), dash, 2, 1.0f);
        g.setColour (col::text3);
        g.drawText ("100% (no change)", juce::Rectangle<float> (4.0f, ySetting (100.0f) - 15.0f, 120.0f, 13.0f), juce::Justification::centredLeft);

        const float split = ctx.param ("st_split_freq"), lo = lowSplit();
        const float edges[4] = { 20.0f, lo, split, 20000.0f };
        // low zone: mono below the Low-End frequency
        g.setColour (juce::Colour (0xff20252d));
        g.fillRect (juce::Rectangle<float> (0.0f, 0.0f, xOf (lo), bottom));
        g.setColour (col::text3);
        g.drawText ("mono", juce::Rectangle<float> (6.0f, 4.0f, 60.0f, 14.0f), juce::Justification::centredLeft);

        const bool sig = ctx.snap.out.hasSignal;
        const auto mix = measured (ctx.snap.out.bandsShort);
        const auto t = ctx.refs.target();
        const bool hasRef = t.count > 0;
        const std::pair<double, double> ref { 0.5 * (t.width[1] + t.width[2]), t.width[3] };
        const juce::Colour colours[2] = { juce::Colour (0xff4fc6e0), juce::Colour (0xffb78cff) };
        static const char* names[] = { "Mid", "High" };
        for (int z = 0; z < 2; ++z)
        {
            const float x0 = xOf (edges[z + 1]), x1 = xOf (edges[z + 2]);
            const auto c = colours[z];
            const float w = ctx.param (z == 0 ? "st_mid_width" : "st_high_width");
            const double now = z == 0 ? mix.first : mix.second, rw = z == 0 ? ref.first : ref.second;
            g.setColour (c.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (x0, 0.0f, x1 - x0, bottom));
            if (sig)
            {
                const float yn = yMeasured (now);
                g.setColour (juce::Colour (0xff8b93a1).withAlpha (0.22f));
                g.fillRect (juce::Rectangle<float> (x0 + 6, yn, x1 - x0 - 12, bottom - yn));
            }
            const float y = ySetting (w);
            g.setColour (c.withAlpha (0.14f));
            g.fillRect (juce::Rectangle<float> (x0 + 4, y, x1 - x0 - 8, bottom - y));
            g.setColour (c);
            g.drawLine (x0 + 4, y, x1 - 4, y, 2.0f);
            if (hasRef)
            {
                g.setColour (col::text);
                g.drawDashedLine (juce::Line<float> (x0 + 4, yMeasured (rw), x1 - 4, yMeasured (rw)), dash, 2, 1.3f);
            }
            const float cx = (x0 + x1) / 2;
            g.setColour (c);
            g.fillEllipse (cx - 6, y - 6, 12, 12);
            g.setColour (col::sunk);
            g.drawEllipse (cx - 6, y - 6, 12, 12, 2.0f);
            juce::String full = juce::String (names[z]) + " " + juce::String (juce::roundToInt (w)) + "%";
            if (sig)
                full << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 mix ")) << juce::String (juce::roundToInt (now)) << "%";
            if (hasRef)
                full << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ref ")) << juce::String (juce::roundToInt (rw)) << "%";
            auto label = full;
            float lw = juce::GlyphArrangement::getStringWidth (font (11.5f, true), label) + 14.0f;
            if (lw > x1 - x0 - 4)
            {
                label = juce::String (names[z]) + " " + juce::String (juce::roundToInt (w)) + "%";
                lw = juce::GlyphArrangement::getStringWidth (font (11.5f, true), label) + 14.0f;
            }
            const float px = juce::jmax (x0 + 2, juce::jmin (x1 - lw - 2, cx - lw / 2));
            const juce::Rectangle<float> pill (px, juce::jmax (1.0f, y - 25.0f), lw, 17.0f);
            g.setColour (col::sunk);
            g.fillRoundedRectangle (pill, 4.0f);
            g.setColour (c);
            g.drawRoundedRectangle (pill, 4.0f, 1.0f);
            g.setFont (font (11.5f, true));
            g.drawText (label, pill, juce::Justification::centred, true);
        }
        // split handle
        const float xs = xOf (split);
        g.setColour (col::yellow);
        g.fillRect (xs - 1.0f, 0.0f, 2.0f, bottom);
        const auto st = hzText (split);
        const float sw = juce::GlyphArrangement::getStringWidth (font (11.5f, true), st) + 12.0f;
        const juce::Rectangle<float> pill (xs - sw / 2, bottom - 18.0f, sw, 16.0f);
        g.fillRoundedRectangle (pill, 4.0f);
        g.setColour (juce::Colour (0xff1b1600));
        g.setFont (font (11.5f, true));
        g.drawText (st, pill, juce::Justification::centred);
        // legend
        auto lg = juce::Rectangle<float> ((float) getWidth() - 214.0f, 6.0f, 208.0f, 20.0f);
        g.setColour (col::sunk);
        g.fillRoundedRectangle (lg, 4.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (lg, 4.0f, 1.0f);
        g.setFont (font (11.0f));
        auto item = [&] (float x, juce::Colour c, const char* t, int kind)
        {
            g.setColour (c);
            if (kind == 2)
                g.drawDashedLine (juce::Line<float> (lg.getX() + x, lg.getCentreY(), lg.getX() + x + 14.0f, lg.getCentreY()), dash, 2, 1.2f);
            else
                g.fillRect (juce::Rectangle<float> (lg.getX() + x, lg.getCentreY() - (kind == 1 ? 3.0f : 2.0f), 14.0f, kind == 1 ? 6.0f : 4.0f));
            g.setColour (col::text2);
            g.drawText (t, juce::Rectangle<float> (lg.getX() + x + 18.0f, lg.getY(), 56.0f, lg.getHeight()), juce::Justification::centredLeft);
        };
        item (8.0f, colours[0], "module", 0);
        item (74.0f, juce::Colour (0xff8b93a1).withAlpha (0.6f), "your mix", 1);
        item (144.0f, col::text, "reference", 2);
        g.setColour (col::line);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    }

    int hit (juce::Point<float> p, bool& isSplit) const
    {
        isSplit = std::abs (p.x - xOf (ctx.param ("st_split_freq"))) < 6.0f;
        if (isSplit)
            return 1;
        if (p.x > xOf (lowSplit()) && p.x < xOf (ctx.param ("st_split_freq")))
            return 1;
        if (p.x > xOf (ctx.param ("st_split_freq")))
            return 2;
        return 0;
    }
    juce::String idFor (int z, bool split) const { return split ? juce::String ("st_split_freq") : juce::String (z == 1 ? "st_mid_width" : "st_high_width"); }
    void mouseMove (const juce::MouseEvent& e) override
    {
        bool split = false;
        const int z = hit (e.position, split);
        setMouseCursor (z == 0 ? juce::MouseCursor::NormalCursor : (split ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        zone = hit (e.position, splitDrag);
        if (zone > 0)
        {
            ctx.apvts.getParameter (idFor (zone, splitDrag))->beginChangeGesture();
            mouseDrag (e);
        }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (zone <= 0)
            return;
        auto* p = ctx.apvts.getParameter (idFor (zone, splitDrag));
        const float v = splitDrag ? (float) juce::jlimit (1000.0, 10000.0, fOf (e.position.x))
                                  : juce::jlimit (0.0f, 200.0f, std::round ((pb() - e.position.y) / (pb() - 16.0f) * 200.0f));
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (zone > 0)
            ctx.apvts.getParameter (idFor (zone, splitDrag))->endChangeGesture();
        zone = 0;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        bool split = false;
        const int z = hit (e.position, split);
        if (z > 0 && ! split)
            ctx.setParam (idFor (z, false), 100.0f); // double-click: back to 100 % (no change)
    }
    Context& ctx;
    int zone = 0;
    bool splitDrag = false;
};

/** Vectorscope and correlation with the guard threshold. */
class StScope final : public juce::Component
{
public:
    explicit StScope (Context& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const float sz = std::min (r.getWidth(), r.getHeight() - 64.0f);
        scope = juce::Rectangle<float> (r.getX(), r.getY(), sz, sz);
        g.setColour (col::sunk);
        g.fillRoundedRectangle (scope, 6.0f);
        drawVectorscope (g, scope.reduced (4.0f), ctx.snap.out.scope);
        g.setColour (col::line);
        g.drawRoundedRectangle (scope.reduced (0.5f), 6.0f, 1.0f);
        auto c = r.withTrimmedTop (sz + 10.0f);
        corr = c;
        const double now = ctx.snap.out.broadShort.correlation;
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText ("Correlation", c.removeFromTop (16.0f), juce::Justification::centredLeft);
        g.setColour (col::text);
        g.setFont (font (13.0f, true));
        g.drawText (fmtSigned (now, 2), juce::Rectangle<float> (c.getX(), c.getY() - 16.0f, c.getWidth(), 16.0f), juce::Justification::centredRight);
        auto bar = c.removeFromTop (18.0f).reduced (0.0f, 1.0f);
        drawCorrelation (g, bar, now);
        const float thr = ctx.param ("st_guard_thr");
        const float gx = bar.getX() + (thr + 1.0f) / 2.0f * bar.getWidth();
        g.setColour (juce::Colours::white);
        g.fillRect (gx - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
        g.setColour (col::text3);
        g.setFont (font (11.5f));
        g.drawText (minusSign() + "1", c.withHeight (16.0f), juce::Justification::centredLeft);
        g.drawText ("+1", c.withHeight (16.0f), juce::Justification::centredRight);
        g.drawText ((ctx.param ("st_guard_on") >= 0.5f ? "guard " : "guard off ") + fmtSigned (thr, 2), c.withHeight (16.0f), juce::Justification::centred);
    }
    void mouseMove (const juce::MouseEvent& e) override { setTip (*this, scope.contains (e.position) ? "c:st.scope" : (corr.contains (e.position) ? "c:st.corr" : juce::String())); }
    Context& ctx;
    juce::Rectangle<float> scope, corr;
};

class StereoEditor final : public ModuleEditor
{
public:
    explicit StereoEditor (Context& c) : ModuleEditor (c, 6, "width per band and the correlation guard, drag a band up or down"), graph (c), scope (c)
    {
        guard.attach (ctx.apvts, "st_guard_on");
        setTip (guard, "t:Correlation guard\nReduces the stereo width when the correlation falls below the threshold.");
        addRight (guard, 136);
        addAndMakeVisible (graph);
        addAndMakeVisible (scope);
        row.push_back (&makeKnob (*this, "st_mid_width", "Mid width", "st.mid", col::orange));
        row.push_back (&makeKnob (*this, "st_high_width", "High width", "st.high", col::orange));
        row.push_back (&makeKnob (*this, "st_split_freq", "Split", "st.split", col::yellow));
        row.push_back (&makeKnob (*this, "st_ms_balance", "M/S bal.", "st.bal", col::orange, true));
        auto& g = makeKnob (*this, "st_guard_thr", "Guard thr", "st.guard", col::orange, true);
        g.meterInverse = true;
        g.meterLevel = [this]
        {
            if (ctx.param ("st_on") < 0.5f || ctx.param ("st_guard_on") < 0.5f)
                return noSignal();
            const float v = ctx.proc.getChain().getStereo().guardCorrelation();
            return v > 1.5f ? noSignal() : v;
        };
        row.push_back (&g);
    }
    int naturalHeight (int) const override { return compact ? headH + 8 + 120 + 8 + knobH + 8 : headH + 8 + 176 + 8 + knobH + 8; }
    void update() override
    {
        ModuleEditor::update();
        graph.repaint();
        scope.repaint();
    }
    void layoutBody (juce::Rectangle<int> r) override
    {
        auto top = r.removeFromTop (compact ? 120 : 176);
        if (! compact)
        {
            scope.setBounds (top.removeFromRight (236));
            top.removeFromRight (10);
            scope.setVisible (true);
        }
        else
            scope.setVisible (false);
        graph.setBounds (top);
        r.removeFromTop (8);
        layoutKnobRow (r.removeFromTop (knobH), row);
    }
    StGraph graph;
    StScope scope;
    Btn guard { "Correlation guard", true };
    std::vector<Knob*> row;
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
        case 3: return std::make_unique<LowEndEditor> (ctx);
        case 6: return std::make_unique<StereoEditor> (ctx);
        case 7: return std::make_unique<InflatorEditor> (ctx);
        case 8: return std::make_unique<LimiterEditor> (ctx);
        case 9: return std::make_unique<OutputEditor> (ctx);
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
        case 4:
        {
            e = std::make_unique<GenericEditor> (ctx, 4, "glue compressor for the whole mix", orange,
                                                 std::vector<KSpec> { { "bus_thr", "Thresh", "bc.thr" }, { "bus_ratio", "Ratio", "bc.ratio" }, { "bus_att", "Attack", "bc.attack" },
                                                                      { "bus_rel", "Release", "bc.release" }, { "bus_sc_hpf", "SC HPF", "bc.schpf" }, { "bus_mix", "Mix", "bc.mix" },
                                                                      { "bus_detect", "Peak/RMS", "bc.detect" }, { "bus_link", "Link", "bc.link" } });
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
        default: break;
    }
    return e;
}
} // namespace mm::ui
