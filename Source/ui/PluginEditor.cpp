#include "ui/PluginEditor.h"
#include "ui/Locale.h"

#include "mm_build_info.h"

namespace mm::ui
{
LayoutMode layoutModeFor (int w, int h)
{
    if (w >= 1000 && h >= 640)
        return LayoutMode::full;
    if (w >= 720 && h >= 460)
        return LayoutMode::medium;
    return LayoutMode::compact;
}

juce::Point<int> referenceSize (LayoutMode m)
{
    switch (m)
    {
        case LayoutMode::full: return { 1280, 800 };
        case LayoutMode::medium: return { 960, 600 };
        default: return { 640, 400 };
    }
}

float layoutScale (int w, int h)
{
    const auto ref = referenceSize (layoutModeFor (w, h));
    const float s = std::min ((float) w / (float) ref.x, (float) h / (float) ref.y);
    // the smallest UI text is 12 px: keep it at 11 px or more on screen (spec section 8)
    return juce::jlimit (11.0f / 12.0f, 4.0f, s);
}
} // namespace mm::ui

using namespace mm::ui;

namespace
{
constexpr double saveDebounceMs = 400.0;

mm::window::Size primaryWorkArea()
{
    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userBounds.toNearestIntEdges();
        return { area.getWidth(), area.getHeight() };
    }
    return {};
}
} // namespace

// ---- Root ----------------------------------------------------------------------------------------------
class MasterMindEditor::Root final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override { g.fillAll (col::bg); }
};

// ---- Tab bar ---------------------------------------------------------------------------------------------
class MasterMindEditor::TabBar final : public juce::Component, private juce::Timer
{
public:
    TabBar (Context& c, MasterMindEditor& e) : ctx (c), editor (e)
    {
        addAndMakeVisible (analyse);
        addAndMakeVisible (reset);
        addAndMakeVisible (resetOnPlay);
        setTip (analyse, "c:an.seg");
        setTip (reset, "c:an.reset");
        setTip (resetOnPlay, "c:an.rop");
        analyse.setSelected (ctx.tap, false);
        analyse.onChange = [this] (int i) { ctx.tap = i; };
        reset.onClick = [this]
        {
            ctx.engine.requestReset();
            ctx.proc.getChain().resetOvers();
        };
        resetOnPlay.setButtonText ("Reset on play");
        resetOnPlay.setToggleState (ctx.proc.resetOnPlay.load(), juce::dontSendNotification);
        resetOnPlay.onClick = [this]
        {
            ctx.proc.resetOnPlay = resetOnPlay.getToggleState();
            ctx.settings.set ("resetOnPlay", resetOnPlay.getToggleState());
        };
        startTimerHz (10);
    }

    void setTabs (juce::StringArray names)
    {
        tabs = std::move (names);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::line);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        tabAreas.clear();
        int x = 8;
        for (const auto& t : tabs)
        {
            const bool more = t.endsWith (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")));
            const bool on = t == editor.currentTab || (more && ! editor.isCompactTab (editor.currentTab));
            const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (13.5f, on), t)) + 28;
            const juce::Rectangle<int> r (x, 0, w, getHeight());
            tabAreas.push_back ({ t, r });
            g.setColour (on ? juce::Colours::white : col::text2);
            g.setFont (font (13.5f, on));
            g.drawText (t, r, juce::Justification::centred);
            if (on)
            {
                g.setColour (col::orange);
                g.fillRect (r.getX() + 4, getHeight() - 3, r.getWidth() - 8, 2);
            }
            x += w;
        }
        g.setColour (col::text2);
        g.setFont (font (13.0f));
        resetOnPlay.setButtonText (tr ("Reset on play"));
        g.drawText (tr ("Analyse"), analyse.getBounds().translated (-62, 0).withWidth (58), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 3);
        resetOnPlay.setBounds (r.removeFromRight (118));
        r.removeFromRight (6);
        reset.setBounds (r.removeFromRight (56));
        r.removeFromRight (6);
        analyse.setBounds (r.removeFromRight (130));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (const auto& [name, r] : tabAreas)
            if (r.contains (e.getPosition()))
            {
                if (name.endsWith (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe"))))
                {
                    juce::PopupMenu m;
                    const juce::StringArray more { "Low-End", "Stereo", "Spectrogram", "Album", "References" };
                    for (int i = 0; i < more.size(); ++i)
                        m.addItem (i + 1, more[i], true, editor.currentTab == more[i]);
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (r)), [this, more] (int res)
                    {
                        if (res > 0)
                            editor.showTab (more[res - 1]);
                    });
                }
                else
                    editor.showTab (name);
            }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        for (const auto& [name, r] : tabAreas)
            if (r.contains (e.getPosition()))
            {
                const auto id = name.toLowerCase().replace ("-", "").upToFirstOccurrenceOf (" ", false, false);
                setTip (*this, "c:tab." + id);
                return;
            }
        setTip (*this, {});
    }

    void timerCallback() override
    {
        analyse.setSelected (ctx.tap, false);
        repaint();
    }

    Context& ctx;
    MasterMindEditor& editor;
    juce::StringArray tabs;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> tabAreas;
    Segmented analyse { { "In", "Out", "Both" } };
    Btn reset { "Reset" };
    juce::ToggleButton resetOnPlay;
};

