#include "ui/OtherViews.h"

#include "presets/PresetManager.h"

namespace mm::ui
{
namespace
{
juce::Colour heat (float t)
{
    // perceptually ordered dark-to-bright palette (magma-like)
    static const juce::Colour stops[] = { juce::Colour (0xff0d0f13), juce::Colour (0xff2b1b5c), juce::Colour (0xff7a2a7f), juce::Colour (0xffd34f62),
                                          juce::Colour (0xfffb9a55), juce::Colour (0xfffcf2b0) };
    t = juce::jlimit (0.0f, 1.0f, t) * 5.0f;
    const int i = juce::jlimit (0, 4, (int) t);
    return stops[i].interpolatedWith (stops[i + 1], t - (float) i);
}

void sectionTitle (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t, const juce::String& sub = {})
{
    drawPanel (g, r.toFloat());
    paintPanelHeader (g, r, t, sub);
}
} // namespace

// ---- Low-End -------------------------------------------------------------------------------------------
LowEndView::LowEndView (Context& c) : ctx (c), panel (c, true)
{
    addAndMakeVisible (panel);
    for (int b = -1; b < 2; ++b)
    {
        auto btn = std::make_unique<Btn> (b < 0 ? juce::String ("Full band") : juce::String (analysis::tonalBandName (b)), true);
        btn->onClick = [this, b]
        {
            ctx.monitoring.bandSolo = b;
            for (size_t i = 0; i < listen.size(); ++i)
                listen[i]->setToggleState ((int) i - 1 == b, juce::dontSendNotification);
        };
        setTip (*btn, "t:Band listen\nSolo the sub or bass band to hear what lives in it (monitoring only, never rendered).");
        addAndMakeVisible (*btn);
        listen.push_back (std::move (btn));
    }
}

void LowEndView::resized()
{
    auto r = getLocalBounds();
    side = r.removeFromRight (r.getWidth() * 28 / 100);
    panel.setBounds (r.withTrimmedRight (8));
    auto s = side.withTrimmedTop (40).reduced (12, 0);
    for (auto& b : listen)
    {
        b->setBounds (s.removeFromTop (28));
        s.removeFromTop (6);
    }
}

void LowEndView::paint (juce::Graphics& g)
{
    sectionTitle (g, side, "Low-band listen");
    auto r = side.withTrimmedTop (40 + 3 * 34 + 10).reduced (12, 0);
    const auto& t = viewTap (ctx);
    const auto target = ctx.refs.target();
    g.setColour (col::text2);
    g.setFont (semibold (13.0f));
    g.drawText ("Crest per band (5.8)", r.removeFromTop (20), juce::Justification::left);
    g.setFont (font (12.0f));
    for (int b = 0; b < analysis::numTonalBands; ++b)
    {
        auto row = r.removeFromTop (22);
        g.setColour (col::text);
        g.drawText (analysis::tonalBandName (b), row.removeFromLeft (70), juce::Justification::left);
        g.setColour (col::text);
        g.drawText (juce::String (t.crest6[(size_t) b], 1) + " dB", row.removeFromLeft (70), juce::Justification::right);
        if (target.count > 0)
        {
            const double d = t.crest6[(size_t) b] - target.crest[(size_t) b];
            g.setColour (d < -2.0 ? col::bad : (d < -1.0 ? col::warn : col::ok));
            g.drawText (fmtSigned (d) + " vs ref", row, juce::Justification::right);
        }
    }
    r.removeFromTop (8);
    g.setColour (col::text3);
    g.drawFittedText ("Lower crest than the reference means the band is more compressed (flatter drums or bass).", r.removeFromTop (40), juce::Justification::topLeft, 3);
}

void LowEndView::update() { repaint(); panel.repaint(); }

// ---- Stereo --------------------------------------------------------------------------------------------
StereoView::StereoView (Context& c) : ctx (c), panel (c, true) { addAndMakeVisible (panel); }

void StereoView::resized()
{
    auto r = getLocalBounds();
    bars = r.removeFromBottom (r.getHeight() * 36 / 100);
    panel.setBounds (r.withTrimmedBottom (8));
}

void StereoView::paint (juce::Graphics& g)
{
    sectionTitle (g, bars, "Width and mono compatibility per band");
    const auto& t = viewTap (ctx);
    auto r = bars.withTrimmedTop (40).reduced (16, 8);
    const float w = r.getWidth() / 4.0f;
    static const char* names[] = { "< 150 Hz", "150\xe2\x80\x93" "500 Hz", "0.5\xe2\x80\x93" "4 kHz", "> 4 kHz" };
    for (int b = 0; b < analysis::numStereoBands; ++b)
    {
        const auto& s = t.bandsIntegrated[(size_t) b];
        auto cell = juce::Rectangle<float> (r.getX() + b * w, (float) r.getY(), w - 16, (float) r.getHeight());
        g.setColour (col::text);
        g.setFont (semibold (13.0f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (names[b])), cell.removeFromTop (18), juce::Justification::left);
        const auto bar = [&] (const juce::String& label, float frac, juce::Colour c, const juce::String& value)
        {
            auto row = cell.removeFromTop (34);
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText (label, row.removeFromTop (14), juce::Justification::left);
            auto track = row.reduced (0, 4);
            g.setColour (col::sunk);
            g.fillRoundedRectangle (track, 3.0f);
            g.setColour (c);
            g.fillRoundedRectangle (track.withWidth (track.getWidth() * juce::jlimit (0.0f, 1.0f, frac)), 3.0f);
            g.setColour (col::text);
            g.drawText (value, track.reduced (6, 0), juce::Justification::centredRight);
        };
        bar ("Width (side/mid)", (float) s.widthPercent / 100.0f, col::blue, juce::String (juce::roundToInt (s.widthPercent)) + "%");
        bar ("Mono loss", (float) -s.monoLossDb / 6.0f, s.monoLossDb >= -1 ? col::ok : (s.monoLossDb >= -3 ? col::warn : col::bad), fmtMinus (s.monoLossDb) + " dB");
        bar ("Correlation", (float) (s.correlation + 1.0) * 0.5f, s.correlation >= 0.5 ? col::ok : col::warn, fmtSigned (s.correlation, 2));
    }
}

void StereoView::update() { repaint(); panel.repaint(); }

// ---- Spectrogram -----------------------------------------------------------------------------------------
SpectrogramView::SpectrogramView (Context& c) : ctx (c)
{
    addAndMakeVisible (tapSel);
    addAndMakeVisible (seconds);
    addAndMakeVisible (freeze);
    tapSel.setSelected (1, false);
    tapSel.onChange = [this] (int i)
    {
        shownTap = i;
        lastIndex = 0;
        columns.clear();
    };
    seconds.addItemList ({ "Window 5 s", "Window 10 s", "Window 20 s", "Window 30 s", "Window 60 s" }, 1);
    seconds.setSelectedId (3, juce::dontSendNotification);
    setTip (seconds, "t:Spectrogram window\nHow many seconds the picture shows (1 to 60 s).");
    setTip (freeze, "t:Freeze\nStops the picture so you can read it. Click on the picture to read frequency, time and level.");
    setTip (tapSel, "c:an.seg");
}

void SpectrogramView::resized()
{
    auto r = getLocalBounds();
    auto bottom = r.removeFromBottom (r.getHeight() * 34 / 100);
    plot = r.withTrimmedBottom (8);
    auto head = plot.removeFromTop (32).reduced (8, 4);
    freeze.setBounds (head.removeFromRight (70));
    head.removeFromRight (6);
    seconds.setBounds (head.removeFromRight (120));
    head.removeFromRight (6);
    tapSel.setBounds (head.removeFromRight (90));
    plot = plot.reduced (8, 4).withTrimmedLeft (40).withTrimmedBottom (14);
    const int w = (bottom.getWidth() - 16) / 3;
    harsh = bottom.removeFromLeft (w);
    bottom.removeFromLeft (8);
    crest = bottom.removeFromLeft (w);
    bottom.removeFromLeft (8);
    damage = bottom;
}

void SpectrogramView::update()
{
    if (freeze.getToggleState())
        return;
    std::vector<std::array<float, analysis::TapAnalyzer::spectrogramBins>> fresh;
    lastIndex = ctx.engine.copySpectrogram (shownTap == 1, lastIndex, fresh);
    columns.insert (columns.end(), fresh.begin(), fresh.end());
    static const double secs[] = { 5, 10, 20, 30, 60 };
    const auto maxCols = (size_t) (secs[juce::jlimit (0, 4, seconds.getSelectedId() - 1)] * ctx.engine.spectrogramColumnRate());
    if (columns.size() > maxCols)
        columns.erase (columns.begin(), columns.begin() + (long) (columns.size() - maxCols));
    rebuildImage();
    repaint();
}

void SpectrogramView::rebuildImage()
{
    static const double secs[] = { 5, 10, 20, 30, 60 };
    const int cols = std::max (1, (int) (secs[juce::jlimit (0, 4, seconds.getSelectedId() - 1)] * ctx.engine.spectrogramColumnRate()));
    if (image.getWidth() != cols)
        image = juce::Image (juce::Image::RGB, cols, analysis::TapAnalyzer::spectrogramBins, true);
    image.clear (image.getBounds(), col::sunk);
    const int start = cols - (int) columns.size();
    for (size_t x = 0; x < columns.size(); ++x)
        for (int b = 0; b < analysis::TapAnalyzer::spectrogramBins; ++b)
        {
            const float db = columns[x][(size_t) b];
            image.setPixelAt (start + (int) x, analysis::TapAnalyzer::spectrogramBins - 1 - b, heat ((db + 100.0f) / 90.0f));
        }
}

void SpectrogramView::mouseDown (const juce::MouseEvent& e)
{
    if (! plot.contains (e.getPosition()))
        return;
    const double fx = (double) (plot.getBottom() - e.y) / plot.getHeight();
    const double f = 20.0 * std::pow (1000.0, fx);
    static const double secs[] = { 5, 10, 20, 30, 60 };
    const double window = secs[juce::jlimit (0, 4, seconds.getSelectedId() - 1)];
    const double t = (double) (e.x - plot.getRight()) / plot.getWidth() * window;
    const int col = (int) columns.size() - 1 + (int) std::round (t * ctx.engine.spectrogramColumnRate());
    float level = -100.0f;
    if (col >= 0 && col < (int) columns.size())
        level = columns[(size_t) col][(size_t) juce::jlimit (0, analysis::TapAnalyzer::spectrogramBins - 1, (int) (fx * analysis::TapAnalyzer::spectrogramBins))];
    readout = juce::String (f, f < 1000 ? 0 : 1) + " Hz, " + juce::String (t, 1) + " s, " + fmtMinus (level) + " dB";
    repaint();
}

void SpectrogramView::paint (juce::Graphics& g)
{
    auto top = getLocalBounds().withHeight (plot.getBottom() + 18);
    sectionTitle (g, top, "Spectrogram", "mid signal, log frequency");
    if (image.isValid())
        g.drawImage (image, plot.toFloat(), juce::RectanglePlacement::stretchToFit);
    g.setColour (col::text2);
    g.setFont (font (12.0f));
    for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float y = plot.getBottom() - (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getHeight();
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), juce::Rectangle<float> ((float) plot.getX() - 38, y - 7, 34, 14), juce::Justification::right);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
        g.setColour (col::text2);
    }
    if (readout.isNotEmpty())
    {
        g.setColour (col::tipBg);
        g.fillRoundedRectangle (juce::Rectangle<float> ((float) plot.getX() + 8, (float) plot.getY() + 8, 230, 22), 5.0f);
        g.setColour (col::tipText);
        g.drawText (readout, juce::Rectangle<int> (plot.getX() + 14, plot.getY() + 8, 220, 22), juce::Justification::centredLeft);
    }

    const auto& t = viewTap (ctx);
    const auto target = ctx.refs.target();
    // Harshness (5.7)
    sectionTitle (g, harsh, "Harshness", juce::String (juce::CharPointer_UTF8 ("3\xe2\x80\x93" "8 kHz vs 0.5\xe2\x80\x93" "2 kHz")));
    {
        auto r = harsh.withTrimmedTop (36).reduced (10, 6);
        const double ref = target.count > 0 ? target.harshness : ctx.snap.targets.harshness;
        const double d = t.harshness - ref;
        g.setColour (d <= 1.5 ? col::ok : (d <= 3.0 ? col::warn : col::bad));
        g.setFont (font (22.0f, true));
        g.drawText (fmtSigned (d) + " dB", r.removeFromTop (28), juce::Justification::left);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (target.count > 0 ? "vs reference set" : "vs default target", r.removeFromTop (16), juce::Justification::left);
        auto hr = r.toFloat().reduced (0, 4);
        g.setColour (col::sunk);
        g.fillRoundedRectangle (hr, 5.0f);
        if (t.harshHistory.size() > 1)
        {
            juce::Path p;
            for (size_t i = 0; i < t.harshHistory.size(); ++i)
            {
                const float x = hr.getRight() - (float) (t.harshHistory.size() - 1 - i) / 30.0f * hr.getWidth();
                const float y = hr.getCentreY() - (float) (t.harshHistory[i] - ref) / 12.0f * hr.getHeight();
                i == 0 ? p.startNewSubPath (x, juce::jlimit (hr.getY(), hr.getBottom(), y)) : p.lineTo (x, juce::jlimit (hr.getY(), hr.getBottom(), y));
            }
            g.setColour (col::orange);
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }
        g.setColour (col::text3);
        g.drawHorizontalLine ((int) hr.getCentreY(), hr.getX(), hr.getRight());
        g.drawText ("30 s history", hr.reduced (6, 2), juce::Justification::bottomLeft);
    }
    // Dynamics per band (5.8)
    sectionTitle (g, crest, "Dynamics per band", "crest factor, dB");
    {
        auto r = crest.withTrimmedTop (36).reduced (10, 4);
        const float bw = r.getWidth() / 6.0f;
        float mx = 1.0f;
        for (auto v : t.crest6)
            mx = std::max (mx, (float) v);
        for (int b = 0; b < analysis::numTonalBands; ++b)
        {
            auto cell = juce::Rectangle<float> (r.getX() + b * bw + 4, (float) r.getY(), bw - 8, r.getHeight() - 30.0f);
            const float h = cell.getHeight() * juce::jlimit (0.0f, 1.0f, (float) t.crest6[(size_t) b] / std::max (20.0f, mx));
            g.setColour (col::blue.withAlpha (0.7f));
            g.fillRect (cell.withTop (cell.getBottom() - h));
            if (target.count > 0)
            {
                const float ry = cell.getBottom() - cell.getHeight() * juce::jlimit (0.0f, 1.0f, (float) target.crest[(size_t) b] / std::max (20.0f, mx));
                g.setColour (col::magenta);
                g.fillRect (cell.getX(), ry - 1, cell.getWidth(), 2.0f);
            }
            g.setColour (col::text);
            g.setFont (font (12.0f));
            g.drawText (juce::String (t.crest6[(size_t) b], 1), juce::Rectangle<float> (cell.getX() - 4, cell.getBottom() + 1, cell.getWidth() + 8, 14), juce::Justification::centred);
            g.setColour (col::text3);
            g.drawText (analysis::tonalBandName (b), juce::Rectangle<float> (cell.getX() - 6, cell.getBottom() + 14, cell.getWidth() + 12, 14), juce::Justification::centred);
        }
    }
    // Limiter damage (5.9)
    sectionTitle (g, damage, "Overs and limiter damage");
    {
        auto r = damage.withTrimmedTop (36).reduced (10, 4);
        g.setFont (font (12.0f));
        g.setColour (col::text2);
        g.drawText ("Before limiter: sample overs " + juce::String ((juce::int64) ctx.snap.preLimiterSampleOvers) + ", inter-sample overs " + juce::String ((juce::int64) ctx.snap.preLimiterTpOvers),
                    r.removeFromTop (16), juce::Justification::left);
        const auto stats = [] (const std::array<uint64_t, analysis::GrHistogram::bins>& h, double& above3, double& above6)
        {
            uint64_t total = 0, a3 = 0, a6 = 0;
            for (int b = 0; b < analysis::GrHistogram::bins; ++b)
            {
                total += h[(size_t) b];
                if (b * analysis::GrHistogram::binWidth >= 3.0)
                    a3 += h[(size_t) b];
                if (b * analysis::GrHistogram::binWidth >= 6.0)
                    a6 += h[(size_t) b];
            }
            above3 = total ? 100.0 * a3 / total : 0.0;
            above6 = total ? 100.0 * a6 / total : 0.0;
        };
        for (int which = 0; which < 2; ++which)
        {
            const auto& h = which == 0 ? ctx.snap.clipHist : ctx.snap.limHist;
            double a3, a6;
            stats (h, a3, a6);
            auto row = r.removeFromTop ((r.getHeight()) / (2 - which));
            g.setColour (col::text);
            g.setFont (semibold (12.5f));
            g.drawText ((which == 0 ? "Clipper" : "Limiter") + juce::String (":  > 3 dB ") + juce::String (a3, 1) + "%,  > 6 dB " + juce::String (a6, 1) + "%",
                        row.removeFromTop (16), juce::Justification::left);
            auto hr = row.toFloat().reduced (0, 3);
            uint64_t mx = 1;
            for (int b = 1; b < analysis::GrHistogram::bins; ++b)
                mx = std::max (mx, h[(size_t) b]);
            const float bw = hr.getWidth() / 48.0f;
            for (int b = 1; b < 48; ++b)
            {
                const float frac = (float) h[(size_t) b] / (float) mx;
                g.setColour (b * analysis::GrHistogram::binWidth >= 6.0 ? col::bad : (b * analysis::GrHistogram::binWidth >= 3.0 ? col::warn : col::orange));
                g.fillRect (hr.getX() + b * bw, hr.getBottom() - frac * hr.getHeight(), bw - 1, frac * hr.getHeight());
            }
        }
    }
}

