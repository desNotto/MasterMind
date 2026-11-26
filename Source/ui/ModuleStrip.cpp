#include "ui/ModuleStrip.h"

#include "state/Parameters.h"

#include <algorithm>

namespace mm::ui
{
namespace
{
int knobWidth (const juce::String& label) { return std::max (52, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.0f), label)) + 8); }

/** Ten band chips of the Dynamic EQ. */
class BandChips final : public juce::Component
{
public:
    explicit BandChips (Context& c) : ctx (c) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        const float w = (getWidth() - 27.0f) / 10.0f;
        for (int b = 1; b <= 10; ++b)
        {
            const juce::Rectangle<float> r ((b - 1) * (w + 3.0f), 0.0f, w, (float) getHeight());
            const bool on = ctx.param (params::eq (b, "on")) >= 0.5f;
            const bool sel = ctx.selectedEqBand == b;
            g.setColour (sel ? col::yellow : col::sunk);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (sel ? col::yellow : (on ? juce::Colour (0xff6b5d2a) : col::line2));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            g.setColour (sel ? juce::Colour (0xff1b1600) : (on ? col::text : col::text3));
            g.setFont (font (12.0f, sel));
            g.drawText (juce::String (b), r, juce::Justification::centred);
        }
    }
    int bandAt (float x) const { return juce::jlimit (1, 10, 1 + (int) (x / ((getWidth() + 3.0f) / 10.0f))); }
    void mouseMove (const juce::MouseEvent& e) override { setTip (*this, "c:eq.band:" + juce::String (bandAt (e.position.x))); }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const int b = bandAt (e.position.x);
        if (e.mods.isPopupMenu())
            ctx.setParam (params::eq (b, "on"), ctx.param (params::eq (b, "on")) >= 0.5f ? 0.0f : 1.0f);
        ctx.selectedEqBand = b;
        if (onSelect)
            onSelect (b);
        repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = bandAt (e.position.x);
        ctx.setParam ("eq_on", 1.0f);
        ctx.setParam (params::eq (b, "on"), ctx.param (params::eq (b, "on")) >= 0.5f ? 0.0f : 1.0f);
    }
    std::function<void (int)> onSelect;
    Context& ctx;
};

/** Small painted readouts (gain reduction bars, text lines). */
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
} // namespace

// ==================================================================================================
class ModuleStrip::Card final : public juce::Component
{
public:
    struct Item
    {
        juce::Component* comp;
        int width, height;
    };