// ---- Footer ---------------------------------------------------------------------------------------------
class MasterMindEditor::Footer final : public juce::Component
{
public:
    Footer (Context& c) : ctx (c)
    {
        addAndMakeVisible (options);
        setTip (options, "t:Options\nQuality (oversampling), traffic-light thresholds and folders.");
        options.onClick = [this] { showOptions(); };
    }

    void showOptions()
    {
        juce::PopupMenu m;
        juce::PopupMenu quality;
        const int q = juce::roundToInt (ctx.param ("quality"));
        quality.addItem (1, "Auto (2x real time, 4x offline)", true, q == 0);
        quality.addItem (2, "Normal (2x)", true, q == 1);
        quality.addItem (3, "High (4x)", true, q == 2);
        m.addSubMenu ("Quality", quality);
        m.addItem (10, "Reset analysis on transport start", true, ctx.proc.resetOnPlay.load());
        juce::PopupMenu lang;
        lang.addItem (30, "English", true, language() == Language::english);
        lang.addItem (31, juce::String (juce::CharPointer_UTF8 ("\xd0\xa0\xd1\x83\xd1\x81\xd1\x81\xd0\xba\xd0\xb8\xd0\xb9")), true, language() == Language::russian);
        m.addSubMenu ("Language", lang);
        m.addSeparator();
        m.addItem (20, "Open settings folder (settings.json, presets, emulations)");
        m.addItem (21, "Open reference cache folder");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (options), [this] (int r)
        {
            if (r >= 1 && r <= 3)
                ctx.setParam ("quality", (float) (r - 1));
            else if (r == 10)
            {
                ctx.proc.resetOnPlay = ! ctx.proc.resetOnPlay.load();
                ctx.settings.set ("resetOnPlay", ctx.proc.resetOnPlay.load());
            }
            else if (r == 20)
                mm::GlobalSettings::userDataDirectory().startAsProcess();
            else if (r == 30 || r == 31)
            {
                setLanguage (r == 31 ? Language::russian : Language::english);
                ctx.settings.set ("language", r == 31 ? "ru" : "en");
                if (auto* top = getTopLevelComponent())
                    top->repaint();
            }
            else if (r == 21)
                mm::GlobalSettings::userDataDirectory().getChildFile ("refcache").startAsProcess();
        });
    }

    /** Hint bar (mockup v18): the tooltip of the control under the mouse in two lines, otherwise
        the version. Line 1: name, range and what it does; line 2: advice and the other sections. */
    void setHint (const TipBubble* tip)
    {
        hint.clear();
        if (tip != nullptr && tip->getTitle().isNotEmpty())
        {
            const auto& secs = tip->getSections();
            hint.push_back ({ tip->getTitle(), 0 });
            if (tip->getValues().isNotEmpty())
                hint.push_back ({ juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + tip->getValues(), 1 });
            if (! secs.empty())
                hint.push_back ({ juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 ")) + secs.front().text, 2 });
            hint.push_back ({ {}, -1 }); // line break
            for (size_t i = 1; i < secs.size(); ++i)
            {
                hint.push_back ({ juce::String (i > 1 ? "   " : "") + secs[i].label + ": ", secs[i].warning ? 4 : 3 });
                hint.push_back ({ secs[i].text.replace ("\n", " "), 2 });
            }
            if (tip->getFooter().isNotEmpty())
                hint.push_back ({ "   " + tip->getFooter(), 5 });
        }
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const bool small = getHeight() < 34;
        const float fsz = small ? 10.5f : 12.0f;
        g.fillAll (hint.empty() ? col::bg : juce::Colour (0xff1d2027));
        g.setColour (col::line);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        auto r = getLocalBounds().reduced (small ? 8 : 12, 0).withTrimmedRight (26 + 18);

        // right block: CPU and sample rate, latency
        const int latency = ctx.proc.getLatencySamples();
        const double fs = ctx.proc.getSampleRate();
        const double ms = fs > 0.0 ? 1000.0 * latency / fs : 0.0;
        juce::String r1 = "CPU " + juce::String (100.0f * ctx.proc.getCpuLoad(), 1) + "%";
        if (fs > 0.0)
            r1 << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << juce::String (fs / 1000.0, fs == 44100.0 || fs == 88200.0 ? 1 : 0) << " kHz";
        if (ctx.snap.droppedFrames > 0)
            r1 << "  analysis dropped " << juce::String ((juce::int64) ctx.snap.droppedFrames);
        const juce::String r2 = tr ("Latency") + " " + juce::String (latency) + " " + tr ("samples") + " (" + (ms < 10.0 ? juce::String (ms, 2) : juce::String (juce::roundToInt (ms))) + " ms)";
        const auto f = font (fsz);
        const int rw = juce::roundToInt (std::max (juce::GlyphArrangement::getStringWidth (f, r1), juce::GlyphArrangement::getStringWidth (f, r2))) + 4;
        auto right = r.removeFromRight (rw);
        r.removeFromRight (16);
        const int lineH = small ? 13 : 15;
        auto lines = right.withSizeKeepingCentre (right.getWidth(), 2 * lineH + 2);
        g.setColour (col::text3);
        g.setFont (f);
        g.drawText (r1, lines.removeFromTop (lineH), juce::Justification::centredRight, true);
        lines.removeFromTop (2);
        g.drawText (r2, lines.removeFromTop (lineH), juce::Justification::centredRight, true);
        bypassArea = r.withTrimmedLeft (r.getWidth() / 2);

        // left block: the hint, or the version
        auto left = r.withSizeKeepingCentre (r.getWidth(), 2 * lineH + 2);
        if (hint.empty())
        {
            g.setColour (col::text3);
            g.setFont (f);
            g.drawText (juce::String (mm::build::productName) + " " + mm::build::version + " (" + mm::build::gitHash + ")", left.removeFromTop (lineH), juce::Justification::centredLeft);
            left.removeFromTop (2);
            g.drawText (tr ("Hover over a knob or a button: its hint and advice appear here."), left.removeFromTop (lineH), juce::Justification::centredLeft, true);
            return;
        }
        g.setColour (col::magenta);
        g.fillRect (0, 1, 2, getHeight() - 1);
        auto line = left.removeFromTop (lineH);
        left.removeFromTop (2);
        int x = line.getX();
        bool full = false;
        const juce::Colour colours[] = { col::text, col::text2, col::text2, juce::Colour (0xffffb877), juce::Colour (0xffff8a95), col::text3 };
        for (const auto& run : hint)
        {
            if (run.style < 0)
            {
                line = left.removeFromTop (lineH);
                x = line.getX();
                full = false;
                continue;
            }
            if (full || run.text.isEmpty())
                continue;
            const auto rf = (run.style == 0 || run.style == 3 || run.style == 4) ? font (fsz, true) : font (fsz);
            g.setColour (colours[(size_t) run.style]);
            g.setFont (rf);
            const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (rf, run.text)) + 1;
            const int avail = line.getRight() - x;
            g.drawText (run.text, juce::Rectangle<int> (x, line.getY(), std::min (w, avail), lineH), juce::Justification::centredLeft, true);
            x += w;
            full = x >= line.getRight();
        }
    }

    juce::Rectangle<int> bypassArea;

    void resized() override { options.setBounds (getWidth() - 26 - 18, 2, 24, getHeight() - 4); }

    Context& ctx;
    Btn options { juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\x99")) };
    struct Run
    {
        juce::String text;
        int style = 0; // 0 title, 1 values, 2 text, 3 label, 4 warning label, 5 footer, -1 line break
    };
    std::vector<Run> hint;
};

// ---- Editor ---------------------------------------------------------------------------------------------
MasterMindEditor::MasterMindEditor (MasterMindProcessor& p) : AudioProcessorEditor (p), processorRef (p), ctx (p)
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    // analysis defaults from settings.json
    const auto a = ctx.settings.get ("analysis");
    if (a.isObject())
    {
        ctx.engine.fftOrder = juce::jlimit (12, 14, (int) a.getProperty ("fftOrder", 13));
        ctx.engine.smoothing = juce::jlimit (0, 4, (int) a.getProperty ("smoothing", 3));
        ctx.engine.tilt = (float) (double) a.getProperty ("tilt", 4.5);
        ctx.engine.channel = juce::jlimit (0, 4, (int) a.getProperty ("channel", 4));
    }
    processorRef.resetOnPlay = (bool) ctx.settings.get ("resetOnPlay", true);
    setLanguage (ctx.settings.get ("language", "en").toString() == "ru" ? Language::russian : Language::english);
    auto targets = ctx.engine.getTargets();
    targets.integratedLufs = (double) ctx.settings.get ("loudnessTarget", -14.0);
    ctx.engine.setTargets (targets);

    ctx.showTab = [this] (const juce::String& t) { showTab (t); };
    ctx.notify = [this] (const juce::String& m) { notify (m); };

    root = std::make_unique<Root>();
    addAndMakeVisible (*root);
    topBar = std::make_unique<TopBar> (ctx);
    tabBar = std::make_unique<TabBar> (ctx, *this);
    footer = std::make_unique<Footer> (ctx);
    spectrumCompact = std::make_unique<SpectrumPanel> (ctx, true);
    master = std::make_unique<MasterView> (ctx);
    eyes = std::make_unique<EyesView> (ctx);
    checksCompact = std::make_unique<ChecksPanel> (ctx, true);
    lowEnd = std::make_unique<LowEndView> (ctx);
    stereo = std::make_unique<StereoView> (ctx);
    spectrogram = std::make_unique<SpectrogramView> (ctx);
    albumView = std::make_unique<AlbumView> (ctx);
    refsView = std::make_unique<ReferencesView> (ctx);
    player = std::make_unique<RefPlayer> (ctx);
    for (juce::Component* c : std::initializer_list<juce::Component*> { topBar.get(), tabBar.get(), footer.get(), spectrumCompact.get(), master.get(),
                                                                         eyes.get(), checksCompact.get(), lowEnd.get(), stereo.get(), spectrogram.get(), albumView.get(), refsView.get(),
                                                                         player.get() })
        root->addChildComponent (c);
    topBar->onSizeButton = [this]
    {
        const auto next = mm::window::sizeAfterButtonClick (currentSize());
        setSize (next.width, next.height);
    };
    processorRef.onStateRestored = [this] { topBar->update(); repaint(); };

    setResizable (true, true);
    setResizeLimits (mm::window::minimumSize.width, mm::window::minimumSize.height, mm::window::maximumSize.width, mm::window::maximumSize.height);
    const auto stored = ctx.settings.loadWindowSize().value_or (mm::window::size100);
    const auto initial = mm::window::clampToWorkArea (stored, primaryWorkArea());
    lastSavedSize = stored;
    setSize (initial.width, initial.height);
    initialised = true;
    startTimerHz (30);
}

