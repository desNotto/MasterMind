#include "ui/EyesView.h"
#include "ui/Locale.h"

namespace mm::ui
{
namespace
{
int lightKind (analysis::Light l)
{
    switch (l)
    {
        case analysis::Light::ok: return 0;
        case analysis::Light::warn: return 1;
        case analysis::Light::bad: return 2;
        default: return 3;
    }
}

juce::String noteName (double f)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int r = juce::roundToInt (12.0 * std::log2 (f / 440.0) + 69.0);
    return juce::String (names[((r % 12) + 12) % 12]) + juce::String (r / 12 - 1);
}

void drawStatBox (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const juce::String& value, const juce::String& unit, juce::Colour c = col::text)
{
    g.setColour (col::panel2);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (col::line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    auto in = r.reduced (8.0f, 4.0f);
    g.setColour (col::text3);
    g.setFont (font (12.0f));
    g.drawText (label, in.removeFromTop (16), juce::Justification::left);
    g.setColour (c);
    g.setFont (font (15.0f, true));
    const float vw = juce::GlyphArrangement::getStringWidth (font (15.0f, true), value) + 4;
    g.drawText (value, in.withWidth (vw), juce::Justification::left);
    g.setColour (col::text3);
    g.setFont (font (12.0f));
    g.drawText (unit, in.withTrimmedLeft (vw), juce::Justification::left);
}
} // namespace

const analysis::TapView& viewTap (const Context& c) { return c.tap == 0 ? c.snap.in : c.snap.out; }

void drawVectorscope (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<std::pair<float, float>>& pts)
{
    g.setColour (col::sunk);
    g.fillRoundedRectangle (r, 6.0f);
    const auto c = r.getCentre();
    const float rad = r.getWidth() * 0.5f - 6;
    g.setColour (juce::Colour (0xff262b34));
    g.drawEllipse (juce::Rectangle<float> (2 * rad, 2 * rad).withCentre (c), 1.0f);
    g.drawLine (c.x, r.getY() + 6, c.x, r.getBottom() - 6);
    g.drawLine (r.getX() + 6, c.y, r.getRight() - 6, c.y);
    g.drawLine (c.x - rad * 0.707f, c.y - rad * 0.707f, c.x + rad * 0.707f, c.y + rad * 0.707f);
    g.drawLine (c.x + rad * 0.707f, c.y - rad * 0.707f, c.x - rad * 0.707f, c.y + rad * 0.707f);
    g.setColour (col::text3);
    g.setFont (font (12.0f));
    g.drawText ("M", juce::Rectangle<float> (c.x - 10, r.getY() + 2, 20, 14), juce::Justification::centred);
    g.drawText ("L", juce::Rectangle<float> (c.x - rad * 0.74f - 14, c.y - rad * 0.74f - 2, 14, 14), juce::Justification::centred);
    g.drawText ("R", juce::Rectangle<float> (c.x + rad * 0.74f, c.y - rad * 0.74f - 2, 14, 14), juce::Justification::centred);
    g.drawText ("-S", juce::Rectangle<float> (r.getX() + 4, c.y - 7, 20, 14), juce::Justification::left);
    g.drawText ("+S", juce::Rectangle<float> (r.getRight() - 24, c.y - 7, 20, 14), juce::Justification::right);
    // auto gain so quiet material is visible
    float peak = 0.05f;
    for (const auto& [m, s] : pts)
        peak = std::max (peak, std::max (std::abs (m), std::abs (s)));
    const float k = rad / peak * 0.9f;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        const float x = c.x + pts[i].second * k, y = c.y - pts[i].first * k;
        g.setColour (col::orange.withAlpha (0.15f + 0.55f * (float) i / (float) pts.size()));
        g.fillRect (x, y, 1.6f, 1.6f);
    }
}

void drawCorrelation (juce::Graphics& g, juce::Rectangle<float> r, double corr)
{
    juce::ColourGradient grad (col::bad.withAlpha (0.35f), r.getX(), 0, col::ok.withAlpha (0.35f), r.getRight(), 0, false);
    grad.addColour (0.5, col::warn.withAlpha (0.25f));
    g.setGradientFill (grad);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (col::line2);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    const float x = r.getX() + (float) (corr + 1.0) * 0.5f * r.getWidth();
    g.setColour (juce::Colours::white);
    g.fillRoundedRectangle (x - 1.5f, r.getY() - 3, 3.0f, r.getHeight() + 6, 1.5f);
}