    Card (Context& c, juce::String k, juce::String n, const juce::String& onParam) : ctx (c), key (std::move (k)), name (std::move (n))
    {
        led = std::make_unique<Led> (ctx.apvts, onParam);
        addAndMakeVisible (*led);
        setTip (*led, "m:" + key);
        header.draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (col::text);
            g.setFont (semibold (13.5f));
            g.drawText (name, r.withTrimmedLeft (2), juce::Justification::centredLeft);
        };
        addAndMakeVisible (header);
        setTip (header, "m:" + key);
        header.addMouseListener (this, false);
        addAndMakeVisible (rightLabel);
        rightLabel.setJustificationType (juce::Justification::centredRight);
        rightLabel.setFont (font (12.5f));
        rightLabel.setColour (juce::Label::textColourId, col::text3);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == &header && ctx.showModuleDetail)
            ctx.showModuleDetail (key);
    }

    juce::Component& add (std::unique_ptr<juce::Component> c, int row, int width, int height)
    {
        auto& ref = *c;
        addAndMakeVisible (ref);
        rows[(size_t) row].push_back ({ &ref, width, height });
        owned.push_back (std::move (c));
        return ref;
    }

    Knob& knob (const juce::String& id, const juce::String& label, const juce::String& tipId, juce::Colour colour = col::orange, bool bipolar = false, int row = 0)
    {
        auto k = std::make_unique<Knob> (ctx.apvts, id, label, colour, bipolar);
        setTip (*k, "k:" + tipId);
        auto* raw = k.get();
        add (std::move (k), row, knobWidth (label), 66);
        return *raw;
    }

    int naturalWidth() const
    {
        int w = 0;
        for (const auto& row : rows)
        {
            int rw = 0;
            for (const auto& it : row)
                rw += it.width + 4;
            w = std::max (w, rw);
        }
        return std::max (w + 14, minWidth);
    }

    void paint (juce::Graphics& g) override
    {
        const bool on = led->isOn();
        const bool cur = ctx.selectedModule == index;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (col::panel2);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (cur ? col::orange : col::line);
        g.drawRoundedRectangle (r, 7.0f, 1.0f);
        g.setColour (col::line);
        g.drawHorizontalLine (28, 1.0f, (float) getWidth() - 1.0f);
        if (! on)
        {
            g.setColour (col::bg.withAlpha (0.45f));
            g.fillRoundedRectangle (r.withTrimmedTop (29), 7.0f);
        }
    }

    void resized() override
    {
        auto head = getLocalBounds().removeFromTop (28).reduced (8, 0);
        led->setBounds (head.removeFromLeft (12));
        head.removeFromLeft (4);
        rightLabel.setBounds (head.removeFromRight (std::min (110, head.getWidth() / 2)));
        header.setBounds (head);
        auto body = getLocalBounds().withTrimmedTop (34).reduced (7, 0);
        for (const auto& row : rows)
        {
            if (row.empty())
                continue;
            int h = 0;
            for (const auto& it : row)
                h = std::max (h, it.height);
            auto line = body.removeFromTop (h);
            for (const auto& it : row)
            {
                it.comp->setBounds (line.removeFromLeft (it.width).withHeight (it.height));
                line.removeFromLeft (4);
            }
            body.removeFromTop (4);
        }
    }

    void refreshOverlay()
    {
        const bool on = led->isOn();
        rightLabel.setText (on ? rightText() : juce::String ("Bypassed"), juce::dontSendNotification);
        for (auto& o : owned)
            o->setAlpha (on ? 1.0f : 0.55f);
        repaint();
    }

    Context& ctx;
    juce::String key, name;
    int index = 0, minWidth = 120;
    std::unique_ptr<Led> led;
    Painter header;
    juce::Label rightLabel;
    std::array<std::vector<Item>, 3> rows;
    std::vector<std::unique_ptr<juce::Component>> owned;
    std::function<juce::String()> rightText = [] { return juce::String(); };
    std::function<void()> onUpdate;
};