MasterMindEditor::~MasterMindEditor()
{
    stopTimer();
    processorRef.onStateRestored = nullptr;
    saveSizeIfChanged();
    processorRef.syncExtraState();
    root.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

juce::Component& MasterMindEditor::getRoot() noexcept { return *root; }

bool MasterMindEditor::isCompactTab (const juce::String& t) const
{
    return t == "Overview" || t == "Master" || t == "Eyes";
}

void MasterMindEditor::showTab (const juce::String& name)
{
    currentTab = name;
    layoutContent();
    tabBar->repaint();
}

void MasterMindEditor::notify (const juce::String& message)
{
    toast = message;
    toastUntil = juce::Time::getMillisecondCounterHiRes() + 4000.0;
    repaint();
}

void MasterMindEditor::selectModule (int index)
{
    master->selectModule (index);
}

void MasterMindEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void MasterMindEditor::paintOverChildren (juce::Graphics& g)
{
    // Bypass greys out the plugin below the top bar; the controls stay usable (D-036). The red
    // monitoring frame and the MONITOR ON badge were removed at the user's request.
    if (ctx.monitoring.chainBypass.load() && ! ctx.monitoring.referenceActive.load())
    {
        const float sc = layoutScale (getWidth(), getHeight());
        const int top = juce::roundToInt ((float) topBar->getBottom() * sc);
        const auto area = getLocalBounds().withTrimmedTop (top);
        g.setColour (juce::Colour (0xff3a3d44).withAlpha (0.62f));
        g.fillRect (area);
        // label in the middle of the footer, where it covers no control
        const auto f = font (12.5f * sc, true);
        const juce::String label = root->getWidth() >= 900 ? tr ("BYPASS: you hear the input") : juce::String ("BYPASS");
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, label) + 28.0f * sc);
        const auto fb = (footer->bypassArea + footer->getPosition()).toFloat() * sc;
        const auto pill = juce::Rectangle<int> (w, juce::roundToInt (22.0f * sc)).withCentre (fb.getCentre().roundToInt());
        g.setColour (juce::Colour (0xff23262c));
        g.fillRoundedRectangle (pill.toFloat(), pill.getHeight() * 0.5f);
        g.setColour (juce::Colour (0xffb9bec8));
        g.drawRoundedRectangle (pill.toFloat().reduced (0.5f), pill.getHeight() * 0.5f, 1.0f);
        g.setFont (f);
        g.drawText (label, pill, juce::Justification::centred);
    }
    if (toast.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() < toastUntil)
    {
        const auto f = font (13.0f * layoutScale (getWidth(), getHeight()));
        const int w = std::min (getWidth() - 40, juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, toast)) + 30);
        const juce::Rectangle<int> r ((getWidth() - w) / 2, getHeight() - 70, w, 32);
        g.setColour (col::tipBg);
        g.fillRoundedRectangle (r.toFloat(), 8.0f);
        g.setColour (col::tipText);
        g.setFont (f);
        g.drawFittedText (toast, r.reduced (12, 2), juce::Justification::centred, 2);
    }
}