// ---- Tonal --------------------------------------------------------------------------------------------
TonalPanel::TonalPanel (Context& c) : ctx (c)
{
    addAndMakeVisible (basis);
    addAndMakeVisible (target);
    setTip (basis, "c:tb.mode");
    setTip (target, "c:tb.target");
    basis.setSelected (ctx.engine.tonalLast10s.load() ? 1 : 0, false);
    basis.onChange = [this] (int i) { ctx.engine.tonalLast10s = i == 1; };
    target.onClick = [this]
    {
        juce::PopupMenu m;
        const auto t = ctx.refs.target();
        m.addItem (1, "Default target (4.5 dB/oct slope)", true, t.count == 0);
        m.addItem (2, "Reference set (" + juce::String (t.count) + " tracks)", t.count > 0, t.count > 0);
        m.addItem (0, "Load references with the buttons 1-6 in the top bar.", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
    };
}

void TonalPanel::resized()
{
    auto head = getLocalBounds().removeFromTop (32).reduced (8, 4);
    target.setBounds (head.removeFromRight (164));
    head.removeFromRight (6);
    basis.setBounds (head.removeFromRight (156));
    rowsArea = getLocalBounds().withTrimmedTop (32).reduced (12, 6);
}

void TonalPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    paintPanelHeader (g, getLocalBounds(), tr ("Tonal balance"), tr ("vs target"));
    basis.setOptions ({ tr ("Integrated##tonal"), tr ("Last 10 s") });
    target.setButtonText (tr ("Target: ") + ctx.snap.targets.name);
    const auto& t = viewTap (ctx);
    const auto& tonal = basis.getSelected() == 1 ? t.tonalRecent : t.tonalIntegrated;
    const auto& tg = ctx.snap.targets;

    auto r = rowsArea;
    auto scale = r.removeFromTop (18);
    const int nameW = 76, valW = 66, lightW = 24;
    auto track = scale.withTrimmedLeft (nameW).withTrimmedRight (valW + lightW);
    const double range = 6.0;
    const auto xOf = [&] (double v) { return track.getX() + (float) ((v + range) / (2 * range)) * track.getWidth(); };
    g.setFont (font (12.0f));
    g.setColour (col::text3);
    for (double v : { -6.0, -3.0, 0.0, 3.0, 6.0 })
        g.drawText (v == 0 ? juce::String ("target") : fmtSigned (v, 0) + (std::abs (v) == 6 ? " dB" : ""), juce::Rectangle<float> (xOf (v) - 30, (float) scale.getY(), 60, 16), juce::Justification::centred);

    const int rowH = std::max (22, (r.getHeight() - 34) / analysis::numTonalBands);
    juce::StringArray sentence;
    for (int b = 0; b < analysis::numTonalBands; ++b)
    {
        auto row = r.removeFromTop (rowH);
        const double dev = t.hasSignal ? tonal[(size_t) b] - tg.tonal[(size_t) b] : 0.0;
        const int kind = ! t.hasSignal ? 3 : (std::abs (dev) <= 1.0 ? 0 : (std::abs (dev) <= 2.5 ? 1 : 2));
        const auto c = lightColour (kind);
        auto name = row.removeFromLeft (nameW);
        g.setColour (col::text);
        g.setFont (semibold (13.0f));
        g.drawText (analysis::tonalBandName (b), name.removeFromTop (name.getHeight() / 2 + 2), juce::Justification::bottomLeft);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (analysis::tonalBandRange (b))), name, juce::Justification::topLeft);

        auto lightR = row.removeFromRight (lightW);
        auto valR = row.removeFromRight (valW);
        const auto tr = juce::Rectangle<float> ((float) track.getX(), row.getCentreY() - 10.0f, (float) track.getWidth(), 20.0f);
        g.setColour (col::sunk);
        g.fillRoundedRectangle (tr, 4.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (tr, 4.0f, 1.0f);
        const double cor = tg.corridor[(size_t) b];
        g.setColour (col::blue.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float> (xOf (-cor), tr.getY() + 1, xOf (cor) - xOf (-cor), tr.getHeight() - 2));
        g.setColour (col::text.withAlpha (0.9f));
        g.fillRect (xOf (0) - 1, tr.getY() - 2, 2.0f, tr.getHeight() + 4);
        if (t.hasSignal)
        {
            const float a = xOf (std::min (0.0, juce::jlimit (-range, range, dev))), bb = xOf (std::max (0.0, juce::jlimit (-range, range, dev)));
            g.setColour (c.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float> (a, tr.getY() + 6, bb - a, tr.getHeight() - 12));
            g.setColour (c);
            g.fillRoundedRectangle (xOf (juce::jlimit (-range, range, dev)) - 2, tr.getY() + 3, 4.0f, tr.getHeight() - 6, 2.0f);
        }
        g.setColour (c);
        g.setFont (font (14.0f, true));
        g.drawText (t.hasSignal ? fmtSigned (dev) + " dB" : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), valR, juce::Justification::centredRight);
        drawLight (g, lightR.toFloat().withSizeKeepingCentre (17, 17).translated (4, 0), kind);
        if (t.hasSignal && std::abs (dev) > 1.0)
            sentence.add (analysis::tonalLabel (b, dev));
    }
    r.removeFromTop (6);
    auto sbox = r.removeFromTop (30).toFloat();
    g.setColour (col::panel2);
    g.fillRoundedRectangle (sbox, 6.0f);
    g.setColour (col::text);
    g.setFont (font (12.5f));
    g.drawFittedText (sentence.isEmpty() ? (t.hasSignal ? tr ("All six bands are within 1 dB of the target") : tr ("Play audio to measure")) : sentence.joinIntoString (juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "))),
                      sbox.reduced (10, 2).toNearestInt(), juce::Justification::centredLeft, 2);
}

