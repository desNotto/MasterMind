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
    setTip (*this, "t:Waveform of the reference\nClick or drag to move in the song. The coloured part is already played.");
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
    setMouseCursor (f >= 0.0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
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

void RefPlayer::mouseDown (const juce::MouseEvent& e)
{
    if (! waveArea.contains (e.getPosition()))
        return;
    dragFraction = fractionAt (e.x);
    seekTo (dragFraction);
    repaint();
}

void RefPlayer::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFraction < 0.0)
        return;
    dragFraction = fractionAt (e.x);
    hoverFraction = dragFraction;
    seekTo (dragFraction);
    repaint();
}

void RefPlayer::mouseUp (const juce::MouseEvent&)
{
    dragFraction = -1.0;
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

    // hover position and play head
    if (hoverFraction >= 0.0 && dragFraction < 0.0)
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