// ---- Album ---------------------------------------------------------------------------------------------
AlbumView::AlbumView (Context& c) : ctx (c)
{
    for (auto* b : { &newAlbum, &openAlbum, &store })
        addAndMakeVisible (*b);
    addAndMakeVisible (targetMode);
    targetMode.addItemList ({ "Target: mean of all tracks", "Target: reference set", "Target: anchor track" }, 1);
    targetMode.onChange = [this]
    {
        if (! ctx.album.isOpen())
            return;
        ctx.album.album().targetMode = (album::Album::TargetMode) (targetMode.getSelectedId() - 1);
        juce::String err;
        ctx.album.save (err);
        repaint();
    };
    setTip (store, "t:Store passport\nSaves the measurements of the current track since the last Reset into the album file. Play the whole song first.");
    setTip (newAlbum, "t:New album\nCreates an album file (.mastermind-album) in a folder you choose. All plugin instances can share it.");
    setTip (openAlbum, "t:Open album\nOpens an existing album file.");
    newAlbum.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("New album", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Album.mastermind-album"), "*.mastermind-album");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
        {
            juce::String err;
            if (fc.getResult() != juce::File() && ! ctx.album.create (fc.getResult(), fc.getResult().getFileNameWithoutExtension(), err) && ctx.notify)
                ctx.notify (err);
            ctx.proc.syncExtraState();
            repaint();
        });
    };
    openAlbum.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open album", juce::File(), "*.mastermind-album");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            juce::String err;
            if (fc.getResult().existsAsFile() && ! ctx.album.open (fc.getResult(), err) && ctx.notify)
                ctx.notify (err);
            ctx.proc.syncExtraState();
            repaint();
        });
    };
    store.onClick = [this] { storePassport(); };
}