void TonalPanel::mouseMove (const juce::MouseEvent& e)
{
    const int rowH = std::max (22, (rowsArea.getHeight() - 52) / analysis::numTonalBands);
    const int b = (e.y - rowsArea.getY() - 18) / std::max (1, rowH);
    if (b >= 0 && b < analysis::numTonalBands && b < (int) ctx.snap.checks.size())
        setTip (*this, "t:" + ctx.snap.checks[(size_t) b].tooltip);
    else
        setTip (*this, {});
}

// ---- Loudness ------------------------------------------------------------------------------------------
LoudnessPanel::LoudnessPanel (Context& c) : ctx (c)
{
    addAndMakeVisible (target);
    setTip (target, "c:ld.target");
    target.onClick = [this]
    {
        juce::PopupMenu m;
        auto t = ctx.engine.getTargets();
        m.addItem (1, "Streaming reference (-14 LUFS, -1 dBTP)", true, std::abs (t.integratedLufs + 14.0) < 0.01);
        for (int v : { -12, -10, -9, -8, -7 })
            m.addItem (100 - v, "Custom " + fmtMinus (v, 0) + " LUFS", true, std::abs (t.integratedLufs - v) < 0.01);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [this] (int r)
        {
            if (r <= 0)
                return;
            auto t2 = ctx.engine.getTargets();
            t2.integratedLufs = r == 1 ? -14.0 : -(double) (r - 100);
            ctx.engine.setTargets (t2);
            ctx.settings.set ("loudnessTarget", t2.integratedLufs);
        });
    };
}

void LoudnessPanel::resized()
{
    target.setBounds (getLocalBounds().removeFromTop (32).reduced (8, 4).removeFromRight (176));
}

void LoudnessPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    paintPanelHeader (g, getLocalBounds(), tr ("Loudness"), "BS.1770 / EBU R128");
    const auto& t = viewTap (ctx);
    const double tgt = ctx.snap.targets.integratedLufs;
    target.setButtonText (std::abs (tgt + 14.0) < 0.01 ? "Target: Streaming ref" : "Target: " + fmtMinus (tgt, 0) + " LUFS");
    auto r = getLocalBounds().withTrimmedTop (36).reduced (10, 4);

    // big values
    auto top = r.removeFromTop (70);
    const auto big = [&] (juce::Rectangle<int> cell, const juce::String& label, double v, bool main)
    {
        g.setColour (col::panel2);
        g.fillRoundedRectangle (cell.toFloat().reduced (2), 6.0f);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (label, cell.reduced (10, 4).removeFromTop (16), juce::Justification::left);
        g.setColour (col::text);
        g.setFont (font (main ? 34.0f : 22.0f, true));
        g.drawText (fmtLufs (v), cell.reduced (10, 4).withTrimmedTop (16).withTrimmedRight (40), juce::Justification::left);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText ("LUFS", cell.reduced (10, 4).withTrimmedTop (16), juce::Justification::bottomRight);
    };
    big (top.removeFromLeft (top.getWidth() * 2 / 5), tr ("Integrated##loudness"), t.integrated, true);
    big (top.removeFromLeft (top.getWidth() / 2), tr ("Short-term"), t.shortTerm, false);
    big (top, tr ("Momentary"), t.momentary, false);

    // statistics grid
    r.removeFromTop (6);
    const double ceiling = ctx.param ("lim_ceiling");
    struct Stat
    {
        juce::String label, value, unit;
        juce::Colour c;
    };
    const auto tpCol = t.truePeak <= ceiling ? col::ok : (t.truePeak <= ceiling + 0.5 ? col::warn : col::bad);
    const Stat stats[] = {
        { "LRA", juce::String (t.lra, 1), "LU", col::text },
        { "True peak", fmtLufs (t.truePeak), "dBTP", tpCol },
        { "PLR", juce::String (t.plr, 1), "LU", t.plr >= 8 ? col::text : (t.plr >= 6 ? col::warn : col::bad) },
        { "PSR", juce::String (t.psr, 1), "LU", col::text },
        { tr ("Sample peak"), fmtLufs (t.samplePeak), "dBFS", col::text },
        { tr ("Crest"), juce::String (t.crest, 1), "dB", col::text },
        { tr ("Overs"), juce::String ((juce::int64) t.sampleOvers), "", t.sampleOvers > 0 ? col::warn : col::text },
        { tr ("TP overs"), juce::String ((juce::int64) t.truePeakOvers), "", t.truePeakOvers > 0 ? col::warn : col::text },
    };
    const int cols = 4, sh = 46;
    for (int i = 0; i < 8; ++i)
    {
        const int cx = i % cols, cy = i / cols;
        const float w = (r.getWidth() - 6.0f * (cols - 1)) / cols;
        drawStatBox (g, juce::Rectangle<float> (r.getX() + cx * (w + 6), (float) r.getY() + cy * (sh + 6), w, (float) sh), stats[i].label, stats[i].value, stats[i].unit, stats[i].c);
    }
    r.removeFromTop (2 * sh + 14);

    // short-term history (last 60 s) with the information-only target line
    g.setColour (col::text3);
    g.setFont (font (12.0f));
    g.drawText (tr ("Short-term history"), r.removeFromTop (16), juce::Justification::left);
    auto hr = r.toFloat().withTrimmedRight (34).withTrimmedBottom (14);
    if (hr.getHeight() < 30)
        return;
    g.setColour (col::sunk);
    g.fillRoundedRectangle (hr, 6.0f);
    const float top0 = -4.0f, bot0 = -30.0f;
    const auto yOf = [&] (double v) { return hr.getY() + (float) ((top0 - v) / (top0 - bot0)) * hr.getHeight(); };
    g.setFont (font (12.0f));
    for (int v : { -8, -14, -20, -26 })
    {
        g.setColour (juce::Colour (0xff1c2128));
        g.drawHorizontalLine ((int) yOf (v), hr.getX(), hr.getRight());
        g.setColour (col::text2);
        g.drawText (fmtMinus (v, 0), juce::Rectangle<float> (hr.getRight() + 4, yOf (v) - 7, 30, 14), juce::Justification::left);
    }
    const float dash[] = { 4.0f, 4.0f };
    g.setColour (col::info);
    g.drawDashedLine (juce::Line<float> (hr.getX(), yOf (tgt), hr.getRight(), yOf (tgt)), dash, 2, 1.0f);
    g.drawText (fmtMinus (tgt, 0) + " LUFS reference (info only)", juce::Rectangle<float> (hr.getX() + 6, yOf (tgt) - 16, 260, 14), juce::Justification::left);
    if (t.history.size() > 1)
    {
        juce::Path p;
        const size_t n = t.history.size();
        for (size_t i = 0; i < n; ++i)
        {
            const float x = hr.getRight() - (float) (n - 1 - i) / 600.0f * hr.getWidth();
            const float y = juce::jlimit (hr.getY(), hr.getBottom(), yOf (std::max (-60.0f, t.history[i])));
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        g.setColour (col::orange);
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }
    g.setColour (col::text3);
    g.drawText ("-60 s", juce::Rectangle<float> (hr.getX(), hr.getBottom() + 1, 60, 13), juce::Justification::left);
    g.drawText ("now", juce::Rectangle<float> (hr.getRight() - 60, hr.getBottom() + 1, 60, 13), juce::Justification::right);
}

