#include "ui/RefPlayer.h"
#include "ui/Locale.h"

namespace mm::ui
{
namespace
{
juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }

juce::String clockText (double s)
{
    const int t = std::max (0, (int) s);
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}
} // namespace

void RefPlayer::IconBtn::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (down ? col::panel2.brighter (0.25f) : (highlighted ? col::panel2.brighter (0.12f) : col::panel2));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (col::line2);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    const auto c = r.getCentre();
    g.setColour (col::text);
    juce::Path p;
    if (icon == Icon::playPause && playing != nullptr && playing->load())
    {
        g.fillRect (juce::Rectangle<float> (c.x - 4.5f, c.y - 5.5f, 3.5f, 11.0f));
        g.fillRect (juce::Rectangle<float> (c.x + 1.0f, c.y - 5.5f, 3.5f, 11.0f));
        return;
    }
    if (icon == Icon::playPause)
        p.addTriangle (c.x - 3.5f, c.y - 6.0f, c.x - 3.5f, c.y + 6.0f, c.x + 6.0f, c.y);
    else if (icon == Icon::restart)
    {
        g.fillRect (juce::Rectangle<float> (c.x - 6.0f, c.y - 5.5f, 2.0f, 11.0f));
        p.addTriangle (c.x + 5.0f, c.y - 5.5f, c.x + 5.0f, c.y + 5.5f, c.x - 3.5f, c.y);
    }
    else
    {
        p.startNewSubPath (c.x - 4.5f, c.y - 4.5f);
        p.lineTo (c.x + 4.5f, c.y + 4.5f);
        p.startNewSubPath (c.x + 4.5f, c.y - 4.5f);
        p.lineTo (c.x - 4.5f, c.y + 4.5f);
        g.strokePath (p, juce::PathStrokeType (1.6f));
        return;
    }
    g.fillPath (p);
}

