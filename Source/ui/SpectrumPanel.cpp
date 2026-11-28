#include "ui/SpectrumPanel.h"
#include "ui/EqControls.h"
#include "ui/Locale.h"

#include "dsp/DynamicEq.h"
#include "reference/TrackAnalysis.h"
#include "state/Parameters.h"

namespace mm::ui
{
namespace
{
constexpr double fMin = 20.0, fMax = 20000.0;
constexpr float specTop = -6.0f, specBottom = -90.0f, eqRange = 18.0f;

juce::String noteName (double f)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const double n = 12.0 * std::log2 (f / 440.0) + 69.0;
    const int r = juce::roundToInt (n);
    const int cents = juce::roundToInt ((n - r) * 100.0);
    return juce::String (names[((r % 12) + 12) % 12]) + juce::String (r / 12 - 1) + " " + fmtSigned (cents, 0).replace ("+", "+") + " ct";
}

juce::String hz (double f) { return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : juce::String (f, 1) + " Hz"; }

/** Interpolates a curve given at the stored 1/24-octave grid. */
float atGrid (const std::vector<float>& v, double f)
{
    const auto& g = reference::spectrumFreqs();
    if (v.size() != g.size())
        return 0.0f;
    const double pos = std::log (f / g.front()) / std::log (g.back() / g.front()) * (double) (g.size() - 1);
    const int i = juce::jlimit (0, (int) g.size() - 2, (int) pos);
    const double t = juce::jlimit (0.0, 1.0, pos - i);
    return (float) (v[(size_t) i] * (1.0 - t) + v[(size_t) i + 1] * t);
}
} // namespace

// ==================================================================================================
class SpectrumPanel::Plot final : public juce::Component
{
public:
    explicit Plot (Context& c, bool comp) : ctx (c), compact (comp) { setRepaintsOnMouseActivity (false); }

    float xOf (double f) const { return (float) (std::log (f / fMin) / std::log (fMax / fMin)) * plotArea().getWidth() + plotArea().getX(); }
    double fOf (float x) const { return fMin * std::pow (fMax / fMin, (x - plotArea().getX()) / plotArea().getWidth()); }
    float ySpec (float db) const { auto p = plotArea(); return p.getY() + (specTop - db) / (specTop - specBottom) * p.getHeight(); }
    float yEq (double db) const { auto p = plotArea(); return p.getCentreY() - (float) db / eqRange * p.getHeight() * 0.5f; }
    double eqOfY (float y) const { auto p = plotArea(); return (p.getCentreY() - y) / (p.getHeight() * 0.5f) * eqRange; }

    juce::Rectangle<float> plotArea() const
    {
        auto r = getLocalBounds().toFloat();
        r.removeFromLeft (compact ? 4.0f : 34.0f);
        r.removeFromRight (compact ? 30.0f : 36.0f);
        r.removeFromBottom (compact ? 16.0f : 34.0f);
        r.removeFromTop (4.0f);
        return r;
    }