void MasterMindEditor::resized()
{
    const float s = layoutScale (getWidth(), getHeight());
    mode = layoutModeFor (getWidth(), getHeight());
    const int lw = juce::roundToInt (getWidth() / s), lh = juce::roundToInt (getHeight() / s);
    root->setTransform (juce::AffineTransform::scale (s));
    root->setBounds (0, 0, lw, lh);

    const bool compact = mode == LayoutMode::compact;
    topBar->setMedium (mode == LayoutMode::medium);
    topBar->setCompact (compact || lw < 900); // narrow medium windows use the compact top bar
    topBar->setVisible (true);
    tabBar->setVisible (true);
    footer->setVisible (true);
    auto r = root->getLocalBounds();
    topBar->setBounds (r.removeFromTop (compact || lw < 900 ? 38 : 52));
    tabBar->setBounds (r.removeFromTop (compact ? 28 : 34));
    footer->setBounds (r.removeFromBottom (compact ? 30 : 38)); // two-line hint bar (mockup v18)
    tabBar->setTabs (compact ? juce::StringArray { "Overview", "Master", "Eyes", juce::String ("More ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")) }
                             : juce::StringArray { "Master", "Eyes", "Low-End", "Stereo", "Spectrogram", "Album", "References" });
    if (! compact && currentTab == "Overview")
        currentTab = "Master";
    const auto preset = mm::window::presetFor (currentSize());
    topBar->setSizeLabel (preset == mm::window::SizePreset::custom ? juce::String ("Custom") : juce::String (mm::window::presetName (preset)));
    layoutContent();
    if (initialised)
        saveDueAtMs = juce::Time::getMillisecondCounterHiRes() + saveDebounceMs;
}