// ==================================================================================================
ModuleStrip::ModuleStrip (Context& context) : ctx (context)
{
    const auto makeCard = [this] (const char* key, const char* name, const char* onParam)
    {
        cards.push_back (std::make_unique<Card> (ctx, key, name, onParam));
        cards.back()->index = (int) cards.size() - 1;
        addAndMakeVisible (*cards.back());
        return cards.back().get();
    };

    // ---- Input ----
    {
        auto* c = makeCard ("in", "Input", "in_on");
        c->knob ("in_trim", "Trim", "in.trim", col::orange, true);
        c->knob ("hpf_freq", "Subsonic", "in.sub", col::yellow);
        c->knob ("hpf_slope", "Slope", "in.slope", col::text2);
        for (auto [id, label, tip] : { std::tuple { "in_pol_l", juce::String (juce::CharPointer_UTF8 ("\xc3\x98 L")), "c:in.polL" },
                                       std::tuple { "in_pol_r", juce::String (juce::CharPointer_UTF8 ("\xc3\x98 R")), "c:in.polR" } })
        {
            auto b = std::make_unique<Btn> (label, true);
            b->attach (ctx.apvts, id);
            setTip (*b, tip);
            c->add (std::move (b), 1, 40, 24);
        }
        auto hpf = std::make_unique<Btn> ("HPF", true);
        hpf->attach (ctx.apvts, "hpf_on");
        setTip (*hpf, "t:Subsonic HPF\nTurns the subsonic filter and the 5 Hz DC blocker on and off.");
        c->add (std::move (hpf), 1, 44, 24);
        c->rightText = [this] { return ctx.param ("hpf_on") >= 0.5f ? juce::String ("DC block on") : juce::String(); };
    }

    // ---- Dynamic EQ ----
    {
        auto* c = makeCard ("eq", "Dynamic EQ", "eq_on");
        auto chips = std::make_unique<BandChips> (ctx);
        chips->onSelect = [this] (int b) { selectEqBand (b); };
        c->add (std::move (chips), 0, 230, 18);
        c->knob (params::eq (5, "freq"), "Freq", "eq.freq", col::yellow, false, 1);
        c->knob (params::eq (5, "gain"), "Gain", "eq.gain", col::yellow, true, 1);
        c->knob (params::eq (5, "q"), "Q", "eq.q", col::yellow, false, 1);
        c->knob (params::eq (5, "thr"), "Thresh", "eq.thr", col::yellow, false, 1);
        c->rightText = [this] { return ctx.param ("eq_phase") >= 0.5f ? juce::String ("Linear phase") : juce::String ("Min phase"); };
        setTip (c->rightLabel, "c:eq.phase");
        c->rightLabel.setInterceptsMouseClicks (true, false);
        c->rightLabel.addMouseListener (this, false);
    }

    // ---- Multiband ----
    {
        auto* c = makeCard ("mb", "Multiband", "mb_on");
        auto bars = std::make_unique<Painter>();
        bars->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            static const char* labels[] = { "<80", "250", "1.5k", "5.5k", ">5.5k" };
            const float w = r.getWidth() / 5.0f;
            for (int b = 0; b < 5; ++b)
            {
                const auto gr = juce::jlimit (0.0f, 12.0f, -ctx.proc.getChain().getMultiband().gainReductionDb (b));
                const juce::Rectangle<float> t (r.getX() + b * w + w / 2 - 5, (float) r.getY(), 10.0f, r.getHeight() - 18.0f);
                g.setColour (col::sunk);
                g.fillRoundedRectangle (t, 3.0f);
                g.setColour (b == 0 ? col::orange : col::line2);
                g.drawRoundedRectangle (t, 3.0f, 1.0f);
                g.setColour (col::orange);
                g.fillRoundedRectangle (t.withHeight (t.getHeight() * gr / 12.0f), 3.0f);
                g.setColour (col::text3);
                g.setFont (font (12.0f));
                g.drawText (labels[b], juce::Rectangle<float> (r.getX() + b * w, t.getBottom() + 2, w, 14), juce::Justification::centred);
            }
        };
        c->add (std::move (bars), 0, 190, 66);
        auto xo = std::make_unique<Painter>();
        xo->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            juce::String s = "X-over";
            for (int i = 1; i <= 4; ++i)
            {
                const float f = ctx.param (params::mbXo (i));
                s << (i == 1 ? " " : juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))) << (f >= 1000.0f ? juce::String (f / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (f)));
            }
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText (s, r, juce::Justification::centredLeft);
        };
        c->add (std::move (xo), 1, 190, 16);
        c->rightText = [] { return juce::String(); };
    }

    // ---- Low-End ----
    {
        auto* c = makeCard ("le", "Low-End", "lem_on");
        c->knob ("lem_mono_freq", "Mono", "le.mono", col::blue);
        c->knob ("lem_mono_amount", "Amount", "le.amount", col::blue);
        auto& rot = c->knob ("lem_rot_angle", "Rotate", "le.rotate", col::blue);
        auto& tr = c->knob ("lem_tr_amount", "Translator", "le.trans", col::blue);
        rot.onUserChange = [this] { ctx.setParam ("lem_rot_on", ctx.param ("lem_rot_angle") > 0.5f ? 1.0f : 0.0f); };
        tr.onUserChange = [this] { ctx.setParam ("lem_tr_on", ctx.param ("lem_tr_amount") > 0.5f ? 1.0f : 0.0f); };
        c->rightText = [this]
        {
            const float tl = ctx.proc.getChain().getLowEnd().translatorLevelDb();
            return ctx.param ("lem_tr_on") >= 0.5f && tl > -90.0f ? "Tr " + fmtMinus (tl, 0) + " dB" : juce::String();
        };
    }

    // ---- Bus Comp ----
    {
        auto* c = makeCard ("bc", "Bus Comp", "bus_on");
        c->knob ("bus_thr", "Thresh", "bc.thr");
        c->knob ("bus_ratio", "Ratio", "bc.ratio");
        c->knob ("bus_att", "Attack", "bc.attack");
        auto& rel = c->knob ("bus_rel", "Release", "bc.release");
        rel.textOverride = [this, &rel] { return ctx.param ("bus_rel_auto") >= 0.5f ? juce::String ("Auto") : ctx.apvts.getParameter (rel.getParamId())->getCurrentValueAsText(); };
        rel.onUserChange = [this] { ctx.setParam ("bus_rel_auto", 0.0f); };
        c->knob ("bus_sc_hpf", "SC HPF", "bc.schpf");
        c->knob ("bus_mix", "Mix", "bc.mix");
        auto gr = std::make_unique<Painter>();
        gr->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setFont (font (12.0f));
            g.setColour (col::text3);
            g.drawText (ctx.param ("bus_model") >= 0.5f ? "Punch" : "VCA", r, juce::Justification::centredLeft);
            const float grDb = ctx.proc.getChain().getBusComp().gainReductionDb();
            g.drawText ("GR", r.withTrimmedRight (62), juce::Justification::centredRight);
            g.setColour (col::orange);
            g.setFont (font (12.0f, true));
            g.drawText (fmtMinus (grDb) + " dB", r.removeFromRight (58), juce::Justification::centredRight);
        };
        c->add (std::move (gr), 1, 340, 18);
        c->rightText = [this] { return ctx.param ("bus_model") >= 0.5f ? juce::String ("Punch") : juce::String ("VCA"); };
        setTip (c->rightLabel, "c:bc.model");
        c->rightLabel.addMouseListener (this, false);
    }

    // ---- Saturation ----
    {
        auto* c = makeCard ("sat", "Saturation", "sat_on");
        c->knob (params::sat (1, "drive"), "Low", "sat.low");
        c->knob (params::sat (2, "drive"), "Mid", "sat.mid");
        c->knob (params::sat (3, "drive"), "High", "sat.high");
    }

    // ---- Stereo ----
    {
        auto* c = makeCard ("st", "Stereo", "st_on");
        c->knob ("st_mid_width", "Mid width", "st.mid");
        c->knob ("st_high_width", "High width", "st.high");
        c->rightText = [this] { return ctx.param ("st_guard_on") >= 0.5f ? juce::String ("Guard on") : juce::String(); };
    }

    // ---- Clipper ----
    {
        auto* c = makeCard ("cl", "Clipper", "clip_on");
        c->knob ("clip_thr", "Thresh", "cl.thr", col::magenta);
        c->knob ("clip_mix", "Mix", "cl.mix", col::magenta);
        auto type = std::make_unique<Dropdown>();
        type->attach (ctx.apvts, "clip_type");
        setTip (*type, "c:cl.type");
        c->add (std::move (type), 1, 84, 24);
        auto pr = std::make_unique<Painter>();
        pr->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText ("Peak", r.withTrimmedRight (60), juce::Justification::centredRight);
            g.setColour (col::orange);
            g.setFont (font (12.0f, true));
            g.drawText (fmtMinus (ctx.proc.getChain().getClipper().peakReductionDb()) + " dB", r.removeFromRight (58), juce::Justification::centredRight);
        };
        c->add (std::move (pr), 1, 110, 24);
        c->rightText = [this] { return ctx.param ("clip_lowprot") >= 0.5f ? juce::String ("Low protect") : juce::String ("No protect"); };
        setTip (c->rightLabel, "c:cl.lowprot");
        c->rightLabel.addMouseListener (this, false);
    }

    // ---- Limiter ----
    {
        auto* c = makeCard ("li", "Limiter", "lim_on");
        c->knob ("lim_drive", "Drive", "li.drive", col::magenta);
        c->knob ("lim_ceiling", "Ceiling", "li.ceil", col::magenta);
        auto& rel = c->knob ("lim_rel", "Release", "li.rel", col::magenta);
        rel.textOverride = [this, &rel] { return ctx.param ("lim_rel_auto") >= 0.5f ? juce::String ("Auto") : ctx.apvts.getParameter (rel.getParamId())->getCurrentValueAsText(); };
        rel.onUserChange = [this] { ctx.setParam ("lim_rel_auto", 0.0f); };
        c->knob ("lim_lookahead", "Lookahead", "li.la", col::magenta);
        auto mode = std::make_unique<Dropdown>();
        mode->attach (ctx.apvts, "lim_mode");
        setTip (*mode, "c:li.mode");
        c->add (std::move (mode), 1, 104, 24);
        auto gr = std::make_unique<Painter>();
        gr->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText ("GR", r.withTrimmedRight (62), juce::Justification::centredRight);
            g.setColour (col::orange);
            g.setFont (font (12.0f, true));
            g.drawText (fmtMinus (ctx.proc.getChain().getLimiter().gainReductionDb()) + " dB", r.removeFromRight (58), juce::Justification::centredRight);
        };
        c->add (std::move (gr), 1, 130, 24);
        c->rightText = [] { return juce::String ("True peak"); };
    }

    // ---- Output ----
    {
        auto* c = makeCard ("out", "Output", "out_on");
        c->knob ("out_gain", "Gain", "out.gain", col::orange, true);
        auto dither = std::make_unique<Dropdown>();
        dither->attach (ctx.apvts, "out_dither");
        setTip (*dither, "c:out.dither");
        c->add (std::move (dither), 0, 110, 24);
        auto tp = std::make_unique<Painter>();
        tp->draw = [this] (juce::Graphics& g, juce::Rectangle<int> r)
        {
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText ("Out TP", r.withWidth (44), juce::Justification::centredLeft);
            const double tpv = ctx.out().truePeak;
            g.setColour (tpv <= ctx.param ("lim_ceiling") ? col::ok : col::bad);
            g.setFont (font (12.0f, true));
            g.drawText (fmtLufs (tpv), r.withTrimmedLeft (46), juce::Justification::centredLeft);
        };
        c->add (std::move (tp), 1, 110, 20);
    }

    for (auto* id : { "eq_phase", "bus_model", "clip_lowprot", "eq_on", "mb_on", "lem_on", "bus_on", "sat_on", "st_on", "clip_on", "lim_on", "out_on", "in_on" })
        ctx.apvts.addParameterListener (id, this);
    selectEqBand (ctx.selectedEqBand);
}

