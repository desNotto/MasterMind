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
        // reference player (shown while B is active)
        for (auto* b : { &play, &restart })
            addChildComponent (*b);
        addChildComponent (position);
        position.setSliderStyle (juce::Slider::LinearHorizontal);
        position.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        position.setRange (0.0, 1.0);
        position.setColour (juce::Slider::trackColourId, col::magenta);
        position.onDragEnd = [this] { ctx.monitoring.seekRequest = position.getValue() * ctx.monitoring.refLengthSeconds.load(); };
        setTip (position, "t:Reference position\nDrag to seek in the reference. Right-click a reference button for loop and loudness matching.");
        play.onClick = [this] { ctx.monitoring.refPlaying = ! ctx.monitoring.refPlaying.load(); };
        restart.onClick = [this] { ctx.monitoring.seekRequest = 0.0; ctx.monitoring.refPlaying = true; };
        setTip (play, "t:Play / pause the reference");
        setTip (restart, "t:Play the reference from the start");
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
        if (! ctx.monitoring.referenceActive.load())
        {
            g.setColour (col::text2);
            g.setFont (font (13.0f));
            resetOnPlay.setButtonText (tr ("Reset on play"));
            g.drawText (tr ("Analyse"), analyse.getBounds().translated (-62, 0).withWidth (58), juce::Justification::centredRight);
        }
        else
        {
            const auto* ref = ctx.monitoring.currentReferenceForUi();
            g.setColour (col::magenta);
            g.setFont (font (12.5f, true));
            const double len = ctx.monitoring.refLengthSeconds.load(), pos = ctx.monitoring.refPositionSeconds.load();
            const auto tm = [] (double s) { return juce::String ((int) s / 60) + ":" + juce::String ((int) s % 60).paddedLeft ('0', 2); };
            g.drawText ("B: " + (ref != nullptr ? ref->name : juce::String ("loading...")) + "  " + tm (pos) + " / " + tm (len) + "  match "
                            + fmtSigned (ctx.monitoring.appliedRefGainDb.load()) + " dB",
                        playInfo, juce::Justification::centredRight, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 3);
        resetOnPlay.setBounds (r.removeFromRight (118));
        r.removeFromRight (6);
        reset.setBounds (r.removeFromRight (56));
        r.removeFromRight (6);
        analyse.setBounds (r.removeFromRight (130));
        auto pr = getLocalBounds().reduced (8, 3);
        restart.setBounds (pr.removeFromRight (30));
        pr.removeFromRight (4);
        play.setBounds (pr.removeFromRight (30));
        pr.removeFromRight (8);
        position.setBounds (pr.removeFromRight (std::min (240, pr.getWidth() / 3)));
        pr.removeFromRight (8);
        playInfo = pr.removeFromRight (std::min (360, pr.getWidth() / 2));
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (const auto& [name, r] : tabAreas)
            if (r.contains (e.getPosition()))
            {
                if (name.endsWith (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe"))))
                {
                    juce::PopupMenu m;
                    const juce::StringArray more { "Low-End", "Stereo", "Spectrogram", "Album" };
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
        const bool refOn = ctx.monitoring.referenceActive.load();
        analyse.setVisible (! refOn);
        reset.setVisible (! refOn);
        resetOnPlay.setVisible (! refOn);
        play.setVisible (refOn);
        restart.setVisible (refOn);
        position.setVisible (refOn);
        play.setButtonText (ctx.monitoring.refPlaying.load() ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9d\x9a\xe2\x9d\x9a")) : juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6")));
        restart.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x8f\xae")));
        const double len = ctx.monitoring.refLengthSeconds.load();
        if (refOn && len > 0 && ! position.isMouseButtonDown())
            position.setValue (ctx.monitoring.refPositionSeconds.load() / len, juce::dontSendNotification);
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
    Btn play { "Play" }, restart { "|<" };
    juce::Slider position;
    juce::Rectangle<int> playInfo;
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

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        g.setColour (col::line);
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
        auto r = getLocalBounds().reduced (10, 0).withTrimmedRight (26 + 18);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (juce::String (mm::build::productName) + " " + mm::build::version + " (" + mm::build::gitHash + ")", r, juce::Justification::centredLeft);
        const int latency = ctx.proc.getLatencySamples();
        const double fs = ctx.proc.getSampleRate();
        const double ms = fs > 0.0 ? 1000.0 * latency / fs : 0.0;
        juce::String right = "CPU " + juce::String (100.0f * ctx.proc.getCpuLoad(), 1) + "%    " + tr ("Latency") + " " + juce::String (latency) + " " + tr ("samples") + " (" + juce::String (ms, 2) + " ms)";
        if (fs > 0.0)
            right << "    " << juce::String (fs / 1000.0, fs == 44100.0 || fs == 88200.0 ? 1 : 0) << " kHz";
        if (ctx.snap.droppedFrames > 0)
            right << "    analysis dropped " << juce::String ((juce::int64) ctx.snap.droppedFrames);
        g.drawText (right, r, juce::Justification::centredRight);
    }

    void resized() override { options.setBounds (getWidth() - 26 - 18, 2, 24, getHeight() - 4); }

    Context& ctx;
    Btn options { juce::String (juce::CharPointer_UTF8 ("\xe2\x9a\x99")) };
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
    ctx.showModuleDetail = [this] (const juce::String& k) { showModuleDetail (k); };
    ctx.notify = [this] (const juce::String& m) { notify (m); };

    root = std::make_unique<Root>();
    addAndMakeVisible (*root);
    topBar = std::make_unique<TopBar> (ctx);
    tabBar = std::make_unique<TabBar> (ctx, *this);
    footer = std::make_unique<Footer> (ctx);
    spectrum = std::make_unique<SpectrumPanel> (ctx, false);
    spectrumCompact = std::make_unique<SpectrumPanel> (ctx, true);
    strip = std::make_unique<ModuleStrip> (ctx);
    stripViewport = std::make_unique<juce::Viewport>();
    stripViewport->setScrollBarsShown (false, true);
    eyes = std::make_unique<EyesView> (ctx);
    checksCompact = std::make_unique<ChecksPanel> (ctx, true);
    lowEnd = std::make_unique<LowEndView> (ctx);
    stereo = std::make_unique<StereoView> (ctx);
    spectrogram = std::make_unique<SpectrogramView> (ctx);
    albumView = std::make_unique<AlbumView> (ctx);
    for (juce::Component* c : std::initializer_list<juce::Component*> { topBar.get(), tabBar.get(), footer.get(), spectrum.get(), spectrumCompact.get(), strip.get(),
                                                                         stripViewport.get(), eyes.get(), checksCompact.get(), lowEnd.get(), stereo.get(),
                                                                         spectrogram.get(), albumView.get() })
        root->addChildComponent (c);
    root->addChildComponent (bubble);
    bubble.setInterceptsMouseClicks (false, false);
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
    detail.reset();
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

void MasterMindEditor::showModuleDetail (const juce::String& key)
{
    detail = std::make_unique<ModuleDetail> (ctx, key);
    detail->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MasterMindEditor> (this)] { if (safe) safe->detail.reset(); }); };
    root->addAndMakeVisible (*detail);
    detail->setBounds (root->getLocalBounds());
    bubble.toFront (false);
}