RefPlayer::RefPlayer (Context& c)
    : ctx (c), play (IconBtn::Icon::playPause, &c.monitoring.refPlaying), restart (IconBtn::Icon::restart), close (IconBtn::Icon::close)
{
    for (auto* b : { &play, &restart, &close })
        addAndMakeVisible (*b);
    play.onClick = [this] { ctx.monitoring.refPlaying = ! ctx.monitoring.refPlaying.load(); };
    restart.onClick = [this]
    {
        ctx.monitoring.seekRequest = 0.0;
        ctx.monitoring.refPlaying = true;
    };
    close.onClick = [this]
    {
        ctx.monitoring.referenceActive = false;
        ctx.monitoring.refPlaying = false;
    };
    setTip (play, "t:Play / pause\nPlays or pauses the reference. Space in the host still controls your song.");
    setTip (restart, "t:From the start\nPlays the reference from the beginning.");
    setTip (close, "t:Back to A\nStops the reference. You hear your mix again.");
    setTip (*this, "c:ref.region");
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void RefPlayer::update()
{
    play.repaint();
    repaint();
}

void RefPlayer::resized()
{
    auto r = getLocalBounds().reduced (8, compact ? 4 : 6);
    const int bw = compact ? 28 : 32;
    play.setBounds (r.removeFromLeft (bw));
    r.removeFromLeft (4);
    restart.setBounds (r.removeFromLeft (bw));
    r.removeFromLeft (10);
    close.setBounds (r.removeFromRight (bw));
    r.removeFromRight (10);
    infoArea = r.removeFromLeft (compact ? std::min (150, r.getWidth() / 3) : std::min (250, r.getWidth() / 3));
    r.removeFromLeft (10);
    waveArea = r;
}

double RefPlayer::fractionAt (int x) const
{
    return waveArea.getWidth() > 0 ? juce::jlimit (0.0, 1.0, (x - waveArea.getX()) / (double) waveArea.getWidth()) : 0.0;
}

void RefPlayer::seekTo (double fraction)
{
    const double len = ctx.monitoring.refLengthSeconds.load();
    if (len > 0.0)
        ctx.monitoring.seekRequest = fraction * len;
}

void RefPlayer::mouseMove (const juce::MouseEvent& e)
{
    const double f = waveArea.contains (e.getPosition()) ? fractionAt (e.x) : -1.0;
    const auto region = ctx.refs.region (currentFile());
    const bool edge = f >= 0.0 && region.valid()
                      && (std::abs (e.position.x - xOfSeconds (region.start)) <= 5.0f || std::abs (e.position.x - xOfSeconds (region.end)) <= 5.0f);
    setMouseCursor (edge ? juce::MouseCursor::LeftRightResizeCursor
                         : (f >= 0.0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
    if (f != hoverFraction)
    {
        hoverFraction = f;
        repaint (waveArea.expanded (2));
    }
}

void RefPlayer::mouseExit (const juce::MouseEvent&)
{
    hoverFraction = -1.0;
    repaint();
}

juce::File RefPlayer::currentFile() const
{
    const auto* ref = ctx.monitoring.currentReferenceForUi();
    return ref != nullptr ? ref->file : juce::File();
}

float RefPlayer::xOfSeconds (double s) const
{
    const double len = ctx.monitoring.refLengthSeconds.load();
    return len > 0.0 ? (float) waveArea.getX() + (float) (s / len) * (float) waveArea.getWidth() : (float) waveArea.getX();
}

void RefPlayer::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    if (! waveArea.contains (e.getPosition()))
        return;
    const auto file = currentFile();
    const auto region = ctx.refs.region (file);
    if (region.valid() && pillClose.contains (e.position))
    {
        ctx.refs.setRegion (file, 0.0, 0.0); // the cross clears the region
        ctx.proc.syncExtraState();
        repaint();
        return;
    }
    downX = e.x;
    if (region.valid() && std::abs (e.position.x - xOfSeconds (region.start)) <= 5.0f)
        drag = Drag::startEdge;
    else if (region.valid() && std::abs (e.position.x - xOfSeconds (region.end)) <= 5.0f)
        drag = Drag::endEdge;
    else
    {
        // a click seeks; a drag becomes a selection (decided in mouseDrag)
        drag = Drag::seek;
        dragFraction = fractionAt (e.x);
        seekTo (dragFraction);
    }
    selA = region.start;
    selB = region.end;
    repaint();
}

void RefPlayer::mouseDrag (const juce::MouseEvent& e)
{
    const double len = ctx.monitoring.refLengthSeconds.load();
    if (drag == Drag::none || len <= 0.0)
        return;
    const double t = fractionAt (e.x) * len;
    if (drag == Drag::seek && std::abs (e.x - downX) > 4)
    {
        drag = Drag::select;
        selA = fractionAt (downX) * len;
        dragFraction = -1.0;
    }
    if (drag == Drag::seek)
    {
        dragFraction = fractionAt (e.x);
        hoverFraction = dragFraction;
        seekTo (dragFraction);
    }
    else if (drag == Drag::select || drag == Drag::endEdge)
        selB = t;
    else if (drag == Drag::startEdge)
        selA = t;
    repaint();
}

void RefPlayer::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::select || drag == Drag::startEdge || drag == Drag::endEdge)
    {
        const double a = std::min (selA, selB), b = std::max (selA, selB);
        if (b - a >= 1.0) // at least a second; shorter drags change nothing
        {
            ctx.refs.setRegion (currentFile(), a, b);
            ctx.proc.syncExtraState();
        }
    }
    drag = Drag::none;
    dragFraction = -1.0;
    repaint();
}

