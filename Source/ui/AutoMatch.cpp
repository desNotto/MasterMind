#include "ui/AutoMatch.h"
#include "ui/Locale.h"

#include "analysis/TapAnalyzer.h"
#include "state/Parameters.h"

#include <thread>

namespace mm::ui
{
namespace
{
const char* const chipNames[3][AutoMatchStrip::numChips] = {
    { "Listen", "Analyse", "Formula", "Verify", "Apply" }, // Fast
    { "Listen", "Formula", "Search", "Verify", "Apply" },  // Hybrid
    { "Listen", "Analyse", "Search", "Verify", "Apply" },  // Precise
};
const char* const bandNames[6] = { "Sub", "Bass", "Low-mid", "Mid", "Presence", "Air" };
} // namespace

int AutoMatchStrip::loudestWindow (const juce::AudioBuffer<float>& audio, int length)
{
    const int n = audio.getNumSamples();
    if (n <= length)
        return 0;
    // energy per 10 ms block, then the window with the largest sum
    const int block = std::max (1, length / 800);
    const int blocks = n / block;
    std::vector<double> e ((size_t) blocks, 0.0);
    for (int b = 0; b < blocks; ++b)
        for (int i = b * block; i < (b + 1) * block; ++i)
        {
            const double m = 0.5 * (audio.getSample (0, i) + audio.getSample (std::min (1, audio.getNumChannels() - 1), i));
            e[(size_t) b] += m * m;
        }
    const int win = length / block;
    double sum = 0.0;
    for (int b = 0; b < win && b < blocks; ++b)
        sum += e[(size_t) b];
    double best = sum;
    int bestStart = 0;
    for (int b = win; b < blocks; ++b)
    {
        sum += e[(size_t) b] - e[(size_t) (b - win)];
        if (sum > best)
        {
            best = sum;
            bestStart = b - win + 1;
        }
    }
    return std::min (bestStart * block, n - length);
}

AutoMatchStrip::AutoMatchStrip (Context& c) : ctx (c)
{
    for (auto* b : { &go, &undoBtn, &stopBtn })
        addChildComponent (*b);
    addAndMakeVisible (modeSel);
    go.setVisible (true);
    undoBtn.setVisible (true);
    go.accent = true;
    setTip (go, "c:am.go");
    setTip (undoBtn, "c:am.undo");
    setTip (modeSel, "c:am.mode");
    setTip (stopBtn, "t:Stop\nStops Auto match. The knobs keep their positions. Undo restores them.");
    modeSel.setSelected (juce::jlimit (0, 2, (int) ctx.settings.get ("automatch_mode", 1)), false);
    modeSel.onChange = [this] (int i) { ctx.settings.set ("automatch_mode", i); };
    go.onClick = [this] { start(); };
    stopBtn.onClick = [this] { stop (tr ("Stopped. Undo restores the knobs.")); };
    undoBtn.onClick = [this] { undo(); };
    startTimerHz (10);
}

AutoMatchStrip::~AutoMatchStrip()
{
    stopTimer();
    if (phase == Phase::listening)
        ctx.engine.cancelCapture();
    matcher.reset(); // stops the search thread
}

bool AutoMatchStrip::hasReference() const { return ctx.refs.target().count > 0; }

juce::String AutoMatchStrip::referenceText() const
{
    int s = ctx.refs.selected();
    if (s < 0 || ctx.refs.slot (s).state != reference::ReferenceLibrary::State::ready)
        for (int i = 0; i < reference::ReferenceLibrary::numSlots; ++i)
            if (ctx.refs.slot (i).state == reference::ReferenceLibrary::State::ready && ctx.refs.slot (i).inSet)
            {
                s = i;
                break;
            }
    if (s < 0 || ctx.refs.slot (s).state != reference::ReferenceLibrary::State::ready)
        return {};
    return juce::String (s + 1) + "|" + ctx.refs.slot (s).file.getFileName();
}

double AutoMatchStrip::referenceLufs() const
{
    const auto t = referenceText();
    if (t.isEmpty())
        return ctx.refs.target().integrated;
    const int s = t.upToFirstOccurrenceOf ("|", false, false).getIntValue() - 1;
    const auto& a = ctx.refs.slot (s).analysis;
    return a.has_value() ? a->integrated : ctx.refs.target().integrated;
}

automatch::Target AutoMatchStrip::buildTarget() const
{
    // tonal balance and corridor as the checks show them; dynamics, width, harshness from the set;
    // the loudness of the selected reference
    const auto rt = ctx.refs.target();
    const auto ct = ctx.refs.checkTargets();
    automatch::Target t;
    for (size_t b = 0; b < 6; ++b)
    {
        t.tonal[b] = ct.tonal[b];
        t.corridor[b] = ct.corridor[b];
        t.crest[b] = rt.crest[b];
    }
    for (size_t b = 0; b < 4; ++b)
        t.width[b] = rt.width[b];
    t.hasCrest = t.hasWidth = t.hasHarshness = rt.count > 0;
    t.harshness = rt.harshness;
    t.lufs = referenceLufs();
    t.ceiling = std::min (ctx.param ("lim_ceiling"), -1.0f);
    return t;
}

void AutoMatchStrip::start()
{
    if (! hasReference())
    {
        if (ctx.notify)
            ctx.notify (tr ("Load a reference first (References tab)."));
        return;
    }
    // Undo snapshot of every parameter
    snapshot.clear();
    for (auto* p : ctx.proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            snapshot[rp->paramID] = rp->getValue();
    // record our own song, not the reference
    ctx.monitoring.referenceActive = false;
    ctx.monitoring.refPlaying = false;
    ctx.engine.startCapture (recordSeconds);
    runMode = mode();
    phase = Phase::listening;
    failed = false;
    startedAt = juce::Time::getMillisecondCounterHiRes();
    lastCapture = 0.0;
    captureStill = 0.0;
    shownProgress = 0.0;
    status = tr ("Listening to the song");
    resized();
}

void AutoMatchStrip::launch (juce::AudioBuffer<float> rec)
{
    const double fs = ctx.engine.getSampleRate();
    const int length = (int) (excerptSeconds * fs), pre = (int) (prerollSeconds * fs);
    const int win = loudestWindow (rec, length);
    const int from = std::max (0, win - pre);
    const int n = std::min (rec.getNumSamples() - from, length + (win - from));
    juce::AudioBuffer<float> excerpt (2, n);
    for (int ch = 0; ch < 2; ++ch)
        excerpt.copyFrom (ch, 0, rec, std::min (ch, rec.getNumChannels() - 1), from, n);
    // input loudness for the gain staging
    analysis::TapAnalyzer a;
    a.setLightweight (true);
    a.prepare (fs, 12);
    a.process (excerpt.getReadPointer (0), excerpt.getReadPointer (1), n);
    automatch::ParamMap current;
    for (auto* p : ctx.proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            current[rp->paramID] = rp->convertFrom0to1 (rp->getValue());
    const auto fixed = automatch::fixedSetup (current, a.loudness().integrated());
    const int threads = (int) std::max (1u, std::thread::hardware_concurrency()) - 2;
    matcher = std::make_unique<automatch::Matcher> (runMode, fs, current, fixed, std::move (excerpt), win - from, buildTarget(), threads);
    matcher->startThread (juce::Thread::Priority::low);
    phase = Phase::running;
    runStartedAt = juce::Time::getMillisecondCounterHiRes();
    status = {};
    resized();
}

void AutoMatchStrip::stop (const juce::String& message)
{
    if (phase == Phase::listening)
        ctx.engine.cancelCapture();
    if (matcher != nullptr)
    {
        matcher->signalThreadShouldExit();
        matcher.reset();
    }
    phase = Phase::done;
    failed = true;
    status = message;
    resized();
    repaint();
}

void AutoMatchStrip::undo()
{
    if (snapshot.empty())
        return;
    for (auto* p : ctx.proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (const auto it = snapshot.find (rp->paramID); it != snapshot.end() && std::abs (rp->getValue() - it->second) > 1.0e-6f)
            {
                rp->beginChangeGesture();
                rp->setValueNotifyingHost (it->second);
                rp->endChangeGesture();
            }
    snapshot.clear();
    phase = Phase::idle;
    status.clear();
    resized();
    repaint();
    if (ctx.notify)
        ctx.notify (tr ("Auto match undone: every knob is back."));
}

void AutoMatchStrip::finish()
{
    auto& m = *matcher;
    for (const auto& [id, v] : m.result)
        ctx.setParam (id, v);
    // plain-language summary: what changed and what the twin measured
    const auto t = buildTarget();
    const auto& f = m.finalFeatures;
    int green = 0;
    juce::StringArray off;
    for (size_t b = 0; b < 6; ++b)
    {
        const double dev = f.tonal[b] - t.tonal[b];
        if (std::abs (dev) <= std::max (1.0, t.corridor[b]))
            ++green;
        else
            off.add (juce::String (bandNames[b]) + " " + fmtSigned (dev));
    }
    juce::String msg = tr ("Done in") + " " + clockText ((juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0) + ". ";
    msg << tr ("Tonal balance") << " " << green << "/6, " << fmtMinus (f.lufs) << " LUFS";
    msg << ", Drive " << juce::String (m.result["lim_drive"], 1) << " dB, Inflator " << juce::roundToInt (m.result["infl_effect"]) << "%.";
    if (! off.isEmpty())
        msg << " " << tr ("Off target:") << " " << off.joinIntoString (", ") << " dB.";
    status = msg;
    matcher.reset();
    phase = Phase::done;
    shownProgress = 1.0;
    resized();
}

juce::String AutoMatchStrip::clockText (double s) const
{
    const int t = juce::roundToInt (s);
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}

void AutoMatchStrip::timerCallback()
{
    const bool running = phase == Phase::listening || phase == Phase::running;
    go.setEnabled (hasReference() && ! running);
    undoBtn.setEnabled (! snapshot.empty() && ! running);
    modeSel.setEnabled (! running);
    if (phase == Phase::listening)
    {
        const double p = ctx.engine.captureProgress();
        captureStill = p > lastCapture ? 0.0 : captureStill + 0.1;
        lastCapture = p;
        status = captureStill > 1.5 ? tr ("Waiting for audio. Start playback.")
                                    : tr ("Recording the song") + " " + juce::String (juce::roundToInt (p * recordSeconds)) + " / " + juce::String ((int) recordSeconds) + " s";
        if (p >= 1.0)
        {
            auto rec = ctx.engine.takeCapture();
            if (rec.getNumSamples() > 0)
                launch (std::move (rec));
        }
    }
    else if (phase == Phase::running && matcher != nullptr)
    {
        auto& m = *matcher;
        if (m.done.load())
            finish();
        else if (! m.isThreadRunning())
            stop (tr ("Stopped. Undo restores the knobs."));
        else
        {
            const int ev = m.evals.load(), planned = std::max (1, m.plannedEvals.load());
            const double elapsed = (juce::Time::getMillisecondCounterHiRes() - runStartedAt) / 1000.0;
            const double left = ev > 0 ? elapsed / ev * std::max (0, planned - ev) : 0.0;
            const int st = m.stage.load();
            juce::String what = st == 2 ? tr ("Search:") + " " + juce::String (ev) + " " + tr ("of") + " " + juce::String (planned) + " " + tr ("variants")
                                        : (st == 3 ? tr ("Final check with your limiter") : tr ("Measuring and setting the knobs"));
            if (m.startCost.load() > 0.0f)
                what << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << tr ("distance") << " " << juce::String (m.startCost.load(), 1)
                     << juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) << juce::String (m.bestCost.load(), 1);
            if (ev > 2)
                what << juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) << tr ("about") << " " << clockText (left) << " " << tr ("left");
            status = what;
        }
    }
    repaint();
}

int AutoMatchStrip::chip() const
{
    if (phase == Phase::listening)
        return 0;
    if (phase != Phase::running || matcher == nullptr)
        return -1;
    const int st = matcher->stage.load();
    if (st >= 3)
        return 3;
    if (st == 2)
        return 2;
    // stage 1: Fast shows Analyse for the neutral pass, then Formula
    return (runMode == automatch::Mode::fast && matcher->evals.load() >= 1) ? 2 : 1;
}

double AutoMatchStrip::progress()
{
    if (phase == Phase::idle)
        return 0.0;
    if (phase == Phase::done)
        return failed ? shownProgress : 1.0;
    double p = 0.0;
    if (phase == Phase::listening)
        p = 0.15 * ctx.engine.captureProgress();
    else if (matcher != nullptr)
        p = 0.15 + 0.84 * std::clamp ((double) matcher->evals.load() / std::max (1, matcher->plannedEvals.load()), 0.0, 1.0);
    shownProgress = std::max (shownProgress, std::min (p, 0.99));
    return shownProgress;
}

// ---- drawing -------------------------------------------------------------------------------------------
void AutoMatchStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 5);
    barArea = {};
    if (phase != Phase::idle)
    {
        // progress bar along the bottom of the strip (D-036)
        barArea = r.removeFromBottom (4).translated (0, 2);
        r.removeFromBottom (2);
    }
    const bool running = phase == Phase::listening || phase == Phase::running;
    stopBtn.setVisible (running);
    go.setVisible (! running && phase != Phase::done);
    undoBtn.setVisible (! running);
    if (running)
        stopBtn.setBounds (r.removeFromRight (56));
    else
    {
        undoBtn.setBounds (r.removeFromRight (56));
        r.removeFromRight (6);
        if (go.isVisible())
        {
            go.setBounds (r.removeFromRight (100));
            r.removeFromRight (6);
        }
    }
    r.removeFromRight (4);
    chipsArea = statusArea = hintArea = {};
    const int modeW = std::max (150, modeSel.preferredWidth (12.5f));
    if (phase == Phase::idle)
    {
        // idle: reference on the left, the mode switch, "play, then press" next to the button (mockup v18)
        hintArea = r.removeFromRight (std::min (190, std::max (0, r.getWidth() - modeW - 260)));
        r.removeFromRight (8);
        modeSel.setBounds (r.removeFromRight (modeW).withSizeKeepingCentre (modeW, 24));
        r.removeFromRight (8);
        refArea = r;
        return;
    }
    const int refW = std::min (300, r.getWidth() / 4);
    refArea = r.removeFromLeft (refW);
    r.removeFromLeft (8);
    modeSel.setBounds (r.removeFromLeft (modeW).withSizeKeepingCentre (modeW, 24));
    r.removeFromLeft (8);
    const int chipsW = numChips * 72;
    if (r.getWidth() > chipsW + 120)
        chipsArea = r.removeFromLeft (chipsW);
    statusArea = r.withTrimmedLeft (8);
}

void AutoMatchStrip::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    const auto ref = referenceText();
    auto r = refArea;
    if (ref.isEmpty())
    {
        g.setColour (col::text3);
        g.setFont (font (12.5f));
        g.drawText (tr ("No reference. Load songs on the References tab."), r, juce::Justification::centredLeft, true);
    }
    else
    {
        const auto label = tr ("Reference:") + " ";
        g.setColour (col::text);
        g.setFont (semibold (13.0f));
        const int lw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (semibold (13.0f), label));
        g.drawText (label, r.removeFromLeft (lw), juce::Justification::centredLeft);
        g.setColour (col::magenta);
        g.drawText (ref.upToFirstOccurrenceOf ("|", false, false), r.removeFromLeft (14), juce::Justification::centredLeft);
        const auto name = ref.fromFirstOccurrenceOf ("|", false, false);
        const auto info = fmtMinus (referenceLufs()) + " LUFS";
        const int iw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.0f), info)) + 8;
        g.setColour (col::text);
        g.drawText (name, r.withTrimmedRight (iw), juce::Justification::centredLeft, true);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (info, r.removeFromRight (iw), juce::Justification::centredRight);
    }
    if (! barArea.isEmpty())
    {
        const auto bar = barArea.toFloat();
        g.setColour (col::sunk);
        g.fillRoundedRectangle (bar, 2.0f);
        const bool running = phase == Phase::listening || phase == Phase::running;
        const double p = progress();
        g.setColour (running ? col::blue : (p >= 1.0 ? col::ok : col::text3));
        g.fillRoundedRectangle (bar.withWidth (std::max (4.0f, bar.getWidth() * (float) p)), 2.0f);
    }
    if (! hintArea.isEmpty())
    {
        g.setColour (col::text3);
        g.setFont (font (12.5f));
        g.drawText (tr ("Play the song, then press:"), hintArea, juce::Justification::centredRight, true);
    }
    if (! chipsArea.isEmpty())
    {
        auto c = chipsArea;
        const int now = chip();
        for (int i = 0; i < numChips; ++i)
        {
            const bool ok = phase == Phase::done ? ! failed : (now >= 0 && i < now);
            const bool running = i == now;
            auto chipR = c.removeFromLeft (68).withSizeKeepingCentre (68, 22).toFloat();
            c.removeFromLeft (4);
            g.setColour (running ? juce::Colour (0xff1d3352) : col::sunk);
            g.fillRoundedRectangle (chipR, 11.0f);
            g.setColour (running ? col::blue : (ok ? juce::Colour (0xff285c45) : col::line2));
            g.drawRoundedRectangle (chipR.reduced (0.5f), 11.0f, 1.0f);
            g.setColour (ok ? col::ok : (running ? juce::Colours::white : col::text3));
            g.setFont (font (11.5f, running));
            const juce::String mark = ok ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93 ")) : juce::String();
            g.drawText (mark + tr (chipNames[(int) runMode][i]), chipR, juce::Justification::centred, true);
        }
    }
    if (! statusArea.isEmpty())
    {
        juce::String text = status;
        if (phase == Phase::listening || phase == Phase::running)
            text = juce::String (juce::roundToInt (100.0 * progress())) + "%  " + text;
        g.setColour (col::text2);
        g.setFont (font (12.5f));
        g.drawText (text, statusArea, juce::Justification::centredLeft, true);
    }
}
} // namespace mm::ui