// ---- Checks --------------------------------------------------------------------------------------------
ChecksPanel::ChecksPanel (Context& c, bool comp) : ctx (c), compact (comp) {}

void ChecksPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    paintPanelHeader (g, getLocalBounds(), tr ("Checks"), compact ? juce::String() : tr ("independent checks, hover for advice"));
    listArea = getLocalBounds().withTrimmedTop (34).reduced (6, 2);
    const auto& checks = ctx.snap.checks;
    if (checks.empty())
        return;
    const int rowH = std::max (compact ? 18 : 30, listArea.getHeight() / (int) checks.size());
    auto r = listArea;
    for (size_t i = 0; i < checks.size(); ++i)
    {
        const auto& c = checks[i];
        auto row = r.removeFromTop (rowH);
        const int kind = lightKind (c.light);
        if ((int) i == hover)
        {
            g.setColour (juce::Colour (0xff2a1f25));
            g.fillRoundedRectangle (row.toFloat(), 6.0f);
        }
        if (! compact && i + 1 < checks.size())
        {
            g.setColour (col::line);
            g.drawHorizontalLine (row.getBottom() - 1, (float) row.getX() + 4, (float) row.getRight() - 4);
        }
        auto in = row.reduced (6, 1);
        drawLight (g, in.removeFromLeft (compact ? 16 : 20).toFloat().withSizeKeepingCentre (compact ? 15.0f : 18.0f, compact ? 15.0f : 18.0f).withY ((float) in.getY() + (compact ? 1.0f : 4.0f)), kind);
        in.removeFromLeft (8);
        g.setColour (lightColour (kind));
        g.setFont (font (13.0f, true));
        auto top = compact ? in : in.removeFromTop (in.getHeight() / 2 + 1);
        g.drawText (c.value, top, juce::Justification::centredRight);
        g.setColour (col::text);
        g.setFont (semibold (13.0f));
        g.drawText (tr (c.title + "##check"), top, juce::Justification::centredLeft);
        if (! compact)
        {
            g.setColour (col::text2);
            g.setFont (font (12.0f));
            g.drawText (tr (c.detail), in, juce::Justification::centredLeft);
        }
    }
}

void ChecksPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto& checks = ctx.snap.checks;
    const int rowH = checks.empty() ? 1 : std::max (compact ? 18 : 30, listArea.getHeight() / (int) checks.size());
    const int i = (e.y - listArea.getY()) / rowH;
    const int h = (i >= 0 && i < (int) checks.size() && listArea.contains (e.getPosition())) ? i : -1;
    if (h != hover)
    {
        hover = h;
        setTip (*this, h >= 0 ? "t:" + checks[(size_t) h].title + " " + checks[(size_t) h].value + "\n" + checks[(size_t) h].tooltip : juce::String());
        repaint();
    }
}

void ChecksPanel::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

// ---- Low end -------------------------------------------------------------------------------------------
LowEndPanel::LowEndPanel (Context& c, bool lg) : ctx (c), large (lg)
{
    if (! large)
    {
        addAndMakeVisible (open);
        setTip (open, "c:le.open");
        open.onClick = [this] { if (ctx.showTab) ctx.showTab ("Low-End"); };
    }
    else
    {
        addAndMakeVisible (channel);
        channel.setSelected (juce::jlimit (0, 3, ctx.engine.lowChannel.load()), false);
        channel.onChange = [this] (int i) { ctx.engine.lowChannel = i; };
        setTip (channel, "t:Low-end channel\nChooses which signal the large low-end spectrum shows: Left, Right, Mid or Side.");
    }
}

void LowEndPanel::resized()
{
    auto head = getLocalBounds().removeFromTop (32).reduced (8, 4);
    if (large)
        channel.setBounds (head.removeFromRight (170));
    else
        open.setBounds (head.removeFromRight (136));
}

void LowEndPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    const float mono = ctx.param ("lem_mono_freq");
    paintPanelHeader (g, getLocalBounds(), tr ("Low end"), juce::String (juce::CharPointer_UTF8 ("20\xe2\x80\x93" "200 Hz, 1/12 oct")));
    open.setButtonText (tr ("Open Low-End tab"));
    const auto& t = viewTap (ctx);
    auto r = getLocalBounds().withTrimmedTop (36).reduced (10, 4);
    auto stats = r.removeFromBottom (large ? 56 : 50);
    auto wave = r.removeFromBottom (large ? std::max (60, r.getHeight() / 3) : 52);
    r.removeFromBottom (6);

    // spectrum 20-200 Hz: Mid and Side (or the selected channel in the large view)
    auto plot = r.toFloat().withTrimmedRight (36).withTrimmedBottom (16);
    g.setColour (col::sunk);
    g.fillRoundedRectangle (plot, 6.0f);
    const auto xOf = [&] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (10.0)) * plot.getWidth(); };
    // vertical range from the data
    float top = -12.0f;
    for (const auto& v : { t.lowMid, t.lowSide, t.lowSpec })
        for (auto x : v)
            top = std::max (top, x + 3.0f);
    top = std::ceil (top / 6.0f) * 6.0f;
    const float bottom = top - 36.0f;
    const auto yOf = [&] (float db) { return plot.getY() + (top - db) / (top - bottom) * plot.getHeight(); };
    g.setFont (font (12.0f));
    for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 150.0, 200.0 })
    {
        g.setColour (juce::Colour (0xff1c2128));
        g.drawVerticalLine ((int) xOf (f), plot.getY(), plot.getBottom());
        g.setColour (col::text2);
        g.drawText (juce::String ((int) f), juce::Rectangle<float> (xOf (f) - 15, plot.getBottom() + 2, 30, 13), juce::Justification::centred);
    }
    for (float db = top - 6.0f; db > bottom; db -= 12.0f)
    {
        g.setColour (col::text2);
        g.drawText (fmtMinus (db, 0), juce::Rectangle<float> (plot.getRight() + 4, yOf (db) - 7, 32, 14), juce::Justification::left);
    }
    // mono-maker frequency marker
    if (ctx.param ("lem_on") >= 0.5f && ctx.param ("lem_mono_on") >= 0.5f)
    {
        const float dash[] = { 3.0f, 3.0f };
        g.setColour (col::blue);
        g.drawDashedLine (juce::Line<float> (xOf (mono), plot.getY(), xOf (mono), plot.getBottom()), dash, 2, 1.0f);
        g.drawText ("mono below " + juce::String (juce::roundToInt (mono)) + " Hz", juce::Rectangle<float> (xOf (mono) + 4, plot.getY() + 2, 130, 14), juce::Justification::left);
    }
    const auto draw = [&] (const std::vector<float>& v, juce::Colour c, bool fill)
    {
        if (v.size() != ctx.snap.lowFreqs.size() || v.empty())
            return;
        juce::Path p;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float x = xOf (ctx.snap.lowFreqs[i]), y = juce::jlimit (plot.getY(), plot.getBottom(), yOf (v[i]));
            i == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        if (fill)
        {
            auto f = p;
            f.lineTo (plot.getRight(), plot.getBottom());
            f.lineTo (plot.getX(), plot.getBottom());
            f.closeSubPath();
            g.setColour (c.withAlpha (0.12f));
            g.fillPath (f);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.6f));
    };
    if (large)
        draw (t.lowSpec, col::orange, true);
    else
    {
        draw (t.lowMid, col::orange, true);
        draw (t.lowSide, col::magenta, false);
    }
    // the two strongest peaks with note names
    const auto& v = large ? t.lowSpec : t.lowMid;
    if (v.size() == ctx.snap.lowFreqs.size() && ! v.empty())
    {
        std::vector<size_t> peaks;
        for (size_t i = 2; i + 2 < v.size(); ++i)
            if (v[i] >= v[i - 1] && v[i] >= v[i + 1] && v[i] >= v[i - 2] && v[i] >= v[i + 2])
                peaks.push_back (i);
        std::sort (peaks.begin(), peaks.end(), [&] (size_t a, size_t b) { return v[a] > v[b]; });
        for (size_t k = 0; k < std::min<size_t> (peaks.size(), large ? 4 : 2); ++k)
        {
            const auto i = peaks[k];
            const float x = xOf (ctx.snap.lowFreqs[i]), y = yOf (v[i]);
            g.setColour (juce::Colours::white);
            g.fillEllipse (x - 3, y - 3, 6, 6);
            g.setFont (font (12.0f, true));
            g.drawText (noteName (ctx.snap.lowFreqs[i]) + " " + juce::String (juce::roundToInt (ctx.snap.lowFreqs[i])) + " Hz", juce::Rectangle<float> (x - 50, y - 18, 100, 14), juce::Justification::centred);
        }
    }
    if (! large)
    {
        g.setFont (font (12.0f));
        g.setColour (col::orange);
        g.drawText ("Mid", juce::Rectangle<float> (plot.getX() + 8, plot.getY() + 4, 30, 14), juce::Justification::left);
        g.setColour (col::magenta);
        g.drawText ("Side", juce::Rectangle<float> (plot.getX() + 40, plot.getY() + 4, 36, 14), juce::Justification::left);
    }

    // low-band waveform, last 2 s
    auto wr = wave.toFloat();
    g.setColour (col::text3);
    g.setFont (font (12.0f));
    g.drawText (juce::String (juce::CharPointer_UTF8 ("Low band 20\xe2\x80\x93" "150 Hz, last 2 s")), wr.removeFromTop (15), juce::Justification::left);
    g.setColour (col::sunk);
    g.fillRoundedRectangle (wr, 6.0f);
    if (! t.lowWave.empty())
    {
        float peak = 1.0e-4f;
        for (auto s : t.lowWave)
            peak = std::max (peak, std::abs (s));
        const int cols = (int) wr.getWidth();
        const size_t per = std::max<size_t> (1, t.lowWave.size() / (size_t) std::max (1, cols));
        g.setColour (col::blue.withAlpha (0.8f));
        for (int x = 0; x < cols; ++x)
        {
            float mn = 0, mx = 0;
            for (size_t i = (size_t) x * per; i < std::min (t.lowWave.size(), (size_t) (x + 1) * per); ++i)
            {
                mn = std::min (mn, t.lowWave[i]);
                mx = std::max (mx, t.lowWave[i]);
            }
            const float cy = wr.getCentreY(), h = wr.getHeight() * 0.45f / peak;
            g.drawVerticalLine ((int) wr.getX() + x, cy - mx * h, cy - mn * h + 1);
        }
    }

    // readouts
    const auto lm = t.lowMono;
    const double subRef = ctx.refs.target().count > 0 ? ctx.refs.target().subToBass : std::numeric_limits<double>::quiet_NaN();
    const float w = (stats.getWidth() - 18.0f) / 4.0f;
    auto s = stats.toFloat().withTrimmedTop (6);
    drawStatBox (g, s.removeFromLeft (w), "Correlation <" + juce::String (juce::roundToInt (mono)), juce::String (lm.correlation, 2), "", lm.correlation >= 0.8 ? col::ok : (lm.correlation >= 0.5 ? col::warn : col::bad));
    s.removeFromLeft (6);
    drawStatBox (g, s.removeFromLeft (w), tr ("Mono loss") + " <" + juce::String (juce::roundToInt (mono)), fmtMinus (lm.monoLossDb), "dB", lm.monoLossDb >= -1 ? col::ok : col::warn);
    s.removeFromLeft (6);
    drawStatBox (g, s.removeFromLeft (w), "Sub / Bass", fmtSigned (std::isnan (subRef) ? t.subToBass : t.subToBass - subRef), std::isnan (subRef) ? "dB" : "dB vs ref", std::isnan (subRef) ? col::text : col::warn);
    s.removeFromLeft (6);
    drawStatBox (g, s, tr ("Peak asymmetry"), fmtSigned (t.asymmetry, 0), "%");
}