void MasterMindEditor::layoutContent()
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { spectrumCompact.get(), master.get(), eyes.get(), checksCompact.get(),
                                                                         lowEnd.get(), stereo.get(), spectrogram.get(), albumView.get(), refsView.get() })
        c->setVisible (false);

    const bool compact = mode == LayoutMode::compact;
    auto r = root->getLocalBounds().withTrimmedTop (tabBar->getBottom()).withTrimmedBottom (compact ? 20 : 24).reduced (compact ? 6 : 8);
    const int gap = compact ? 6 : 8;
    const auto t = currentTab;

    // reference player while B plays (D-036)
    lastPlayer = ctx.monitoring.referenceActive.load();
    player->setVisible (lastPlayer);
    if (lastPlayer)
    {
        player->setCompact (compact);
        player->setBounds (r.removeFromTop (compact ? RefPlayer::heightCompact : RefPlayer::heightFull));
        r.removeFromTop (gap);
    }

    if (t == "Master")
    {
        master->setMode (compact ? MasterView::Mode::compact : (mode == LayoutMode::full ? MasterView::Mode::full : MasterView::Mode::medium));
        master->setBounds (r);
        master->setVisible (true);
    }
    else if (t == "Overview")
    {
        auto left = r.removeFromLeft (r.getWidth() * 61 / 100);
        spectrumCompact->setBounds (left);
        spectrumCompact->setVisible (true);
        r.removeFromLeft (gap);
        checksCompact->setBounds (r);
        checksCompact->setVisible (true);
    }
    else if (t == "Eyes")
    {
        eyes->setMedium (mode != LayoutMode::full);
        eyes->setBounds (r);
        eyes->setVisible (true);
    }
    else if (t == "Low-End")
    {
        lowEnd->setBounds (r);
        lowEnd->setVisible (true);
    }
    else if (t == "Stereo")
    {
        stereo->setBounds (r);
        stereo->setVisible (true);
    }
    else if (t == "Spectrogram")
    {
        spectrogram->setBounds (r);
        spectrogram->setVisible (true);
    }
    else if (t == "Album")
    {
        albumView->setBounds (r);
        albumView->setVisible (true);
    }
    else if (t == "References")
    {
        refsView->setCompact (compact);
        refsView->setBounds (r);
        refsView->setVisible (true);
    }
    else
    {
        currentTab = compact ? "Overview" : "Master";
        layoutContent();
        return;
    }
}

