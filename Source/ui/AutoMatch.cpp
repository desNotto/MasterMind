#include "ui/AutoMatch.h"
#include "ui/Locale.h"

#include "state/Parameters.h"

namespace mm::ui
{
namespace
{
constexpr double measureSeconds = 4.0; // signal time per measurement (after an analysis reset)
constexpr double greenDb = 0.9;        // a little inside the green limit of 1 dB
constexpr int maxBandAttempts = 5, maxLoudAttempts = 6, maxWidthAttempts = 3;
constexpr float inputTargetLufs = -16.0f; // gain staging before the dynamics

/** EQ band that corrects each tonal band (D-031). */
struct BandMap
{
    int band, type;
    float freq, q;
};
const BandMap bandMap[6] = {
    { 1, 0, 60.0f, 0.71f },    // Sub: band 1 low shelf
    { 2, 0, 95.0f, 1.0f },     // Bass: bell
    { 3, 0, 270.0f, 0.9f },    // Low-mid: bell
    { 4, 0, 1000.0f, 0.7f },   // Mid: bell
    { 5, 0, 3500.0f, 0.9f },   // Presence: bell
    { 10, 0, 8000.0f, 0.71f }, // Air: band 10 high shelf
};
const char* const stepNames[AutoMatchStrip::numSteps] = { "Input", "Sub", "Bass", "L-mid", "Mid", "Pres.", "Air", "Dynamics", "Width", "Loud", "Checks" };
const char* const bandNames[6] = { "Sub", "Bass", "Low-mid", "Mid", "Presence", "Air" };
constexpr int stepInput = 0, stepBand0 = 1, stepDynamics = 7, stepWidth = 8, stepLoud = 9, stepChecks = 10;

juce::String clock (double s)
{
    const int t = juce::roundToInt (s);
    return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
}
} // namespace

int AutoMatchStrip::nextBand (const std::array<double, 6>& dev, int& current, double green)
{
    for (;;)
    {
        // the lowest band that is not green, up to the current band, comes first
        for (int i = 0; i <= current; ++i)
            if (std::abs (dev[(size_t) i]) > green)
                return i;
        if (current >= 5)
            return -1;
        ++current;
    }
}

AutoMatchStrip::AutoMatchStrip (Context& c) : ctx (c)
{
    for (auto* b : { &go, &undoBtn, &stopBtn })
        addChildComponent (*b);
    go.setVisible (true);
    undoBtn.setVisible (true);
    go.accent = true;
    setTip (go, "c:am.go");
    setTip (undoBtn, "c:am.undo");
    setTip (stopBtn, "t:Stop\nStops Auto match. The knobs keep their current positions. Undo restores them.");
    go.onClick = [this] { start(); };
    stopBtn.onClick = [this] { stop ("Stopped. Undo restores the knobs."); };
    undoBtn.onClick = [this] { undo(); };
    startTimerHz (10);
}

AutoMatchStrip::~AutoMatchStrip() { stopTimer(); }

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

std::array<double, 6> AutoMatchStrip::deviations() const
{
    std::array<double, 6> d {};
    for (int b = 0; b < 6; ++b)
        d[(size_t) b] = ctx.snap.out.tonalIntegrated[(size_t) b] - ctx.snap.targets.tonal[(size_t) b];
    return d;
}

void AutoMatchStrip::start()
{
    if (! hasReference())
    {
        if (ctx.notify)
            ctx.notify ("Load a reference first (References tab or the buttons 1-6).");
        return;
    }
    // Undo snapshot of every parameter
    snapshot.clear();
    for (auto* p : ctx.proc.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            snapshot[rp->paramID] = rp->getValue();
    ctx.monitoring.referenceActive = false; // measure our own mix (A)
    steps.fill (StepState::pending);
    bandAttempts.fill (0);
    currentBand = loudAttempts = checkStage = cycles = dynStage = widthAttempts = 0;
    dynamicsDone = widthDone = loudnessDone = false;

    // Input: subsonic filter on, trim measured below
    ctx.setParam ("in_on", 1.0f);
    ctx.setParam ("hpf_on", 1.0f);
    ctx.setParam ("hpf_freq", 25.0f);
    ctx.setParam ("in_trim", 0.0f);
    setupBands();
    phase = Phase::input;
    steps[stepInput] = StepState::running;
    startedAt = juce::Time::getMillisecondCounterHiRes();
    status = "Measuring the input level";
    restartMeasurement();
    resized();
}

void AutoMatchStrip::setupBands()
{
    ctx.setParam ("eq_on", 1.0f);
    for (const auto& m : bandMap)
    {
        ctx.setParam (params::eq (m.band, "on"), 1.0f);
        ctx.setParam (params::eq (m.band, "type"), (float) m.type);
        ctx.setParam (params::eq (m.band, "freq"), m.freq);
        ctx.setParam (params::eq (m.band, "q"), m.q);
        ctx.setParam (params::eq (m.band, "gain"), 0.0f);
        ctx.setParam (params::eq (m.band, "dyn"), 0.0f);
        ctx.setParam (params::eq (m.band, "place"), 0.0f);
    }
    ctx.setParam ("lim_on", 1.0f);
    ctx.setParam ("lim_ceiling", std::min (ctx.param ("lim_ceiling"), -1.0f));
}

void AutoMatchStrip::restartMeasurement()
{
    ctx.engine.requestReset();
    waited = 0.0;
    meters = {};
}

void AutoMatchStrip::stop (const juce::String& message)
{
    phase = Phase::done;
    for (auto& s : steps)
        if (s == StepState::running)
            s = StepState::pending;
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
    steps.fill (StepState::pending);
    resized();
    repaint();
    if (ctx.notify)
        ctx.notify ("Auto match undone: every knob is back.");
}

void AutoMatchStrip::timerCallback()
{
    const bool running = phase != Phase::idle && phase != Phase::done;
    go.setEnabled (hasReference() && ! running);
    undoBtn.setEnabled (! snapshot.empty() && ! running);
    if (! running)
    {
        repaint();
        return;
    }
    if (ctx.snap.out.momentary > -50.0)
    {
        waited += 0.1;
        noSignalFor = 0.0;
        // sample the dynamics meters for the averages of this measurement
        const auto& chain = ctx.proc.getChain();
        ++meters.n;
        for (int b = 0; b < 5; ++b)
        {
            meters.mbGr[(size_t) b] += -chain.getMultiband().gainReductionDb (b);
            const float lv = chain.getMultiband().bandLevelDb (b);
            if (lv > -99.0f)
            {
                meters.mbLevel[(size_t) b] += lv;
                ++meters.mbLevelN[(size_t) b];
            }
        }
        meters.busGr += -chain.getBusComp().gainReductionDb();
        if (const float bl = chain.getBusComp().detectorLevelDb(); bl > -99.0f)
        {
            meters.busLevel += bl;
            ++meters.busLevelN;
        }
        meters.clipPeak = std::max (meters.clipPeak, (double) chain.getClipper().inputPeakDb());
    }
    else
        noSignalFor += 0.1;
    if (waited >= measureSeconds)
        measureAndAct();
    repaint();
}

void AutoMatchStrip::enterTonal (const juce::String& why)
{
    phase = Phase::tonal;
    currentBand = 0;
    bandAttempts.fill (0);
    for (int b = 0; b < 6; ++b)
        steps[(size_t) (stepBand0 + b)] = StepState::pending;
    steps[stepBand0] = StepState::running;
    status = why;
    restartMeasurement();
}

void AutoMatchStrip::afterTonal()
{
    // the order follows the signal chain: dynamics, width, loudness, checks; every big change is
    // followed by one more pass over the tonal bands
    if (! dynamicsDone)
    {
        phase = Phase::dynamics;
        dynStage = 0;
        steps[stepDynamics] = StepState::running;
        measureAndAct();
        return;
    }
    if (! widthDone)
    {
        phase = Phase::width;
        widthAttempts = 0;
        steps[stepWidth] = StepState::running;
        ctx.setParam ("st_on", 1.0f);
        ctx.setParam ("st_guard_on", 1.0f);
        ctx.setParam ("st_guard_thr", 0.0f);
        ctx.setParam ("st_ms_balance", 0.0f);
        ctx.setParam ("st_split_freq", 4500.0f);
        status = "Measuring the stereo width";
        restartMeasurement();
        return;
    }
    if (! loudnessDone)
    {
        phase = Phase::loudness;
        steps[stepLoud] = StepState::running;
        ctx.setParam ("out_on", 1.0f);
        ctx.setParam ("out_gain", 0.0f);
        status = "Matching the loudness to " + fmtMinus (referenceLufs()) + " LUFS";
        restartMeasurement();
        return;
    }
    phase = Phase::checks;
    checkStage = 1; // the fixes were applied before the last tonal pass
    steps[stepChecks] = StepState::running;
    measureAndAct();
}

void AutoMatchStrip::measureAndAct()
{
    waited = 0.0;
    const auto avg = [] (double sum, int n) { return n > 0 ? sum / n : 0.0; };

    if (phase == Phase::input)
    {
        const double in = ctx.snap.in.integrated;
        if (in > -70.0)
        {
            const float trim = juce::jlimit (-12.0f, 12.0f, std::round ((inputTargetLufs - (float) in) * 2.0f) / 2.0f);
            ctx.setParam ("in_trim", trim);
            status = "Input " + fmtMinus (in) + " LUFS: Trim " + fmtSigned (trim) + " dB";
        }
        steps[stepInput] = StepState::ok;
        enterTonal ("Measuring Sub");
        return;
    }

    if (phase == Phase::tonal)
    {
        auto dev = deviations();
        for (int b = 0; b < 6; ++b)
            if (steps[(size_t) (stepBand0 + b)] == StepState::failed)
                dev[(size_t) b] = 0.0;
        int b = nextBand (dev, currentBand, greenDb);
        while (b >= 0 && bandAttempts[(size_t) b] >= maxBandAttempts)
        {
            steps[(size_t) (stepBand0 + b)] = StepState::failed;
            dev[(size_t) b] = 0.0;
            b = nextBand (dev, currentBand, greenDb);
        }
        for (int i = 0; i < 6; ++i)
        {
            auto& st = steps[(size_t) (stepBand0 + i)];
            if (st != StepState::failed)
                st = (i < currentBand || (b < 0 && i <= currentBand)) ? StepState::ok : (i == currentBand ? StepState::running : StepState::pending);
        }
        if (b < 0)
        {
            afterTonal();
            return;
        }
        if (b < currentBand)
            steps[(size_t) (stepBand0 + b)] = StepState::running;
        ++bandAttempts[(size_t) b];
        const auto& m = bandMap[b];
        const auto id = params::eq (m.band, "gain");
        const float g0 = ctx.param (id);
        const float g1 = juce::jlimit (-12.0f, 12.0f, g0 - 0.85f * (float) dev[(size_t) b]);
        ctx.setParam (id, g1);
        status = juce::String (bandNames[b]) + " " + fmtSigned (dev[(size_t) b]) + " dB: EQ band " + juce::String (m.band) + " " + fmtSigned (g0)
               + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + fmtSigned (g1) + " dB" + (b < currentBand ? " (re-check)" : "");
        restartMeasurement();
        return;
    }

    if (phase == Phase::dynamics)
    {
        if (dynStage == 0)
        {
            // Multiband, Bus Comp, Saturation and Clipper on with neutral thresholds, then measure the levels
            ctx.setParam ("mb_on", 1.0f);
            ctx.setParam ("mb_xover_type", 0.0f);
            for (int b = 1; b <= 5; ++b)
            {
                for (auto [field, v] : { std::pair { "thr", 0.0f }, { "ratio", 2.0f }, { "att", 20.0f }, { "rel", 200.0f }, { "rel_auto", 1.0f }, { "knee", 6.0f },
                                         { "makeup", 0.0f }, { "mix", 100.0f }, { "link", 100.0f }, { "mode", 0.0f }, { "solo", 0.0f }, { "bypass", 0.0f }, { "trim", 0.0f } })
                    ctx.setParam (params::mb (b, field), v);
            }
            for (auto [id, v] : { std::pair { "bus_on", 1.0f }, { "bus_model", 0.0f }, { "bus_ratio", 1.0f }, { "bus_att", 10.0f }, { "bus_rel_auto", 1.0f },
                                  { "bus_knee", 0.0f }, { "bus_sc_hpf", 100.0f }, { "bus_mix", 100.0f }, { "bus_thr", 0.0f }, { "bus_makeup_auto", 0.0f },
                                  { "sat_on", 1.0f }, { "clip_on", 1.0f }, { "clip_type", 1.0f }, { "clip_thr", 0.0f }, { "clip_shape", 50.0f },
                                  { "clip_mix", 100.0f }, { "clip_lowprot", 1.0f } })
                ctx.setParam (id, v);
            // gentle density: low band Tape 2 dB, mid band Tube 1 dB at 50 %, high band clean
            const float sat[3][4] = { { 2.0f, 1.0f, 100.0f, 0.0f }, { 1.0f, 0.0f, 50.0f, 0.0f }, { 0.0f, 0.0f, 100.0f, 0.0f } };
            for (int b = 1; b <= 3; ++b)
            {
                ctx.setParam (params::sat (b, "drive"), sat[b - 1][0]);
                ctx.setParam (params::sat (b, "type"), sat[b - 1][1]);
                ctx.setParam (params::sat (b, "mix"), sat[b - 1][2]);
                ctx.setParam (params::sat (b, "bias"), sat[b - 1][3]);
                ctx.setParam (params::sat (b, "out"), 0.0f);
                ctx.setParam (params::sat (b, "solo"), 0.0f);
                ctx.setParam (params::sat (b, "mute"), 0.0f);
            }
            dynStage = 1;
            status = "Dynamics: measuring the band levels";
            restartMeasurement();
            return;
        }
        const auto t = ctx.refs.target();
        if (dynStage == 1)
        {
            // target gain reduction per band from the crest factor above the reference (D-031)
            const auto& crest = ctx.snap.out.crest6;
            const double ours[5] = { crest[0], crest[1], 0.5 * (crest[2] + crest[3]), crest[4], crest[5] };
            const double refs[5] = { t.crest[0], t.crest[1], 0.5 * (t.crest[2] + t.crest[3]), t.crest[4], t.crest[5] };
            for (int b = 0; b < 5; ++b)
            {
                const double excess = ours[b] - refs[b];
                mbTargetGr[(size_t) b] = excess > 0.5 ? juce::jlimit (0.5, 4.0, excess) : 0.0;
                const double level = avg (meters.mbLevel[(size_t) b], meters.mbLevelN[(size_t) b]);
                const float thr = mbTargetGr[(size_t) b] > 0.0 && meters.mbLevelN[(size_t) b] > 0 ? (float) juce::jlimit (-60.0, 0.0, level - 2.0 * mbTargetGr[(size_t) b]) : 0.0f;
                ctx.setParam (params::mb (b + 1, "thr"), thr);
            }
            if (meters.busLevelN > 0)
                ctx.setParam ("bus_thr", (float) juce::jlimit (-40.0, 0.0, avg (meters.busLevel, meters.busLevelN) - 3.0));
            if (meters.clipPeak > -60.0)
                ctx.setParam ("clip_thr", (float) juce::jlimit (-6.0, 0.0, meters.clipPeak - 1.5));
            dynStage = 2;
            status = "Dynamics: Multiband, Bus Comp and Clipper thresholds set";
            restartMeasurement();
            return;
        }
        if (dynStage <= 3)
        {
            // refine towards the target gain reduction (Multiband per band, Bus Comp 1.5 dB)
            for (int b = 0; b < 5; ++b)
                if (mbTargetGr[(size_t) b] > 0.0)
                {
                    const double gr = avg (meters.mbGr[(size_t) b], meters.n);
                    const auto id = params::mb (b + 1, "thr");
                    ctx.setParam (id, (float) juce::jlimit (-60.0, 0.0, ctx.param (id) - 1.6 * (mbTargetGr[(size_t) b] - gr)));
                }
            const double busGr = avg (meters.busGr, meters.n);
            ctx.setParam ("bus_thr", (float) juce::jlimit (-40.0, 0.0, ctx.param ("bus_thr") - 1.6 * (1.5 - busGr)));
            status = "Dynamics: Bus Comp GR " + juce::String (busGr, 1) + " dB, target 1.5 dB";
            ++dynStage;
            restartMeasurement();
            return;
        }
        steps[stepDynamics] = StepState::ok;
        dynamicsDone = true;
        ++cycles;
        enterTonal ("Re-checking the bands after the dynamics");
        return;
    }

    if (phase == Phase::width)
    {
        const auto t = ctx.refs.target();
        const auto& b = ctx.snap.out.bandsIntegrated;
        const double mixMid = 0.5 * (b[1].widthPercent + b[2].widthPercent), mixHigh = b[3].widthPercent;
        const double refMid = 0.5 * (t.width[1] + t.width[2]), refHigh = t.width[3];
        bool done = mixMid < 1.0 || mixHigh < 1.0 || refMid < 1.0 || refHigh < 1.0; // mono material: nothing to match
        if (! done)
        {
            const double rMid = refMid / mixMid, rHigh = refHigh / mixHigh;
            done = (std::abs (rMid - 1.0) < 0.1 && std::abs (rHigh - 1.0) < 0.1) || ++widthAttempts > maxWidthAttempts;
            if (! done)
            {
                const float mid = juce::jlimit (50.0f, 150.0f, ctx.param ("st_mid_width") * (float) std::pow (rMid, 0.8));
                const float high = juce::jlimit (50.0f, 150.0f, ctx.param ("st_high_width") * (float) std::pow (rHigh, 0.8));
                ctx.setParam ("st_mid_width", std::round (mid));
                ctx.setParam ("st_high_width", std::round (high));
                status = "Width: mix " + juce::String (juce::roundToInt (mixMid)) + " / " + juce::String (juce::roundToInt (mixHigh)) + "%, reference "
                       + juce::String (juce::roundToInt (refMid)) + " / " + juce::String (juce::roundToInt (refHigh)) + "%";
                restartMeasurement();
                return;
            }
        }
        steps[stepWidth] = StepState::ok;
        widthDone = true;
        afterTonal();
        return;
    }

    if (phase == Phase::loudness)
    {
        const double target = referenceLufs(), measured = ctx.snap.out.integrated;
        const double err = target - measured;
        if (std::abs (err) <= 0.5)
            steps[stepLoud] = StepState::ok;
        else if (++loudAttempts > maxLoudAttempts || (err > 0 && ctx.param ("lim_drive") >= 23.9f))
            steps[stepLoud] = StepState::failed;
        else
        {
            const float d = juce::jlimit (0.0f, 24.0f, ctx.param ("lim_drive") + (float) (err * 0.9));
            ctx.setParam ("lim_drive", d);
            status = "Loudness " + fmtMinus (measured) + " LUFS, reference " + fmtMinus (target) + ": Limiter Drive " + juce::String (d, 1) + " dB";
            restartMeasurement();
            return;
        }
        loudnessDone = true;
        phase = Phase::checks;
        checkStage = 0;
        steps[stepChecks] = StepState::running;
        status = "Checking mono, harshness and peaks";
        measureAndAct();
        return;
    }

    if (phase == Phase::checks)
    {
        const auto& checks = ctx.snap.checks;
        const auto lightOf = [&] (int i) { return i < (int) checks.size() ? checks[(size_t) i].light : analysis::Light::ok; };
        if (checkStage == 0)
        {
            // 6 mono loss, 7 low-band correlation, 8 PLR, 9 harshness, 10 true peak, 11 integrated (info)
            bool changed = false;
            if (lightOf (6) != analysis::Light::ok || lightOf (7) != analysis::Light::ok)
            {
                ctx.setParam ("lem_on", 1.0f);
                ctx.setParam ("lem_mono_on", 1.0f);
                ctx.setParam ("lem_mono_freq", 120.0f);
                ctx.setParam ("lem_mono_amount", 100.0f);
                changed = true;
            }
            if (lightOf (9) != analysis::Light::ok)
            {
                ctx.setParam ("rs_on", 1.0f);
                ctx.setParam ("rs_low", 2000.0f);
                ctx.setParam ("rs_high", 8000.0f);
                ctx.setParam ("rs_depth", 4.0f);
                changed = true;
            }
            checkStage = 1;
            if (changed)
            {
                restartMeasurement();
                return;
            }
        }
        // the fixes can move the tonal balance: one more pass over the bands
        const auto dev = deviations();
        bool tonalOk = true;
        for (int b = 0; b < 6; ++b)
            tonalOk = tonalOk && (steps[(size_t) (stepBand0 + b)] == StepState::failed || std::abs (dev[(size_t) b]) <= 1.0);
        if (! tonalOk && cycles < 2)
        {
            ++cycles;
            enterTonal ("Re-checking the bands after the last steps");
            return;
        }
        finish();
    }
}

void AutoMatchStrip::finish()
{
    const auto& checks = ctx.snap.checks;
    juce::StringArray left;
    for (int i = analysis::numTonalBands; i < (int) checks.size(); ++i)
        if (checks[(size_t) i].light == analysis::Light::warn || checks[(size_t) i].light == analysis::Light::bad)
            left.add (checks[(size_t) i].title);
    steps[stepChecks] = left.isEmpty() ? StepState::ok : StepState::failed;
    juce::String msg = "Done in " + clock ((juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0) + ".";
    if (left.contains ("PLR"))
        msg << " PLR stays yellow: more loudness means less dynamics.";
    left.removeString ("PLR");
    if (! left.isEmpty())
        msg << " Not green: " << left.joinIntoString (", ") << ".";
    for (int b = 0; b < 6; ++b)
        if (steps[(size_t) (stepBand0 + b)] == StepState::failed)
            msg << " " << bandNames[b] << " cannot reach the target.";
    phase = Phase::done;
    status = msg;
    resized();
}

// ---- drawing -------------------------------------------------------------------------------------------
void AutoMatchStrip::resized()
{
    auto r = getLocalBounds().reduced (10, 5);
    const bool running = phase != Phase::idle && phase != Phase::done;
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
    if (phase == Phase::idle)
    {
        // idle: reference on the left, "play, then press" and the badge next to the button (mockup v11)
        badgeArea = r.removeFromRight (92).withSizeKeepingCentre (88, 18);
        r.removeFromRight (8);
        refArea = r.removeFromLeft (std::min (420, r.getWidth() / 2));
        hintArea = r;
        return;
    }
    const int refW = std::min (300, r.getWidth() / 4);
    refArea = r.removeFromLeft (refW);
    r.removeFromLeft (8);
    badgeArea = r.removeFromLeft (92).withSizeKeepingCentre (88, 18);
    r.removeFromLeft (8);
    const int chipsW = numSteps * 62;
    if (r.getWidth() > chipsW + 100)
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
    // experimental badge
    g.setColour (col::yellow);
    g.fillRoundedRectangle (badgeArea.toFloat(), 3.0f);
    g.setColour (juce::Colour (0xff1b1600));
    g.setFont (font (11.0f, true));
    g.drawText ("EXPERIMENTAL", badgeArea, juce::Justification::centred);

    if (! hintArea.isEmpty())
    {
        g.setColour (col::text3);
        g.setFont (font (12.5f));
        g.drawText (tr ("Play the song, then press:"), hintArea, juce::Justification::centredRight, true);
    }
    if (! chipsArea.isEmpty())
    {
        auto c = chipsArea;
        for (int i = 0; i < numSteps; ++i)
        {
            const auto st = steps[(size_t) i];
            auto chip = c.removeFromLeft (58).withSizeKeepingCentre (58, 22).toFloat();
            c.removeFromLeft (4);
            const auto colour = st == StepState::ok ? col::ok : (st == StepState::failed ? col::warn : (st == StepState::running ? juce::Colours::white : col::text3));
            g.setColour (st == StepState::running ? juce::Colour (0xff1d3352) : col::sunk);
            g.fillRoundedRectangle (chip, 11.0f);
            g.setColour (st == StepState::running ? col::blue : (st == StepState::ok ? juce::Colour (0xff285c45) : col::line2));
            g.drawRoundedRectangle (chip.reduced (0.5f), 11.0f, 1.0f);
            g.setColour (colour);
            g.setFont (font (11.5f, st == StepState::running));
            const juce::String mark = st == StepState::ok ? juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x93")) : (st == StepState::failed ? juce::String ("!") : juce::String());
            g.drawText (mark + stepNames[i], chip, juce::Justification::centred, true);
        }
    }
    if (! statusArea.isEmpty())
    {
        juce::String text = status;
        if (phase != Phase::idle && phase != Phase::done && noSignalFor > 1.5)
            text = tr ("Waiting for audio. Start playback.");
        g.setColour (col::text2);
        g.setFont (font (12.5f));
        g.drawText (text, statusArea, juce::Justification::centredLeft, true);
    }
}
} // namespace mm::ui