    void update()
    {
        eqSettings = ctx.proc.getChain().eqSnapshot();
        for (int b = 0; b < 10; ++b)
            dynGains[(size_t) b] = ctx.proc.getChain().getEq().effectiveGainDb (b);
        if (++normCounter >= 15 || ourNorm.empty())
        {
            normCounter = 0;
            ourNorm = ctx.engine.outSpectrumNormalised();
        }
        target = ctx.refs.target();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto p = plotArea();
        g.setColour (col::sunk);
        g.fillRect (p);

        // grid and labels
        g.setFont (font (12.0f));
        for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 80.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 800.0, 1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 8000.0, 10000.0, 20000.0 })
        {
            const float x = xOf (f);
            const bool major = f == 20.0 || f == 50.0 || f == 100.0 || f == 200.0 || f == 500.0 || f == 1000.0 || f == 2000.0 || f == 5000.0 || f == 10000.0 || f == 20000.0;
            g.setColour (major ? juce::Colour (0xff20252d) : juce::Colour (0xff181c22));
            g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
            if (major && ! compact)
            {
                g.setColour (col::text2);
                const auto label = f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f);
                g.drawText (label, juce::Rectangle<float> (x - 20, p.getBottom() + 2, 40, 14), f == 20.0 ? juce::Justification::left : (f == 20000.0 ? juce::Justification::right : juce::Justification::centred));
            }
            else if (major && compact && (f == 20.0 || f == 100.0 || f == 1000.0 || f == 10000.0))
            {
                g.setColour (col::text2);
                g.drawText (f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + "k" : juce::String ((int) f), juce::Rectangle<float> (x - 18, p.getBottom() + 1, 36, 14), juce::Justification::centred);
            }
        }
        const bool deltaView = ctx.delta && target.count > 0;
        for (int db = -84; db <= -12; db += 12)
        {
            const float y = ySpec ((float) db);
            g.setColour (juce::Colour (0xff1c2128));
            g.drawHorizontalLine ((int) y, p.getX(), p.getRight());
            if (! deltaView)
            {
                g.setColour (col::text2);
                g.drawText (fmtMinus (db, 0), juce::Rectangle<float> (p.getRight() + 3, y - 7, 32, 14), juce::Justification::left);
            }
        }
        if (! compact)
            for (int db = -12; db <= 12; db += 6)
            {
                g.setColour (col::text2.withAlpha (db == 0 ? 1.0f : 0.8f));
                g.drawText (db == 0 ? juce::String ("0") : fmtSigned (db, 0), juce::Rectangle<float> (0, yEq (db) - 7, 30, 14), juce::Justification::right);
                if (deltaView)
                    g.drawText (db == 0 ? juce::String ("0") : fmtSigned (db, 0), juce::Rectangle<float> (p.getRight() + 3, yEq (db) - 7, 32, 14), juce::Justification::left);
            }

        g.saveState();
        g.reduceClipRegion (p.toNearestInt());
        const auto& snap = ctx.snap;
        const auto curve = [&] (const std::vector<float>& v, auto&& yf)
        {
            juce::Path path;
            for (size_t i = 0; i < v.size() && i < snap.freqs.size(); ++i)
            {
                const float x = xOf (snap.freqs[i]), y = juce::jlimit (p.getY() - 20.0f, p.getBottom() + 20.0f, yf (v[i]));
                if (i == 0)
                    path.startNewSubPath (x, y);
                else
                    path.lineTo (x, y);
            }
            return path;
        };
        const auto pick = [&] (const analysis::TapView& t) -> const std::vector<float>&
        {
            return ctx.spectrumMode == 1 ? t.specAvg : (ctx.spectrumMode == 2 ? t.specPeak : t.specRt);
        };
        const float tiltNow = ctx.engine.tilt.load();
        const auto tiltTerm = [&] (double f) { return (float) ((tiltNow - 3.0103) * std::log2 (f / 1000.0)); };

        if (! deltaView)
        {
            // reference corridor and target, aligned to our level
            if (target.count > 0 && ! ourNorm.empty() && ! snap.out.specAvg.empty())
            {
                double off = 0.0;
                int n = 0;
                for (size_t i = 0; i < snap.freqs.size(); ++i)
                    if (snap.freqs[i] >= 100.0 && snap.freqs[i] <= 10000.0)
                    {
                        off += snap.out.specAvg[i] - (atGrid (ourNorm, snap.freqs[i]) + tiltTerm (snap.freqs[i]));
                        ++n;
                    }
                off /= std::max (1, n);
                juce::Path band, mid;
                std::vector<juce::Point<float>> lows;
                for (size_t i = 0; i < snap.freqs.size(); i += 2)
                {
                    const double f = snap.freqs[i];
                    const float x = xOf (f), t = tiltTerm (f) + (float) off;
                    const float hi = ySpec (atGrid (target.high, f) + t), lo = ySpec (atGrid (target.low, f) + t), m = ySpec (atGrid (target.spectrum, f) + t);
                    if (i == 0)
                    {
                        band.startNewSubPath (x, hi);
                        mid.startNewSubPath (x, m);
                    }
                    else
                    {
                        band.lineTo (x, hi);
                        mid.lineTo (x, m);
                    }
                    lows.push_back ({ x, lo });
                }
                for (auto it = lows.rbegin(); it != lows.rend(); ++it)
                    band.lineTo (*it);
                band.closeSubPath();
                g.setColour (col::blue.withAlpha (0.22f));
                g.fillPath (band);
                juce::Path dashed;
                const float dashes[] = { 5.0f, 4.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, mid, dashes, 2);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.fillPath (dashed);
            }
            const auto specY = [&] (float v) { return ySpec (v); };
            if ((ctx.tap == 0 || ctx.tap == 2) && ! pick (snap.in).empty())
            {
                g.setColour (juce::Colour (0xff5f86b8).withAlpha (0.85f));
                g.strokePath (curve (pick (snap.in), specY), juce::PathStrokeType (1.4f));
            }
            if ((ctx.tap == 1 || ctx.tap == 2) && ! pick (snap.out).empty())
            {
                if (ctx.spectrumMode == 0 && ! snap.out.specPeak.empty())
                {
                    g.setColour (col::orange.withAlpha (0.35f));
                    g.strokePath (curve (snap.out.specPeak, specY), juce::PathStrokeType (1.0f));
                }
                auto path = curve (pick (snap.out), specY);
                auto fill = path;
                fill.lineTo (p.getRight(), p.getBottom());
                fill.lineTo (p.getX(), p.getBottom());
                fill.closeSubPath();
                g.setColour (col::orange.withAlpha (0.07f));
                g.fillPath (fill);
                g.setColour (col::orange);
                g.strokePath (path, juce::PathStrokeType (1.8f));
            }
        }
        else
        {
            // delta view: our average minus the target, corridor around zero (spec 7.2)
            juce::Path band, d;
            std::vector<juce::Point<float>> lows;
            for (size_t i = 0; i < reference::spectrumFreqs().size(); ++i)
            {
                const double f = reference::spectrumFreqs()[i];
                const float x = xOf (f);
                const float hi = yEq (target.high[i] - target.spectrum[i]), lo = yEq (target.low[i] - target.spectrum[i]);
                const float v = ourNorm.size() == target.spectrum.size() ? yEq (ourNorm[i] - target.spectrum[i]) : yEq (0);
                if (i == 0)
                {
                    band.startNewSubPath (x, hi);
                    d.startNewSubPath (x, v);
                }
                else
                {
                    band.lineTo (x, hi);
                    d.lineTo (x, v);
                }
                lows.push_back ({ x, lo });
            }
            for (auto it = lows.rbegin(); it != lows.rend(); ++it)
                band.lineTo (*it);
            band.closeSubPath();
            g.setColour (col::blue.withAlpha (0.22f));
            g.fillPath (band);
            g.setColour (col::text3);
            g.drawHorizontalLine ((int) yEq (0), p.getX(), p.getRight());
            g.setColour (col::orange);
            g.strokePath (d, juce::PathStrokeType (2.0f));
        }

        // Match EQ preview
        if (! previewEq.empty())
        {
            juce::Path m;
            for (int i = 0; i <= 300; ++i)
            {
                const double f = fMin * std::pow (fMax / fMin, i / 300.0);
                const float y = yEq (reference::matchEqResponseDb (previewEq, ctx.snap.sampleRate, f));
                i == 0 ? m.startNewSubPath (xOf (f), y) : m.lineTo (xOf (f), y);
            }
            juce::Path dashed;
            const float dashes[] = { 6.0f, 4.0f };
            juce::PathStrokeType (2.0f).createDashedStroke (dashed, m, dashes, 2);
            g.setColour (col::magenta);
            g.fillPath (dashed);
        }

        // EQ curve (+ subsonic filter) with band nodes
        const bool eqOn = ctx.param ("eq_on") >= 0.5f;
        const bool hpfOn = ctx.param ("hpf_on") >= 0.5f && ctx.param ("in_on") >= 0.5f;
        if (eqOn || hpfOn)
        {
            juce::Path eq;
            const double fs = ctx.snap.sampleRate > 0 ? ctx.snap.sampleRate : 48000.0;
            for (int i = 0; i <= 400; ++i)
            {
                const double f = fMin * std::pow (fMax / fMin, i / 400.0);
                double db = eqOn ? dsp::DynamicEq::responseDb (eqSettings, fs, f, dynGains.data()) : 0.0;
                if (hpfOn)
                    db += ctx.proc.getChain().getInput().hpfMagnitudeDb (f);
                const float y = juce::jlimit (p.getY() - 4, p.getBottom() + 4, yEq (db));
                i == 0 ? eq.startNewSubPath (xOf (f), y) : eq.lineTo (xOf (f), y);
            }
            auto fill = eq;
            fill.lineTo (p.getRight(), yEq (0));
            fill.lineTo (p.getX(), yEq (0));
            fill.closeSubPath();
            g.setColour (col::yellow.withAlpha (0.08f));
            g.fillPath (fill);
            g.setColour (col::yellow);
            g.strokePath (eq, juce::PathStrokeType (2.2f));
        }
        if (eqOn && ! compact)
        {
            // the selected band's own curve
            const int sb = ctx.selectedEqBand - 1;
            if (sb >= 0 && sb < 10 && eqSettings.bands[(size_t) sb].on)
            {
                juce::Path own;
                const auto& band = eqSettings.bands[(size_t) sb];
                for (int i = 0; i <= 240; ++i)
                {
                    const double f = fMin * std::pow (fMax / fMin, i / 240.0);
                    const float y = juce::jlimit (p.getY() - 4, p.getBottom() + 4, yEq (dsp::DynamicEq::bandResponseDb (band, sb, ctx.snap.sampleRate > 0 ? ctx.snap.sampleRate : 48000.0, f, band.gainDb)));
                    i == 0 ? own.startNewSubPath (xOf (f), y) : own.lineTo (xOf (f), y);
                }
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.strokePath (own, juce::PathStrokeType (1.2f));
            }
            for (int b = 0; b < 10; ++b)
            {
                if (! eqedit::bandUsed (ctx, b + 1))
                    continue;
                const auto& band = eqSettings.bands[(size_t) b];
                const auto pos = nodePos (b);
                const bool sel = ctx.selectedEqBand == b + 1;
                const float r = sel ? 10.0f : 8.0f;
                if (sel && band.on && band.dynamic)
                {
                    // dynamic range of the selected band (dashed)
                    juce::Path dr;
                    const double g2 = band.mode == 0 ? band.gainDb - band.rangeDb : band.gainDb + band.rangeDb;
                    auto alt = band;
                    alt.gainDb = (float) g2;
                    for (int i = 0; i <= 120; ++i)
                    {
                        const double f = band.freq * std::pow (2.0, (i - 60) / 30.0);
                        const float y = yEq (dsp::DynamicEq::bandResponseDb (alt, b, ctx.snap.sampleRate > 0 ? ctx.snap.sampleRate : 48000.0, f, alt.gainDb));
                        i == 0 ? dr.startNewSubPath (xOf (f), y) : dr.lineTo (xOf (f), y);
                    }
                    juce::Path dashed;
                    const float dashes[] = { 4.0f, 4.0f };
                    juce::PathStrokeType (1.2f).createDashedStroke (dashed, dr, dashes, 2);
                    g.setColour (col::yellow.withAlpha (0.6f));
                    g.fillPath (dashed);
                }
                const auto ring = juce::Rectangle<float> (pos.x - r, pos.y - r, 2 * r, 2 * r);
                if (sel)
                {
                    g.setColour (col::yellow.withAlpha (0.18f));
                    g.fillEllipse (ring.expanded (5.0f));
                }
                const auto stroke = band.on ? col::yellow : juce::Colour (0xff7d7250);
                g.setColour (sel && band.on ? col::yellow : juce::Colour (0xff1b1d21));
                g.fillEllipse (ring);
                g.setColour (stroke);
                if (band.on)
                    g.drawEllipse (ring, 2.0f);
                else
                {
                    juce::Path circle, dashed;
                    circle.addEllipse (ring);
                    const float dashes[] = { 3.0f, 2.0f };
                    juce::PathStrokeType (2.0f).createDashedStroke (dashed, circle, dashes, 2);
                    g.fillPath (dashed);
                }
                g.setColour (sel && band.on ? juce::Colour (0xff1b1600) : stroke);
                g.setFont (font (12.0f, true));
                g.drawText (juce::String (b + 1), ring, juce::Justification::centred);
            }
            g.setColour (col::text3);
            g.setFont (font (11.5f));
            g.drawText (tr ("Double-click: add a band") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + tr ("at the edges: low cut / high cut"),
                        p.reduced (8, 5), juce::Justification::bottomRight, true);
        }
        else if (! compact && ! eqOn)
        {
            g.setColour (col::text3);
            g.setFont (font (11.5f));
            g.drawText (tr ("Double-click: add a band") + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + tr ("at the edges: low cut / high cut"),
                        p.reduced (8, 5), juce::Justification::bottomRight, true);
        }

        // cursor readout with the nearest note for 20-500 Hz (spec 5.1)
        if (cursor.has_value() && p.contains (*cursor))
        {
            const double f = fOf (cursor->x);
            g.setColour (col::text.withAlpha (0.45f));
            const float dashes[] = { 3.0f, 3.0f };
            g.drawDashedLine (juce::Line<float> (cursor->x, p.getY(), cursor->x, p.getBottom()), dashes, 2, 1.0f);
            float level = 0.0f;
            const auto& v = pick (snap.out);
            if (! v.empty())
            {
                const double pos = std::log (f / fMin) / std::log (fMax / fMin) * (double) (v.size() - 1);
                level = v[(size_t) juce::jlimit (0, (int) v.size() - 1, juce::roundToInt (pos))];
                g.setColour (juce::Colours::white);
                g.fillEllipse (cursor->x - 4, ySpec (level) - 4, 8, 8);
            }
            const auto t1 = hz (f) + (f <= 500.0 ? juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + noteName (f) : juce::String());
            const auto t2 = fmtMinus (level) + " dB (out)";
            juce::Rectangle<float> box (cursor->x + 10, std::max (p.getY() + 4, ySpec (level) - 46), 160, 38);
            if (box.getRight() > p.getRight())
                box.setX (cursor->x - 170);
            g.setColour (col::tipBg);
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (col::tipText);
            g.setFont (font (12.0f, true));
            g.drawText (t1, box.reduced (8, 4).removeFromTop (15), juce::Justification::left);
            g.setColour (juce::Colour (0xff3b414c));
            g.setFont (font (12.0f));
            g.drawText (t2, box.reduced (8, 4).withTrimmedTop (16), juce::Justification::left);
        }
        g.restoreState();

        // tonal band strip
        if (! compact)
        {
            const float y = p.getBottom() + 17;
            for (int b = 0; b < analysis::numTonalBands; ++b)
            {
                const float x0 = xOf (analysis::tonalEdges[b]) + 1, x1 = xOf (analysis::tonalEdges[b + 1]) - 1;
                g.setColour (col::panel2);
                g.fillRect (juce::Rectangle<float> (x0, y, x1 - x0, 15));
                g.setColour (col::text2);
                g.setFont (font (12.0f));
                g.drawText (analysis::tonalBandName (b), juce::Rectangle<float> (x0, y, x1 - x0, 15), juce::Justification::centred);
            }
        }
    }

    juce::Point<float> nodePos (int b) const
    {
        const auto& band = eqSettings.bands[(size_t) b];
        const auto kind = dsp::DynamicEq::kindOf (b, band.type);
        float y;
        if (kind == dsp::EqKind::bell)
            y = yEq (band.on ? dynGains[(size_t) b] : band.gainDb);
        else if (kind == dsp::EqKind::lowShelf || kind == dsp::EqKind::highShelf)
            y = yEq ((band.on ? dynGains[(size_t) b] : band.gainDb) * 0.5);
        else
            y = yEq (0.0); // cuts and notches sit on the 0 dB line (mockup v8)
        const auto pa = plotArea();
        return { juce::jlimit (pa.getX() + 9, pa.getRight() - 9, xOf (band.freq)), juce::jlimit (pa.getY() + 10, pa.getBottom() - 10, y) };
    }

    int nodeAt (juce::Point<float> pt) const
    {
        if (compact)
            return -1;
        for (int b = 9; b >= 0; --b)
            if (eqedit::bandUsed (ctx, b + 1) && nodePos (b).getDistanceFrom (pt) < 12.0f)
                return b;
        return -1;
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        cursor = e.position;
        const int n = nodeAt (e.position);
        const auto pa = plotArea();
        const bool overHint = ! compact && e.position.y > pa.getBottom() - 22.0f && e.position.x > pa.getRight() - 340.0f;
        setTip (*this, n >= 0 ? "c:eq.node:" + juce::String (n + 1) : (overHint ? juce::String ("c:eq.add") : juce::String()));
        setMouseCursor (n >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        cursor.reset();
        repaint();
    }

    juce::String id (int b, const char* field) const { return params::eq (b + 1, field); }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBand = nodeAt (e.position);
        if (dragBand < 0)
            return;
        ctx.selectedEqBand = dragBand + 1;
        ctx.selectedModule = 1;
        if (e.mods.isAltDown())
        {
            eqedit::deleteBand (ctx, dragBand + 1);
            dragBand = -1;
            return;
        }
        if (e.mods.isPopupMenu())
        {
            showNodeMenu (dragBand);
            dragBand = -1;
            return;
        }
        startPos = e.position;
        startFreq = ctx.param (id (dragBand, "freq"));
        startGain = ctx.param (id (dragBand, "gain"));
        for (auto* f : { "freq", "gain" })
            ctx.apvts.getParameter (id (dragBand, f))->beginChangeGesture();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        cursor = e.position;
        if (dragBand < 0)
            return;
        const float k = e.mods.isShiftDown() ? 0.15f : 1.0f; // Shift: fine
        const auto pa = plotArea();
        double f = startFreq * std::pow (fMax / fMin, (e.position.x - startPos.x) * k / pa.getWidth());
        f = juce::jlimit (20.0, 20000.0, ctx.eqSnap ? eqedit::snapToNote (f) : f);
        auto set = [&] (const char* field, float v)
        {
            auto* p = ctx.apvts.getParameter (id (dragBand, field));
            p->setValueNotifyingHost (p->convertTo0to1 (v));
        };
        set ("freq", (float) f);
        const auto kind = dsp::DynamicEq::kindOf (dragBand, eqSettings.bands[(size_t) dragBand].type);
        const double dg = (eqOfY (e.position.y) - eqOfY (startPos.y)) * k;
        if (kind == dsp::EqKind::bell)
            set ("gain", (float) juce::jlimit (-18.0, 18.0, startGain + dg));
        else if (kind == dsp::EqKind::lowShelf || kind == dsp::EqKind::highShelf)
            set ("gain", (float) juce::jlimit (-18.0, 18.0, startGain + dg * 2.0));
        update();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0)
            for (auto* f : { "freq", "gain" })
                ctx.apvts.getParameter (id (dragBand, f))->endChangeGesture();
        dragBand = -1;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (compact)
            return;
        const int n = nodeAt (e.position);
        if (n >= 0)
        {
            // double-click on a point: band on/off (bypass), the settings stay
            ctx.setParam (id (n, "on"), ctx.param (id (n, "on")) >= 0.5f ? 0.0f : 1.0f);
            return;
        }
        if (plotArea().contains (e.position))
            eqedit::addBandAt (ctx, fOf (e.position.x), eqOfY (e.position.y));
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = nodeAt (e.position);
        if (b < 0)
            return;
        const float dir = (w.deltaY > 0 ? 1.0f : -1.0f) * (w.isReversed ? -1.0f : 1.0f);
        if (eqedit::isCut (ctx, b + 1))
        {
            // cuts: the wheel changes the slope
            const int s = juce::jlimit (0, 5, juce::roundToInt (ctx.param (id (b, "slope"))) + (dir > 0 ? 1 : -1));
            ctx.setParam (id (b, "slope"), (float) s);
            return;
        }
        auto* p = ctx.apvts.getParameter (id (b, "q"));
        const float step = dir * (e.mods.isShiftDown() ? 0.005f : 0.03f);
        p->beginChangeGesture();
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, p->getValue() + step));
        p->endChangeGesture();
    }

    void showNodeMenu (int b)
    {
        juce::PopupMenu m;
        const auto band = b + 1;
        const auto* typeSpec = params::find (params::eq (band, "type"));
        juce::PopupMenu types;
        for (int i = 0; i < typeSpec->choices.size(); ++i)
            types.addItem (100 + i, typeSpec->choices[i], true, juce::roundToInt (ctx.param (params::eq (band, "type"))) == i);
        m.addSubMenu ("Type", types);
        juce::PopupMenu place;
        for (int i = 0; i < params::choice::placement.size(); ++i)
            place.addItem (200 + i, params::choice::placement[i], true, juce::roundToInt (ctx.param (params::eq (band, "place"))) == i);
        m.addSubMenu ("Placement", place);
        if (eqedit::isCut (ctx, band))
        {
            juce::PopupMenu slopes;
            const auto* slopeSpec = params::find (params::eq (band, "slope"));
            for (int i = 0; i < slopeSpec->choices.size(); ++i)
                slopes.addItem (400 + i, slopeSpec->choices[i], true, juce::roundToInt (ctx.param (params::eq (band, "slope"))) == i);
            m.addSubMenu ("Slope", slopes);
        }
        else
            m.addItem (300, "Dynamic", true, ctx.param (params::eq (band, "dyn")) >= 0.5f);
        m.addSeparator();
        m.addItem (303, "Snap to notes", true, ctx.eqSnap);
        const double nf = eqedit::snapToNote (ctx.param (params::eq (band, "freq")));
        m.addItem (304, "Move to nearest note: " + eqedit::noteText (nf).upToFirstOccurrenceOf (" ", false, false) + " (" + eqedit::freqText (nf) + ")");
        m.addSeparator();
        m.addItem (302, "Bypass band (double-click)", true, ctx.param (params::eq (band, "on")) < 0.5f);
        m.addItem (305, "Delete band (Alt+click)");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, band, nf] (int r)
        {
            if (r >= 100 && r < 200)
                ctx.setParam (params::eq (band, "type"), (float) (r - 100));
            else if (r >= 200 && r < 300)
                ctx.setParam (params::eq (band, "place"), (float) (r - 200));
            else if (r >= 400 && r < 500)
                ctx.setParam (params::eq (band, "slope"), (float) (r - 400));
            else if (r == 300)
                ctx.setParam (params::eq (band, "dyn"), ctx.param (params::eq (band, "dyn")) >= 0.5f ? 0.0f : 1.0f);
            else if (r == 302)
                ctx.setParam (params::eq (band, "on"), ctx.param (params::eq (band, "on")) >= 0.5f ? 0.0f : 1.0f);
            else if (r == 303)
                ctx.eqSnap = ! ctx.eqSnap;
            else if (r == 304)
                ctx.setParam (params::eq (band, "freq"), (float) nf);
            else if (r == 305)
                eqedit::deleteBand (ctx, band);
        });
    }

    Context& ctx;
    bool compact;
    dsp::DynamicEq::Settings eqSettings;
    std::array<float, 10> dynGains {};
    std::vector<float> ourNorm;
    reference::ReferenceTarget target;
    std::vector<reference::EqSuggestion> previewEq;
    std::optional<juce::Point<float>> cursor;
    int dragBand = -1, normCounter = 0;
    juce::Point<float> startPos;
    double startFreq = 1000.0, startGain = 0.0;
};