void MasterMindEditor::saveSizeIfChanged()
{
    saveDueAtMs = 0.0;
    const auto size = currentSize();
    if (size == lastSavedSize || size.width <= 0 || size.height <= 0)
        return;
    if (ctx.settings.saveWindowSize (size))
        lastSavedSize = size;
}

void MasterMindEditor::updateTooltip()
{
    // the tooltip of the control under the mouse goes into the hint bar at once (mockup v18)
    const auto mouse = juce::Desktop::getMousePosition();
    auto* under = juce::Desktop::getInstance().findComponentAt (mouse);
    juce::Component* source = nullptr;
    juce::String text;
    for (auto* c = under; c != nullptr && c != root.get(); c = c->getParentComponent())
        if (c->getProperties().contains (tipProperty) && c->getProperties()[tipProperty].toString().isNotEmpty())
        {
            source = c;
            text = c->getProperties()[tipProperty].toString();
            break;
        }
    if (source != nullptr && ! root->isParentOf (source))
        source = nullptr;
    if (source == tipSource && text == tipText)
        return;
    tipSource = source;
    tipText = text;
    if (source != nullptr)
    {
        fillTip (bubble, tipText, tipSource, ctx.apvts);
        footer->setHint (&bubble);
    }
    else
        footer->setHint (nullptr);
}

void MasterMindEditor::timerCallback()
{
    if (ctx.engine.copySnapshot (ctx.snap, lastSerial))
        lastSerial = ctx.snap.serial;
    ctx.engine.setTargets ([&]
    {
        auto t = ctx.refs.checkTargets();
        t.integratedLufs = ctx.engine.getTargets().integratedLufs;
        return t;
    }());
    ctx.monitoring.collectGarbage();

    if (ctx.monitoring.referenceActive.load() != lastPlayer)
    {
        layoutContent();
        repaint();
    }
    if (player->isVisible())
        player->update();
    if (const bool byp = ctx.monitoring.chainBypass.load(); byp != lastBypass)
    {
        lastBypass = byp;
        repaint();
    }
    ++frame;
    if (frame % 3 == 0)
        topBar->update();
    footer->repaint();
    if (spectrumCompact->isVisible())
        spectrumCompact->update();
    if (master->isVisible())
        master->update();
    if (eyes->isVisible())
        eyes->update();
    if (checksCompact->isVisible())
        checksCompact->repaint();
    if (lowEnd->isVisible())
        lowEnd->update();
    if (stereo->isVisible())
        stereo->update();
    if (spectrogram->isVisible())
        spectrogram->update();
    if (albumView->isVisible() && frame % 15 == 0)
        albumView->update();
    updateTooltip();
    if (toast.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() > toastUntil)
    {
        toast.clear();
        repaint();
    }
    if (saveDueAtMs > 0.0 && juce::Time::getMillisecondCounterHiRes() >= saveDueAtMs)
        saveSizeIfChanged();
}