void AlbumView::storePassport()
{
    if (! ctx.album.isOpen())
    {
        if (ctx.notify)
            ctx.notify ("Create or open an album first.");
        return;
    }
    auto* w = new juce::AlertWindow ("Store passport", "Track name", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", "Track " + juce::String ((int) ctx.album.album().tracks.size() + 1));
    w->addButton ("Store", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
    {
        if (r != 1)
            return;
        auto p = ctx.engine.makePassport (w->getTextEditorContents ("name").trim());
        juce::String err;
        if (p.integrated < -70.0)
            err = "No signal was measured. Play the song, then store the passport.";
        else if (! ctx.album.storePassport (p, err))
        {
        }
        if (err.isNotEmpty() && ctx.notify)
            ctx.notify (err);
        repaint();
    }), true);
}

void AlbumView::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (40).reduced (10, 6);
    newAlbum.setBounds (head.removeFromLeft (110));
    head.removeFromLeft (6);
    openAlbum.setBounds (head.removeFromLeft (112));
    head.removeFromLeft (6);
    store.setBounds (head.removeFromLeft (128));
    targetMode.setBounds (head.removeFromRight (220));
    auto bottom = r.removeFromBottom (r.getHeight() * 36 / 100);
    table = r.reduced (0, 4);
    overlay = bottom.removeFromLeft (bottom.getWidth() * 55 / 100).withTrimmedRight (4);
    bars = bottom.withTrimmedLeft (4);
}