ModuleStrip::~ModuleStrip()
{
    for (auto* id : { "eq_phase", "bus_model", "clip_lowprot", "eq_on", "mb_on", "lem_on", "bus_on", "sat_on", "st_on", "clip_on", "lim_on", "out_on", "in_on" })
        ctx.apvts.removeParameterListener (id, this);
}

void ModuleStrip::mouseUp (const juce::MouseEvent& e)
{
    // clickable header texts toggle a setting
    const auto toggle = [this] (const char* id) { ctx.setParam (id, ctx.param (id) >= 0.5f ? 0.0f : 1.0f); };
    if (e.eventComponent == &cards[1]->rightLabel)
        toggle ("eq_phase");
    else if (e.eventComponent == &cards[4]->rightLabel)
        toggle ("bus_model");
    else if (e.eventComponent == &cards[7]->rightLabel)
        toggle ("clip_lowprot");
}

void ModuleStrip::parameterChanged (const juce::String&, float)
{
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ModuleStrip> (this)]
    {
        if (safe)
            for (auto& c : safe->cards)
                c->refreshOverlay();
    });
}

void ModuleStrip::selectEqBand (int band)
{
    band = juce::jlimit (1, 10, band);
    shownEqBand = band;
    ctx.selectedEqBand = band;
    ctx.selectedModule = 1;
    auto* eq = cards[1].get();
    int k = 0;
    for (auto& o : eq->owned)
        if (auto* knob = dynamic_cast<Knob*> (o.get()))
        {
            static const char* fields[] = { "freq", "gain", "q", "thr" };
            knob->rebind (params::eq (band, fields[k++]));
        }
    eq->repaint();
    for (auto& c : cards)
        c->repaint();
}