// ==================================================================================================
class SpectrumPanel::MatchEqPopup final : public juce::Component
{
public:
    MatchEqPopup (Context& c, Plot& p) : ctx (c), plot (p)
    {
        for (auto* b : { &suggest, &apply, &undo, &close })
            addAndMakeVisible (*b);
        addAndMakeVisible (strength);
        strength.setRange (0.0, 100.0, 1.0);
        strength.setValue (100.0);
        strength.setSliderStyle (juce::Slider::LinearHorizontal);
        strength.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
        strength.setTextValueSuffix ("%");
        strength.setColour (juce::Slider::trackColourId, col::magenta);
        strength.onValueChange = [this] { if (! plot.previewEq.empty()) compute(); };
        suggest.onClick = [this] { compute(); };
        apply.onClick = [this]
        {
            if (plot.previewEq.empty())
                return;
            previous = reference::applyMatchEq (ctx.apvts, plot.previewEq);
            plot.previewEq.clear();
            status = "Applied to Dynamic EQ. Undo restores the previous bands.";
            repaint();
        };
        undo.onClick = [this]
        {
            if (previous.empty())
                return;
            reference::undoMatchEq (ctx.apvts, previous);
            previous.clear();
            status = "Restored.";
            repaint();
        };
        close.onClick = [this]
        {
            plot.previewEq.clear();
            setVisible (false);
        };
    }