void AlbumView::update()
{
    if (ctx.album.reloadIfChanged())
        repaint();
}

void AlbumView::mouseUp (const juce::MouseEvent& e)
{
    if (! ctx.album.isOpen() || ! table.contains (e.getPosition()))
        return;
    const int row = (e.y - table.getY() - 36 - rowH) / rowH;
    auto& a = ctx.album.album();
    if (row < 0 || row >= (int) a.tracks.size())
        return;
    const auto name = a.tracks[(size_t) row].name;
    juce::PopupMenu m;
    m.addSectionHeader (name);
    m.addItem (1, "Anchor track (best-sounding reference)", true, a.anchor == row);
    m.addItem (2, "Store chain snapshot for this track");
    m.addItem (3, "Recall chain snapshot", a.snapshots.count (name) > 0);
    m.addItem (4, "Remove from album");
    m.showMenuAsync (juce::PopupMenu::Options(), [this, row, name] (int r)
    {
        auto& al = ctx.album.album();
        juce::String err;
        if (r == 1)
        {
            al.anchor = al.anchor == row ? -1 : row;
            al.targetMode = album::Album::TargetMode::anchor;
        }
        else if (r == 2)
            al.snapshots[name] = presets::PresetManager::toJson (ctx.presets.captureValues(), name);
        else if (r == 3)
        {
            std::map<juce::String, float> values;
            juce::String n;
            if (presets::PresetManager::fromJson (al.snapshots[name], values, n, err))
                ctx.presets.applyValues (values);
        }
        else if (r == 4)
        {
            al.tracks.erase (al.tracks.begin() + row);
            if (al.anchor == row)
                al.anchor = -1;
        }
        if (r > 0)
            ctx.album.save (err);
        if (err.isNotEmpty() && ctx.notify)
            ctx.notify (err);
        repaint();
    });
}