void ModuleStrip::update()
{
    if (ctx.selectedEqBand != shownEqBand)
        selectEqBand (ctx.selectedEqBand);
    for (auto& c : cards)
    {
        c->refreshOverlay();
        for (auto& o : c->owned)
            if (dynamic_cast<Painter*> (o.get()) != nullptr || dynamic_cast<BandChips*> (o.get()) != nullptr)
                o->repaint();
    }
}

void ModuleStrip::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    auto r = paintPanelHeader (g, getLocalBounds(), "Signal chain", "10 modules, fixed order: left to right, top to bottom");
    juce::ignoreUnused (r);
    g.setColour (col::text3);
    g.setFont (font (12.5f));
    const int lat = ctx.proc.getLatencySamples();
    g.drawText ("Chain latency " + juce::String (lat) + " smp", getLocalBounds().removeFromTop (32).reduced (12, 0), juce::Justification::centredRight);
}

void ModuleStrip::resized()
{
    auto r = getLocalBounds().withTrimmedTop (38).reduced (10, 0).withTrimmedBottom (10);
    const int gap = 8;
    const int rowH = (r.getHeight() - gap) / 2;
    for (int row = 0; row < 2; ++row)
    {
        auto line = r.removeFromTop (rowH);
        r.removeFromTop (gap);
        const int first = row * 5;
        int natural = 0;
        for (int i = first; i < first + 5; ++i)
            natural += cards[(size_t) i]->naturalWidth();
        const int extra = std::max (0, line.getWidth() - natural - 4 * gap) / 5;
        for (int i = first; i < first + 5; ++i)
        {
            const int w = i == first + 4 ? line.getWidth() : cards[(size_t) i]->naturalWidth() + extra;
            cards[(size_t) i]->setBounds (line.removeFromLeft (w));
            line.removeFromLeft (gap);
        }
    }
}