    void compute()
    {
        const auto target = ctx.refs.target();
        if (target.count == 0)
        {
            status = "Load references (top bar 1-6) to get a target.";
            plot.previewEq.clear();
        }
        else
        {
            plot.previewEq = reference::suggestMatchEq (ctx.engine.outSpectrumNormalised(), target.spectrum, strength.getValue() / 100.0);
            status = juce::String ((int) plot.previewEq.size()) + " filters proposed (dashed curve). Nothing changes until Apply.";
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), col::panel2, 8.0f);
        g.setColour (col::text);
        g.setFont (semibold (14.0f));
        g.drawText ("Match EQ", 12, 8, 200, 18, juce::Justification::left);
        g.setColour (col::text2);
        g.setFont (font (12.0f));
        g.drawFittedText (status, 12, getHeight() - 44, getWidth() - 24, 36, juce::Justification::topLeft, 2);
        g.drawText ("Strength", 12, 34, 60, 20, juce::Justification::left);
    }

    void resized() override
    {
        strength.setBounds (74, 34, getWidth() - 86, 20);
        auto row = juce::Rectangle<int> (12, 62, getWidth() - 24, 26);
        suggest.setBounds (row.removeFromLeft (80));
        row.removeFromLeft (6);
        apply.setBounds (row.removeFromLeft (64));
        row.removeFromLeft (6);
        undo.setBounds (row.removeFromLeft (56));
        close.setBounds (getWidth() - 30, 6, 24, 22);
    }

    Context& ctx;
    Plot& plot;
    juce::Slider strength;
    Btn suggest { "Suggest" }, apply { "Apply" }, undo { "Undo" }, close { "x" };
    juce::String status = "Proposes up to 8 gentle filters (+-3 dB) towards the reference target.";
    std::map<juce::String, float> previous;
};