void AlbumView::paint (juce::Graphics& g)
{
    sectionTitle (g, table, ctx.album.isOpen() ? "Album: " + ctx.album.album().name : juce::String ("Album"),
                  ctx.album.isOpen() ? ctx.album.getFile().getFullPathName() : juce::String ("Create or open an album file to compare tracks"));
    if (! ctx.album.isOpen())
    {
        targetMode.setSelectedId (1, juce::dontSendNotification);
        auto r = table.withTrimmedTop (40).reduced (24, 16);
        g.setColour (col::text2);
        g.setFont (font (14.0f));
        g.drawFittedText ("No album open.\n\n1. Press New album... and choose a folder (all plugin instances can share the file).\n"
                          "2. Play a whole song, then press Store passport.\n"
                          "3. Repeat for every song: the table compares loudness, tonal balance, width and harshness against one album target.",
                          r, juce::Justification::topLeft, 8);
        return;
    }
    const auto& a = ctx.album.album();
    targetMode.setSelectedId ((int) a.targetMode + 1, juce::dontSendNotification);
    const auto refT = ctx.refs.target();
    const auto target = a.target (&refT);

    auto r = table.withTrimmedTop (36).reduced (8, 0);
    static const char* heads[] = { "#", "Track", "LUFS-I", "LRA", "PLR", "Sub", "Bass", "Low-mid", "Mid", "Pres.", "Air", "Width", "Harsh" };
    const int widths[] = { 26, 0, 64, 50, 50, 56, 56, 64, 56, 56, 56, 58, 58 };
    int fixed = 0;
    for (int w : widths)
        fixed += w;
    const int nameW = std::max (100, r.getWidth() - fixed);
    auto headRow = r.removeFromTop (rowH);
    g.setFont (font (12.0f));
    g.setColour (col::text3);
    for (int c = 0; c < 13; ++c)
        g.drawText (heads[c], headRow.removeFromLeft (c == 1 ? nameW : widths[c]), c <= 1 ? juce::Justification::centredLeft : juce::Justification::centredRight);
    const auto cellCol = [] (double d, double g1, double g2) { return std::abs (d) <= g1 ? col::ok : (std::abs (d) <= g2 ? col::warn : col::bad); };
    for (size_t i = 0; i < a.tracks.size(); ++i)
    {
        const auto& t = a.tracks[i];
        auto row = r.removeFromTop (rowH);
        if (row.getBottom() > table.getBottom() - 40)
            break;
        g.setColour (col::line);
        g.drawHorizontalLine (row.getY(), (float) row.getX(), (float) row.getRight());
        g.setFont (font (12.5f, (int) i == a.anchor));
        g.setColour (col::text2);
        g.drawText (juce::String ((int) i + 1), row.removeFromLeft (widths[0]), juce::Justification::centredLeft);
        g.setColour (col::text);
        g.drawText (t.name + ((int) i == a.anchor ? "  (anchor)" : ""), row.removeFromLeft (nameW), juce::Justification::centredLeft);
        const auto num = [&] (double v, double tg, int w, double g1, double g2, int dec = 1)
        {
            g.setColour (cellCol (v - tg, g1, g2));
            g.drawText (juce::String (v, dec), row.removeFromLeft (w), juce::Justification::centredRight);
        };
        num (t.integrated, target.integrated, widths[2], 1.0, 2.0);
        num (t.lra, target.lra, widths[3], 2.0, 4.0);
        num (t.plr, target.plr, widths[4], 1.0, 2.0);
        for (int b = 0; b < 6; ++b)
        {
            const double d = t.tonal[(size_t) b] - target.tonal[(size_t) b];
            g.setColour (cellCol (d, 1.0, 2.5));
            g.drawText (fmtSigned (d), row.removeFromLeft (widths[5 + b]), juce::Justification::centredRight);
        }
        num (t.width[2], target.width[2], widths[11], 10.0, 25.0, 0);
        num (t.harshness - target.harshness, 0.0, widths[12], 1.5, 3.0);
    }
    // hints
    const auto lines = album::albumHints (a, target);
    auto hr = table.withTop (table.getBottom() - 40).reduced (12, 4);
    g.setColour (col::text);
    g.setFont (font (12.5f));
    g.drawFittedText (lines.isEmpty() ? juce::String ("All tracks are within 1 dB of the album target.") : lines.joinIntoString ("   ·   "), hr, juce::Justification::centredLeft, 2);

    // spectra overlay
    sectionTitle (g, overlay, "Spectra of all tracks", "current track highlighted");
    auto pr = overlay.withTrimmedTop (36).reduced (10, 6).toFloat();
    g.setColour (col::sunk);
    g.fillRoundedRectangle (pr, 5.0f);
    const auto& freqs = reference::spectrumFreqs();
    const auto ours = ctx.engine.outSpectrumNormalised();
    const auto drawSpec = [&] (const std::vector<float>& v, juce::Colour c, float w)
    {
        if (v.size() != freqs.size())
            return;
        juce::Path p;
        for (size_t k = 0; k < v.size(); ++k)
        {
            const float x = pr.getX() + (float) k / (float) (v.size() - 1) * pr.getWidth();
            const float y = pr.getY() + juce::jlimit (0.0f, 1.0f, (-5.0f - v[k]) / 40.0f) * pr.getHeight();
            k == 0 ? p.startNewSubPath (x, y) : p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (w));
    };
    for (const auto& t : a.tracks)
        drawSpec (t.spectrum, col::text3.withAlpha (0.6f), 1.0f);
    drawSpec (target.spectrum, juce::Colours::white.withAlpha (0.6f), 1.2f);
    drawSpec (ours, col::orange, 2.0f);

    // loudness in track order
    sectionTitle (g, bars, "Loudness in track order", "LUFS-I");
    auto br = bars.withTrimmedTop (36).reduced (10, 6).toFloat();
    if (! a.tracks.empty())
    {
        const float bw = br.getWidth() / (float) a.tracks.size();
        for (size_t i = 0; i < a.tracks.size(); ++i)
        {
            const double v = a.tracks[i].integrated;
            const float h = juce::jlimit (0.0f, 1.0f, (float) (v + 30.0) / 30.0f) * (br.getHeight() - 16);
            g.setColour (cellCol (v - target.integrated, 1.0, 2.0));
            g.fillRect (br.getX() + i * bw + 3, br.getBottom() - 16 - h, bw - 6, h);
            g.setColour (col::text2);
            g.setFont (font (12.0f));
            g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (br.getX() + i * bw, br.getBottom() - 14, bw, 14), juce::Justification::centred);
        }
    }
}
} // namespace mm::ui