// ---- Stereo ---------------------------------------------------------------------------------------------
StereoPanel::StereoPanel (Context& c, bool lg) : ctx (c), large (lg) {}

void StereoPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    const auto& t = viewTap (ctx);
    paintPanelHeader (g, getLocalBounds(), tr ("Stereo##panel"));
    g.setColour (col::text3);
    g.setFont (font (12.5f));
    const auto bal = t.balanceDb;
    g.drawText ("Balance L/R  " + juce::String (std::abs (bal), 1) + " dB " + (bal >= 0 ? "R" : "L"), getLocalBounds().removeFromTop (32).reduced (12, 0), juce::Justification::centredRight);

    auto r = getLocalBounds().withTrimmedTop (36).reduced (10, 6);
    const float sz = std::min ((float) r.getHeight(), large ? r.getWidth() * 0.5f : r.getWidth() * 0.42f);
    drawVectorscope (g, r.removeFromLeft ((int) sz).toFloat().withHeight (sz), t.scope);
    r.removeFromLeft (12);

    g.setColour (col::text3);
    g.setFont (font (12.0f));
    auto corrHead = r.removeFromTop (16);
    g.drawText (tr ("Correlation"), corrHead, juce::Justification::left);
    g.setColour (col::text);
    g.setFont (font (13.0f, true));
    g.drawText (fmtSigned (t.broadShort.correlation, 2), corrHead, juce::Justification::right);
    drawCorrelation (g, r.removeFromTop (16).toFloat(), t.broadShort.correlation);
    g.setColour (col::text3);
    g.setFont (font (12.0f));
    auto scale = r.removeFromTop (14);
    g.drawText (fmtMinus (-1, 0), scale, juce::Justification::left);
    g.drawText ("0", scale, juce::Justification::centred);
    g.drawText ("+1", scale, juce::Justification::right);
    r.removeFromTop (6);

    // per-band table
    static const char* names[] = { "< 150 Hz", "150\xe2\x80\x93" "500", "0.5\xe2\x80\x93" "4 kHz", "> 4 kHz" };
    const int colW[] = { r.getWidth() - 3 * 62, 62, 62, 62 };
    auto head = r.removeFromTop (20);
    g.setColour (col::text3);
    const char* heads[] = { "Band", "Corr", "Width", "Mono loss" };
    for (int c = 0; c < 4; ++c)
        g.drawText (tr (heads[c]), head.removeFromLeft (colW[c]), c == 0 ? juce::Justification::left : juce::Justification::right);
    g.setColour (col::line);
    g.drawHorizontalLine (head.getBottom(), (float) r.getX(), (float) r.getRight());
    const int rowH = std::max (20, std::min (28, r.getHeight() / 4));
    for (int b = 0; b < analysis::numStereoBands; ++b)
    {
        const auto& s = t.bandsShort[(size_t) b];
        auto row = r.removeFromTop (rowH);
        g.setColour (col::text);
        g.setFont (font (12.5f, true));
        g.drawText (juce::String (juce::CharPointer_UTF8 (names[b])), row.removeFromLeft (colW[0]), juce::Justification::left);
        g.setColour (s.correlation >= 0.5 ? col::ok : (s.correlation >= 0.0 ? col::warn : col::bad));
        g.drawText (fmtSigned (s.correlation, 2), row.removeFromLeft (colW[1]), juce::Justification::right);
        g.setColour (col::text);
        g.drawText (juce::String (juce::roundToInt (s.widthPercent)) + "%", row.removeFromLeft (colW[2]), juce::Justification::right);
        g.setColour (s.monoLossDb >= -1.0 ? col::ok : (s.monoLossDb >= -3.0 ? col::warn : col::bad));
        g.drawText (fmtMinus (s.monoLossDb) + " dB", row.removeFromLeft (colW[3]), juce::Justification::right);
        g.setColour (col::line);
        g.drawHorizontalLine (row.getBottom(), (float) getX(), (float) getRight());
    }
}