// ==================================================================================================
SpectrumPanel::SpectrumPanel (Context& c, bool comp) : ctx (c), compact (comp)
{
    plot = std::make_unique<Plot> (ctx, compact);
    addAndMakeVisible (*plot);
    if (! compact)
    {
        bandPanel = std::make_unique<BandFields> (ctx, true);
        addChildComponent (*bandPanel);
    }
    if (compact)
    {
        addAndMakeVisible (modeCompact);
        addAndMakeVisible (smoothing);
        setTip (modeCompact, "c:sp.mode");
        setTip (smoothing, "c:sp.smooth");
        modeCompact.onChange = [this] (int i) { ctx.spectrumMode = i; };
    }
    else
    {
        for (juce::Component* w : { (juce::Component*) &mode, (juce::Component*) &fft, (juce::Component*) &smoothing, (juce::Component*) &tilt,
                                    (juce::Component*) &channel, (juce::Component*) &view })
            addAndMakeVisible (*w);
        setTip (mode, "c:sp.mode");
        setTip (fft, "c:sp.fft");
        setTip (smoothing, "c:sp.smooth");
        setTip (tilt, "c:sp.tilt");
        setTip (channel, "c:sp.chan");
        setTip (view, "c:sp.view");
        mode.onChange = [this] (int i) { ctx.spectrumMode = i; };
        view.onChange = [this] (int i) { ctx.delta = i == 1; };
        fft.addItemList ({ "FFT 4096", "FFT 8192", "FFT 16384" }, 1);
        tilt.addItemList ({ "Tilt 0 dB/oct", "Tilt 3 dB/oct", "Tilt 4.5 dB/oct" }, 1);
        channel.addItemList ({ "Left", "Right", "Mid", "Side", "Stereo" }, 1);
        fft.setSelectedId (ctx.engine.fftOrder.load() - 11, juce::dontSendNotification);
        tilt.setSelectedId (ctx.engine.tilt.load() < 1.0f ? 1 : (ctx.engine.tilt.load() < 4.0f ? 2 : 3), juce::dontSendNotification);
        channel.setSelectedId (ctx.engine.channel.load() + 1, juce::dontSendNotification);
        fft.onChange = [this] { applySettings(); };
        tilt.onChange = [this] { applySettings(); };
        channel.onChange = [this] { applySettings(); };
        // the Match EQ button was removed (mockup v18): Auto match sets the EQ from the references
    }
    smoothing.addItemList ({ compact ? "None" : "No smoothing", "1/12 oct", "1/6 oct", "1/3 oct", "1 oct" }, 1);
    smoothing.setSelectedId (ctx.engine.smoothing.load() + 1, juce::dontSendNotification);
    smoothing.onChange = [this] { applySettings(); };
}