void MasterMindEditor::paint (juce::Graphics& g) { g.fillAll (col::bg); }

void MasterMindEditor::paintOverChildren (juce::Graphics& g)
{
    // red frame while monitoring is active (spec 6.0 rule 3)
    if (processorRef.monitorActive.load())
    {
        g.setColour (col::mon);
        g.drawRect (getLocalBounds(), 2);
        g.fillRect (0, 0, getWidth(), 3);
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
    footer->setBounds (r.removeFromBottom (compact ? 20 : 24));
    tabBar->setTabs (compact ? juce::StringArray { "Overview", "Master", "Eyes", juce::String ("More ") + juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe")) }
                             : juce::StringArray { "Master", "Eyes", "Low-End", "Stereo", "Spectrogram", "Album" });
    if (! compact && currentTab == "Overview")
        currentTab = "Master";
    const auto preset = mm::window::presetFor (currentSize());
    topBar->setSizeLabel (preset == mm::window::SizePreset::custom ? juce::String ("Custom") : juce::String (mm::window::presetName (preset)));
    layoutContent();
    if (detail != nullptr)
        detail->setBounds (root->getLocalBounds());
    if (initialised)
        saveDueAtMs = juce::Time::getMillisecondCounterHiRes() + saveDebounceMs;
}

void MasterMindEditor::layoutContent()
{
    for (juce::Component* c : std::initializer_list<juce::Component*> { spectrum.get(), spectrumCompact.get(), stripViewport.get(), eyes.get(), checksCompact.get(),
                                                                         lowEnd.get(), stereo.get(), spectrogram.get(), albumView.get() })
        c->setVisible (false);
    if (stripViewport->getViewedComponent() == strip.get())
        stripViewport->setViewedComponent (nullptr, false);
    root->addChildComponent (*strip);
    strip->setVisible (false);

    const bool compact = mode == LayoutMode::compact;
    auto r = root->getLocalBounds().withTrimmedTop (tabBar->getBottom()).withTrimmedBottom (compact ? 20 : 24).reduced (compact ? 6 : 8);
    const int gap = compact ? 6 : 8;
    const auto t = currentTab;

    if (t == "Master" && ! compact)
    {
        const int stripH = mode == LayoutMode::full ? 310 : std::max (220, r.getHeight() * 46 / 100);
        auto bottom = r.removeFromBottom (stripH);
        r.removeFromBottom (gap);
        spectrum->setBounds (r);
        spectrum->setVisible (true);
        if (bottom.getWidth() >= 1240)
        {
            strip->setBounds (bottom);
            strip->setVisible (true);
        }
        else
        {
            stripViewport->setBounds (bottom);
            stripViewport->setViewedComponent (strip.get(), false);
            strip->setBounds (0, 0, 1260, bottom.getHeight() - 8);
            strip->setVisible (true);
            stripViewport->setVisible (true);
        }
    }
    else if (t == "Master" && compact)
    {
        stripViewport->setBounds (r);
        stripViewport->setViewedComponent (strip.get(), false);
        strip->setBounds (0, 0, 1260, std::max (260, r.getHeight() - 8));
        strip->setVisible (true);
        stripViewport->setVisible (true);
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
    else
    {
        currentTab = compact ? "Overview" : "Master";
        layoutContent();
        return;
    }
    bubble.toFront (false);
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
    const bool buttonDown = juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (source != tipSource || text != tipText || buttonDown)
    {
        tipSource = buttonDown ? nullptr : source;
        tipText = text;
        tipSince = now;
        if (tipShown)
        {
            bubble.setVisible (false);
            tipShown = false;
        }
        return;
    }
    if (tipSource != nullptr && ! tipShown && now - tipSince >= 500.0)
    {
        fillTip (bubble, tipText, tipSource, ctx.apvts);
        const auto area = root->getLocalArea (tipSource, tipSource->getLocalBounds());
        // for large components (panels), anchor the bubble at the mouse position
        auto target = area;
        if (area.getWidth() > 220 || area.getHeight() > 120)
        {
            const auto p = root->getLocalPoint (nullptr, mouse);
            target = juce::Rectangle<int> (p.x - 4, p.y - 4, 8, 8);
        }
        bubble.place (target, root->getLocalBounds());
        bubble.setVisible (true);
        bubble.toFront (false);
        tipShown = true;
    }
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

    const bool monitor = processorRef.monitorActive.load();
    if (monitor != lastMonitor)
    {
        lastMonitor = monitor;
        topBar->resized();
        repaint();
    }
    ++frame;
    if (frame % 3 == 0)
        topBar->update();
    footer->repaint();
    if (spectrum->isVisible())
        spectrum->update();
    if (spectrumCompact->isVisible())
        spectrumCompact->update();
    if (strip->isVisible() && frame % 2 == 0)
        strip->update();
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