void RefPlayer::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    drawPanel (g, getLocalBounds().toFloat().reduced (0.5f), col::panel, 8.0f);

    const auto* ref = ctx.monitoring.currentReferenceForUi();
    const bool loading = ctx.refs.isLoadingPlayback();
    const double len = ctx.monitoring.refLengthSeconds.load();
    const double pos = ctx.monitoring.refPositionSeconds.load();
    double frac = len > 0.0 ? juce::jlimit (0.0, 1.0, pos / len) : 0.0;
    if (dragFraction >= 0.0)
        frac = dragFraction;

    // info: B badge, name, time and loudness match
    {
        auto r = infoArea;
        auto badge = r.removeFromLeft (20).withSizeKeepingCentre (20, 18).toFloat();
        g.setColour (col::magenta);
        g.fillRoundedRectangle (badge, 4.0f);
        g.setColour (juce::Colours::white);
        g.setFont (font (12.0f, true));
        g.drawText ("B", badge, juce::Justification::centred);
        r.removeFromLeft (8);
        const juce::String name = ref != nullptr ? ref->name : (loading ? tr ("Loading...") : juce::String ("-"));
        const juce::String sub = clockText (dragFraction >= 0.0 ? frac * len : pos) + " / " + clockText (len) + u8 ("  \xc2\xb7  ")
                               + tr ("match") + " " + fmtSigned (ctx.monitoring.appliedRefGainDb.load()) + " dB";
        if (compact)
        {
            g.setColour (col::text);
            g.setFont (semibold (12.5f));
            g.drawText (name, r, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (col::text);
            g.setFont (semibold (13.0f));
            g.drawText (name, r.removeFromTop (r.getHeight() / 2 + 1), juce::Justification::bottomLeft, true);
            g.setColour (col::text2);
            g.setFont (font (12.0f));
            g.drawText (sub, r, juce::Justification::topLeft, true);
        }
    }

    // waveform overview
    const auto wa = waveArea.toFloat();
    g.setColour (col::sunk);
    g.fillRoundedRectangle (wa, 4.0f);
    if (ref == nullptr || ref->wavePeak.empty())
    {
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (loading ? tr ("Loading the song...") : tr ("No reference"), waveArea, juce::Justification::centred);
        return;
    }

    const auto& pk = ref->wavePeak;
    const auto& rms = ref->waveRms;
    const int bins = (int) pk.size();
    float top = 1.0e-6f;
    for (float v : pk)
        top = std::max (top, v);
    const float mid = wa.getCentreY(), half = wa.getHeight() * 0.5f - 3.0f;
    const int w = waveArea.getWidth();
    const int playX = waveArea.getX() + juce::roundToInt (frac * w);

    // loop region
    const double la = ctx.monitoring.refLoopStart.load(), lb = ctx.monitoring.refLoopEnd.load();
    if (len > 0.0 && lb > la + 0.05)
    {
        const float x0 = wa.getX() + (float) (la / len) * wa.getWidth(), x1 = wa.getX() + (float) (std::min (lb, len) / len) * wa.getWidth();
        g.setColour (col::magenta.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (x0, wa.getY(), x1 - x0, wa.getHeight()));
    }

    for (int x = 0; x < w; ++x)
    {
        const int b0 = x * bins / w, b1 = std::max (b0 + 1, (x + 1) * bins / w);
        float p = 0.0f, q = 0.0f;
        for (int b = b0; b < std::min (b1, bins); ++b)
        {
            p = std::max (p, pk[(size_t) b]);
            q = std::max (q, rms[(size_t) b]);
        }
        const float hp = std::max (0.5f, half * p / top), hq = std::max (0.5f, half * std::min (q / top, 1.0f));
        const bool played = waveArea.getX() + x < playX;
        const float px = (float) (waveArea.getX() + x);
        g.setColour (played ? col::magenta.withAlpha (0.45f) : col::text3.withAlpha (0.45f));
        g.fillRect (px, mid - hp, 1.0f, 2.0f * hp);
        g.setColour (played ? col::magenta : col::text2.withAlpha (0.8f));
        g.fillRect (px, mid - hq, 1.0f, 2.0f * hq);
    }

    // metrics region: blue zone with handles and a label with a cross (mockup v19)
    {
        auto region = ctx.refs.region (ref->file);
        const bool dragging = drag == Drag::select || drag == Drag::startEdge || drag == Drag::endEdge;
        if (dragging)
            region = { std::min (selA, selB), std::max (selA, selB) };
        pillClose = {};
        if (len > 0.0 && region.end > region.start)
        {
            const float x0 = std::max (wa.getX(), xOfSeconds (region.start)), x1 = std::min (wa.getRight(), xOfSeconds (std::min (region.end, len)));
            const auto blue = juce::Colour (0xff4ea1ff);
            g.setColour (blue.withAlpha (0.16f));
            g.fillRect (juce::Rectangle<float> (x0, wa.getY(), x1 - x0, wa.getHeight()));
            g.setColour (blue);
            g.fillRect (x0 - 1.0f, wa.getY(), 2.0f, wa.getHeight());
            g.fillRect (x1 - 1.0f, wa.getY(), 2.0f, wa.getHeight());
            g.fillRoundedRectangle (juce::Rectangle<float> (x0 - 3.0f, wa.getCentreY() - 7.0f, 6.0f, 14.0f), 2.0f);
            g.fillRoundedRectangle (juce::Rectangle<float> (x1 - 3.0f, wa.getCentreY() - 7.0f, 6.0f, 14.0f), 2.0f);
            const juce::String label = "Metrics " + clockText (region.start) + u8 ("\xe2\x80\x93") + clockText (region.end) + u8 (" \xc2\xb7 ")
                                     + juce::String (juce::roundToInt (region.end - region.start)) + " s"
                                     + (ctx.refs.isAnalysingRegion (ref->file) ? tr (u8 (" \xc2\xb7 analysing\xe2\x80\xa6")) : juce::String());
            const auto f = font (11.0f, true);
            const float lw = juce::GlyphArrangement::getStringWidth (f, label) + 26.0f;
            auto pill = juce::Rectangle<float> (x0 + 3.0f, wa.getY() + 2.0f, lw, 15.0f);
            if (pill.getRight() > wa.getRight() - 2.0f)
                pill.setX (std::max (wa.getX() + 2.0f, wa.getRight() - 2.0f - lw));
            g.setColour (juce::Colour (0xff163152));
            g.fillRoundedRectangle (pill, 3.0f);
            g.setColour (juce::Colour (0xffcfe3ff));
            g.setFont (f);
            g.drawText (label, pill.withTrimmedLeft (6.0f).withTrimmedRight (18.0f), juce::Justification::centredLeft, false);
            if (! dragging)
            {
                pillClose = pill.removeFromRight (16.0f);
                g.drawText (u8 ("\xe2\x9c\x95"), pillClose, juce::Justification::centred, false);
            }
        }
    }

    // hover position and play head
    if (hoverFraction >= 0.0 && dragFraction < 0.0 && drag == Drag::none)
    {
        const float hx = wa.getX() + (float) hoverFraction * wa.getWidth();
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.fillRect (hx, wa.getY(), 1.0f, wa.getHeight());
        const auto t = clockText (hoverFraction * len);
        g.setFont (font (11.5f, true));
        const float tw = juce::GlyphArrangement::getStringWidth (font (11.5f, true), t) + 8.0f;
        auto box = juce::Rectangle<float> (hx + 4.0f, wa.getY() + 2.0f, tw, 15.0f);
        if (box.getRight() > wa.getRight())
            box.setX (hx - 4.0f - tw);
        g.setColour (col::tipBg);
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (col::tipText);
        g.drawText (t, box, juce::Justification::centred);
    }
    g.setColour (juce::Colours::white);
    g.fillRect ((float) playX - 1.0f, wa.getY(), 2.0f, wa.getHeight());
}
} // namespace mm::ui