SpectrumPanel::~SpectrumPanel() = default;

void SpectrumPanel::applySettings()
{
    if (! compact)
    {
        ctx.engine.fftOrder = fft.getSelectedId() + 11;
        static const float tilts[] = { 0.0f, 3.0f, 4.5f };
        ctx.engine.tilt = tilts[juce::jlimit (0, 2, tilt.getSelectedId() - 1)];
        ctx.engine.channel = channel.getSelectedId() - 1;
    }
    ctx.engine.smoothing = smoothing.getSelectedId() - 1;
    auto* o = new juce::DynamicObject();
    o->setProperty ("fftOrder", ctx.engine.fftOrder.load());
    o->setProperty ("smoothing", ctx.engine.smoothing.load());
    o->setProperty ("tilt", ctx.engine.tilt.load());
    o->setProperty ("channel", ctx.engine.channel.load());
    ctx.settings.set ("analysis", juce::var (o));
}

void SpectrumPanel::update()
{
    plot->update();
    placeBandPanel();
    mode.setSelected (ctx.spectrumMode, false);
    view.setSelected (ctx.delta ? 1 : 0, false);
    view.setEnabled (ctx.refs.target().count > 0);
    if (ctx.refs.target().count == 0)
        ctx.delta = false;
}

void SpectrumPanel::placeBandPanel()
{
    if (bandPanel == nullptr)
        return;
    const int b = ctx.selectedEqBand;
    const bool show = ctx.selectedModule == 1 && b >= 1 && b <= 10 && eqedit::bandUsed (ctx, b) && (popup == nullptr || ! popup->isVisible());
    if (! show)
    {
        bandPanel->setVisible (false);
        return;
    }
    bandPanel->setBand (b);
    bandPanel->update();
    const auto pos = plot->nodePos (b - 1) + plot->getPosition().toFloat();
    const auto pa = plot->plotArea() + plot->getPosition().toFloat();
    const auto size = bandPanel->preferredSize();
    const int x = juce::jlimit ((int) pa.getX(), std::max ((int) pa.getX(), (int) pa.getRight() - size.x), juce::roundToInt (pos.x) - size.x / 2);
    const bool below = pos.y + 18 + size.y < pa.getBottom();
    const int yWanted = below ? juce::roundToInt (pos.y) + 18 : juce::roundToInt (pos.y) - 18 - size.y;
    // stay inside the plot (small windows: the panel may cover the point, never the header)
    const int y = std::max ((int) pa.getY(), std::min (yWanted, (int) pa.getBottom() - size.y));
    bandPanel->setBounds (x, y, size.x, size.y);
    if (! bandPanel->isVisible())
        bandPanel->setVisible (true);
}