// ==================================================================================================
void ModuleDetail::Header::paint (juce::Graphics& g)
{
    if (groups == nullptr)
        return;
    for (const auto& [name, r] : *groups)
    {
        g.setColour (col::text2);
        g.setFont (semibold (13.0f));
        g.drawText (name, r, juce::Justification::centredLeft);
        g.setColour (col::line);
        g.drawHorizontalLine (r.getBottom(), (float) r.getX(), (float) r.getRight());
    }
}

ModuleDetail::ModuleDetail (Context& context, const juce::String& moduleKey) : ctx (context), key (moduleKey)
{
    static const std::map<juce::String, std::pair<juce::String, juce::StringArray>> modules {
        { "in", { "Input", { "in_", "hpf_" } } }, { "eq", { "Dynamic EQ", { "eq_" } } }, { "mb", { "Multiband", { "mb_" } } },
        { "le", { "Low-End", { "lem_" } } }, { "bc", { "Bus Comp", { "bus_" } } }, { "sat", { "Saturation", { "sat_" } } },
        { "st", { "Stereo", { "st_" } } }, { "cl", { "Clipper", { "clip_" } } }, { "li", { "Limiter", { "lim_" } } },
        { "out", { "Output", { "out_", "quality" } } }
    };
    const auto it = modules.find (key);
    title = it != modules.end() ? it->second.first : key;
    const auto prefixes = it != modules.end() ? it->second.second : juce::StringArray();

    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    content.addAndMakeVisible (headerPainter);
    headerPainter.groups = &groupHeaders;
    addAndMakeVisible (close);
    close.onClick = [this] { if (onClose) onClose(); };

    // group by band ("eq_b3_", "mb_b2_", "sat_b1_") or the module itself
    std::vector<std::pair<juce::String, std::vector<const params::Spec*>>> groups;
    for (const auto& s : params::all())
    {
        bool match = false;
        for (const auto& p : prefixes)
            match = match || s.id.startsWith (p);
        if (! match)
            continue;
        juce::String group = title;
        if (s.id.startsWith ("eq_b"))
            group = "Band " + s.id.fromFirstOccurrenceOf ("eq_b", false, false).upToFirstOccurrenceOf ("_", false, false);
        else if (s.id.startsWith ("mb_b"))
            group = "Band " + s.id.fromFirstOccurrenceOf ("mb_b", false, false).upToFirstOccurrenceOf ("_", false, false);
        else if (s.id.startsWith ("sat_b"))
        {
            static const char* names[] = { "", "Low band", "Mid band", "High band" };
            group = names[juce::jlimit (0, 3, s.id.fromFirstOccurrenceOf ("sat_b", false, false).getIntValue())];
        }
        auto g = std::find_if (groups.begin(), groups.end(), [&] (const auto& x) { return x.first == group; });
        if (g == groups.end())
        {
            groups.push_back ({ group, {} });
            g = groups.end() - 1;
        }
        g->second.push_back (&s);
    }

    int y = 8;
    const int width = 860;
    for (const auto& [group, specs] : groups)
    {
        groupHeaders.push_back ({ group, juce::Rectangle<int> (12, y, width - 24, 22) });
        y += 28;
        int x = 12, rowH = 0;
        for (const auto* s : specs)
        {
            auto label = s->name.fromFirstOccurrenceOf (" ", false, false);
            if (s->id.startsWith ("eq_b") || s->id.startsWith ("mb_b") || s->id.startsWith ("sat_b"))
                label = s->name.fromLastOccurrenceOf (" ", false, false);
            if (label.isEmpty())
                label = s->name;
            std::unique_ptr<juce::Component> c;
            int w = 0, h = 0;
            if (s->kind == params::Kind::floating)
            {
                auto k = std::make_unique<Knob> (ctx.apvts, s->id, label.substring (0, 12), col::orange, s->minValue < 0 && s->maxValue > 0);
                setTip (*k, "k:" + s->id);
                w = knobWidth (label.substring (0, 12));
                h = 66;
                c = std::move (k);
            }
            else if (s->kind == params::Kind::toggle)
            {
                auto b = std::make_unique<Btn> (label, true);
                b->attach (ctx.apvts, s->id);
                setTip (*b, "t:" + s->name);
                w = std::max (60, juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (13.0f), label)) + 20);
                h = 26;
                c = std::move (b);
            }
            else
            {
                auto d = std::make_unique<Dropdown>();
                d->attach (ctx.apvts, s->id);
                setTip (*d, "t:" + s->name);
                w = 130;
                h = 26;
                c = std::move (d);
            }
            if (x + w > width - 12)
            {
                x = 12;
                y += rowH + 8;
                rowH = 0;
            }
            c->setBounds (x, y + (h < 60 ? 20 : 0), w, h);
            content.addAndMakeVisible (*c);
            controls.push_back (std::move (c));
            x += w + 8;
            rowH = std::max (rowH, 66);
        }
        y += rowH + 14;
    }
    content.setSize (width, y + 8);
    headerPainter.setBounds (content.getLocalBounds());
    headerPainter.setInterceptsMouseClicks (false, false);
}

void ModuleDetail::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.55f));
    auto panel = getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 40, 900), std::min (getHeight() - 40, 640));
    drawPanel (g, panel.toFloat(), col::panel, 10.0f);
    paintPanelHeader (g, panel, title + ": all parameters", "Double-click a knob to reset it");
}

void ModuleDetail::resized()
{
    auto panel = getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 40, 900), std::min (getHeight() - 40, 640));
    close.setBounds (panel.getRight() - 78, panel.getY() + 4, 70, 24);
    viewport.setBounds (panel.withTrimmedTop (34).reduced (4));
}

void ModuleDetail::mouseDown (const juce::MouseEvent& e)
{
    auto panel = getLocalBounds().withSizeKeepingCentre (std::min (getWidth() - 40, 900), std::min (getHeight() - 40, 640));
    if (! panel.contains (e.getPosition()) && onClose)
        onClose();
}
} // namespace mm::ui