// ---- Eyes view -------------------------------------------------------------------------------------------
EyesView::EyesView (Context& c) : ctx (c), tonal (c), loudness (c), checks (c, false), lowEnd (c, false), stereo (c, false)
{
    for (auto* p : std::initializer_list<juce::Component*> { &tonal, &loudness, &checks, &lowEnd, &stereo })
        addAndMakeVisible (p);
}

void EyesView::setMedium (bool m)
{
    medium = m;
    lowEnd.setVisible (! m);
    stereo.setVisible (! m);
    resized();
}

void EyesView::resized()
{
    auto r = getLocalBounds();
    const int gap = 8;
    if (medium)
    {
        auto right = r.removeFromRight (r.getWidth() * 30 / 100);
        checks.setBounds (right.withTrimmedLeft (gap));
        tonal.setBounds (r.removeFromTop (r.getHeight() / 2 - gap / 2));
        r.removeFromTop (gap);
        loudness.setBounds (r);
        return;
    }
    auto right = r.removeFromRight (r.getWidth() * 245 / 1000);
    checks.setBounds (right.withTrimmedLeft (gap));
    auto top = r.removeFromTop (r.getHeight() * 54 / 100);
    r.removeFromTop (gap);
    tonal.setBounds (top.removeFromLeft (top.getWidth() / 2 - gap / 2));
    top.removeFromLeft (gap);
    loudness.setBounds (top);
    lowEnd.setBounds (r.removeFromLeft (r.getWidth() / 2 - gap / 2));
    r.removeFromLeft (gap);
    stereo.setBounds (r);
}

void EyesView::update()
{
    for (auto* p : std::initializer_list<juce::Component*> { &tonal, &loudness, &checks, &lowEnd, &stereo })
        p->repaint();
}
} // namespace mm::ui