void SpectrumPanel::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    paintPanelHeader (g, getLocalBounds(), tr ("Spectrum"));
    if (compact)
        return;
    // legend
    if (legendArea.isEmpty())
        return;
    auto r = legendArea;
    const auto item = [&] (juce::Colour c, const juce::String& t, bool band = false)
    {
        auto cell = r.removeFromLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.5f), t)) + 26);
        g.setColour (c);
        if (band)
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) cell.getX(), (float) cell.getCentreY() - 4, 14, 8), 2.0f);
        else
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) cell.getX(), (float) cell.getCentreY() - 1.5f, 14, 3), 1.5f);
        g.setColour (col::text2);
        g.setFont (font (12.5f));
        g.drawText (t, cell.withTrimmedLeft (18), juce::Justification::centredLeft);
    };
    item (juce::Colour (0xff5f86b8), "In");
    item (col::orange, "Out");
    const auto t = ctx.refs.target();
    juce::String refs;
    for (int i = 0; i < reference::ReferenceLibrary::numSlots; ++i)
        if (ctx.refs.slot (i).state == reference::ReferenceLibrary::State::ready && ctx.refs.slot (i).inSet)
            refs << (refs.isEmpty() ? "R" : ", R") << (i + 1);
    item (col::blue.withAlpha (0.45f), t.count > 0 ? "Ref corridor (" + refs + ")" : juce::String ("Ref corridor (no refs)"), true);
    item (col::yellow, "EQ");
}

void SpectrumPanel::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (32).reduced (8, 3);
    const int bh = 24;
    const auto place = [&] (juce::Component& c, int w)
    {
        c.setBounds (head.removeFromRight (w).withSizeKeepingCentre (w, bh));
        head.removeFromRight (6);
    };
    if (compact)
    {
        place (smoothing, 74);
        place (modeCompact, 76);
    }
    else
    {
        // priority order: the most used controls stay when the panel gets narrow
        struct Item
        {
            juce::Component* c;
            int w;
        };
        const Item items[] = { { &mode, 226 }, { &smoothing, 110 }, { &view, 104 }, { &tilt, 122 }, { &channel, 82 }, { &fft, 98 } };
        int avail = head.getWidth() - 70;
        std::vector<const Item*> shown;
        for (const auto& it : items)
            if (it.w + 6 <= avail)
            {
                shown.push_back (&it);
                avail -= it.w + 6;
                it.c->setVisible (true);
            }
            else
                it.c->setVisible (false);
        // keep the visual order of the mockup (right to left)
        for (auto* order : { (juce::Component*) &view, (juce::Component*) &channel, (juce::Component*) &tilt,
                             (juce::Component*) &smoothing, (juce::Component*) &fft, (juce::Component*) &mode })
            for (auto* it : shown)
                if (it->c == order)
                    place (*it->c, it->w);
        legendArea = avail > 300 ? head.withTrimmedLeft (84) : juce::Rectangle<int>();
    }
    plot->setBounds (r.reduced (4, 4));
    if (popup != nullptr)
        popup->setBounds (getWidth() - 330, 40, 320, 150);
}
} // namespace mm::ui
