#include "dsp/MasterChain.h"

#include <bit>
#include <cstdint>

namespace mm::dsp
{
bool containsNonFinite (const juce::AudioBuffer<float>& buffer) noexcept
{
    constexpr std::uint32_t exponentMask = 0x7f800000u;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* d = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if ((std::bit_cast<std::uint32_t> (d[i]) & exponentMask) == exponentMask)
                return true;
    }
    return false;
}

namespace
{
/** Pre-resolved parameter pointers (lookups by string are not real-time safe). */
struct ParamPtrsImpl
{
    using A = std::atomic<float>*;
    A quality, inTrim, polL, polR, hpfOn, hpfFreq, hpfSlope;
    A eqPhase, rsOn, rsLow, rsHigh, rsDepth, rsSharp, rsSpeed, rsMix, rsDelta;
    struct EqBand { A on, type, slope, freq, gain, q, place, dyn, thr, range, mode, att, rel; };
    std::array<EqBand, 10> eqb {};
    A mbType;
    std::array<A, 4> mbXo {};
    struct MbBand { A thr, ratio, att, rel, relAuto, knee, makeup, mix, link, mode, solo, bypass, trim; };
    std::array<MbBand, 5> mbb {};
    A monoOn, monoFreq, monoSlope, monoAmount, rotOn, rotFreq, rotAngle, trOn, trAmount, trMix;
    A busModel, busThr, busRatio, busAtt, busRel, busRelAuto, busKnee, busSc, busMix, busDetect, busLink, busMakeup;
    struct SatBand { A drive, type, bias, mix, out, solo, mute; };
    std::array<SatBand, params::satBands> satb {};
    A stSplit, stMid, stHigh, stBal, stGuard, stGuardThr;
    A inflInput, inflEffect, inflCurve, inflOutput, inflClip, inflSplit;
    A limLook, limRel, limRelAuto, limMode, limDrive, limCeil, limLink, limTrans, limBands, limAGain, limASpeed, limATrans, limClip, limLevel, limSoft, limDens, limOutClip;
    A outGain, outDither, outBits;
};

ParamPtrsImpl resolve (const params::Raw& r)
{
    using namespace params;
    ParamPtrsImpl p {};
    p.quality = r.get ("quality");
    p.inTrim = r.get ("in_trim");
    p.polL = r.get ("in_pol_l");
    p.polR = r.get ("in_pol_r");
    p.hpfOn = r.get ("hpf_on");
    p.hpfFreq = r.get ("hpf_freq");
    p.hpfSlope = r.get ("hpf_slope");
    p.eqPhase = r.get ("eq_phase");
    p.rsOn = r.get ("rs_on");
    p.rsLow = r.get ("rs_low");
    p.rsHigh = r.get ("rs_high");
    p.rsDepth = r.get ("rs_depth");
    p.rsSharp = r.get ("rs_sharp");
    p.rsSpeed = r.get ("rs_speed");
    p.rsMix = r.get ("rs_mix");
    p.rsDelta = r.get ("rs_delta");
    for (int b = 1; b <= 10; ++b)
    {
        auto& e = p.eqb[(size_t) b - 1];
        e.on = r.get (eq (b, "on"));
        e.type = r.get (eq (b, "type"));
        e.slope = (b == 1 || b == 10) ? r.get (eq (b, "slope")) : nullptr;
        e.freq = r.get (eq (b, "freq"));
        e.gain = r.get (eq (b, "gain"));
        e.q = r.get (eq (b, "q"));
        e.place = r.get (eq (b, "place"));
        e.dyn = r.get (eq (b, "dyn"));
        e.thr = r.get (eq (b, "thr"));
        e.range = r.get (eq (b, "range"));
        e.mode = r.get (eq (b, "mode"));
        e.att = r.get (eq (b, "att"));
        e.rel = r.get (eq (b, "rel"));
    }
    p.mbType = r.get ("mb_xover_type");
    for (int i = 1; i <= 4; ++i)
        p.mbXo[(size_t) i - 1] = r.get (mbXo (i));
    for (int b = 1; b <= 5; ++b)
    {
        auto& m = p.mbb[(size_t) b - 1];
        m.thr = r.get (mb (b, "thr"));
        m.ratio = r.get (mb (b, "ratio"));
        m.att = r.get (mb (b, "att"));
        m.rel = r.get (mb (b, "rel"));
        m.relAuto = r.get (mb (b, "rel_auto"));
        m.knee = r.get (mb (b, "knee"));
        m.makeup = r.get (mb (b, "makeup"));
        m.mix = r.get (mb (b, "mix"));
        m.link = r.get (mb (b, "link"));
        m.mode = r.get (mb (b, "mode"));
        m.solo = r.get (mb (b, "solo"));
        m.bypass = r.get (mb (b, "bypass"));
        m.trim = r.get (mb (b, "trim"));
    }
    p.monoOn = r.get ("lem_mono_on");
    p.monoFreq = r.get ("lem_mono_freq");
    p.monoSlope = r.get ("lem_mono_slope");
    p.monoAmount = r.get ("lem_mono_amount");
    p.rotOn = r.get ("lem_rot_on");
    p.rotFreq = r.get ("lem_rot_freq");
    p.rotAngle = r.get ("lem_rot_angle");
    p.trOn = r.get ("lem_tr_on");
    p.trAmount = r.get ("lem_tr_amount");
    p.trMix = r.get ("lem_tr_mix");
    p.busModel = r.get ("bus_model");
    p.busThr = r.get ("bus_thr");
    p.busRatio = r.get ("bus_ratio");
    p.busAtt = r.get ("bus_att");
    p.busRel = r.get ("bus_rel");
    p.busRelAuto = r.get ("bus_rel_auto");
    p.busKnee = r.get ("bus_knee");
    p.busSc = r.get ("bus_sc_hpf");
    p.busMix = r.get ("bus_mix");
    p.busDetect = r.get ("bus_detect");
    p.busLink = r.get ("bus_link");
    p.busMakeup = r.get ("bus_makeup_auto");
    for (int b = 1; b <= params::satBands; ++b)
    {
        auto& s = p.satb[(size_t) b - 1];
        s.drive = r.get (sat (b, "drive"));
        s.type = r.get (sat (b, "type"));
        s.bias = r.get (sat (b, "bias"));
        s.mix = r.get (sat (b, "mix"));
        s.out = r.get (sat (b, "out"));
        s.solo = r.get (sat (b, "solo"));
        s.mute = r.get (sat (b, "mute"));
    }
    p.stSplit = r.get ("st_split_freq");
    p.stMid = r.get ("st_mid_width");
    p.stHigh = r.get ("st_high_width");
    p.stBal = r.get ("st_ms_balance");
    p.stGuard = r.get ("st_guard_on");
    p.stGuardThr = r.get ("st_guard_thr");
    p.inflInput = r.get ("infl_input");
    p.inflEffect = r.get ("infl_effect");
    p.inflCurve = r.get ("infl_curve");
    p.inflOutput = r.get ("infl_output");
    p.inflClip = r.get ("infl_clip");
    p.inflSplit = r.get ("infl_split");
    p.limLook = r.get ("lim_lookahead");
    p.limRel = r.get ("lim_rel");
    p.limRelAuto = r.get ("lim_rel_auto");
    p.limMode = r.get ("lim_mode");
    p.limDrive = r.get ("lim_drive");
    p.limCeil = r.get ("lim_ceiling");
    p.limLink = r.get ("lim_link");
    p.limTrans = r.get ("lim_transient");
    p.limBands = r.get ("lim_bands");
    p.limAGain = r.get ("lim_adapt_gain");
    p.limASpeed = r.get ("lim_adapt_speed");
    p.limATrans = r.get ("lim_adapt_trans");
    p.limClip = r.get ("lim_clip");
    p.limLevel = r.get ("lim_leveler");
    p.limSoft = r.get ("lim_soften");
    p.limDens = r.get ("lim_density");
    p.limOutClip = r.get ("lim_out_clip");
    p.outGain = r.get ("out_gain");
    p.outDither = r.get ("out_dither");
    p.outBits = r.get ("out_bits");
    return p;
}

float v (std::atomic<float>* a) noexcept { return a == nullptr ? 0.0f : a->load (std::memory_order_relaxed); }
int iv (std::atomic<float>* a) noexcept { return juce::roundToInt (v (a)); }
bool bv (std::atomic<float>* a) noexcept { return v (a) >= 0.5f; }

DynamicEq::Settings eqFrom (const ParamPtrsImpl& p)
{
    DynamicEq::Settings s;
    s.linearPhase = iv (p.eqPhase) == 1;
    for (int b = 0; b < 10; ++b)
    {
        const auto& e = p.eqb[(size_t) b];
        auto& d = s.bands[(size_t) b];
        d.on = bv (e.on);
        d.type = iv (e.type);
        d.slope = e.slope != nullptr ? iv (e.slope) : 3;
        d.freq = v (e.freq);
        d.gainDb = v (e.gain);
        d.q = v (e.q);
        d.placement = iv (e.place);
        d.dynamic = bv (e.dyn);
        d.thresholdDb = v (e.thr);
        d.rangeDb = v (e.range);
        d.mode = iv (e.mode);
        d.attackMs = v (e.att);
        d.releaseMs = v (e.rel);
    }
    return s;
}

Multiband::Settings mbFrom (const ParamPtrsImpl& p)
{
    Multiband::Settings s;
    s.linearPhase = iv (p.mbType) == 1;
    for (int i = 0; i < 4; ++i)
        s.crossovers[(size_t) i] = v (p.mbXo[(size_t) i]);
    for (int b = 0; b < 5; ++b)
    {
        const auto& m = p.mbb[(size_t) b];
        auto& d = s.bands[(size_t) b];
        d.thresholdDb = v (m.thr);
        d.ratio = v (m.ratio);
        d.attackMs = v (m.att);
        d.releaseMs = v (m.rel);
        d.releaseAuto = bv (m.relAuto);
        d.kneeDb = v (m.knee);
        d.makeupDb = v (m.makeup);
        d.mix = v (m.mix) * 0.01f;
        d.link = v (m.link) * 0.01f;
        d.mode = iv (m.mode);
        d.solo = bv (m.solo);
        d.bypass = bv (m.bypass);
        d.trimDb = v (m.trim);
    }
    return s;
}

juce::String keyOf (const DynamicEq::Settings& s)
{
    juce::String k;
    for (const auto& b : s.bands)
        if (b.on)
            k << b.type << ',' << b.slope << ',' << b.freq << ',' << b.gainDb << ',' << b.q << ',' << b.placement << ';';
        else
            k << "-;";
    return k;
}

juce::String keyOf (const Multiband::Settings& s)
{
    juce::String k;
    for (auto x : s.crossovers)
        k << x << ',';
    return k;
}
} // namespace

struct MasterChain::ParamPtrs : ParamPtrsImpl
{
};

MasterChain::MasterChain (juce::AudioProcessorValueTreeState& state) : apvts (state), raw (state)
{
    Module* mods[numModules] = { &inputModule, &eqModule, &mbModule, &lowEndModule, &busModule,
                                 &satModule, &stereoModule, &inflModule, &limModule, &outputModule };
    const char* onIds[numModules] = { "in_on", "eq_on", "mb_on", "lem_on", "bus_on",
                                      "sat_on", "st_on", "clip_on", "lim_on", "out_on" };
    for (int i = 0; i < numModules; ++i)
    {
        slots[(size_t) i].module = mods[i];
        slots[(size_t) i].onParam = raw.get (onIds[i]);
    }
    ptrs = std::make_unique<ParamPtrs>();
    static_cast<ParamPtrsImpl&> (*ptrs) = resolve (raw);
}

MasterChain::~MasterChain() = default;

float MasterChain::p (const char* id) const noexcept { return raw.value (id); }
float MasterChain::p (const juce::String& id) const noexcept { return raw.value (id); }

DynamicEq::Settings MasterChain::eqSnapshot() const { return eqFrom (*ptrs); }
Multiband::Settings MasterChain::multibandSnapshot() const { return mbFrom (*ptrs); }

void MasterChain::readSettings() noexcept
{
    const ParamPtrsImpl& q = *ptrs;

    auto& in = inputModule.settings;
    in.trimDb = v (q.inTrim);
    in.polarityL = bv (q.polL);
    in.polarityR = bv (q.polR);
    in.hpfOn = bv (q.hpfOn);
    in.hpfFreq = v (q.hpfFreq);
    in.hpfSlope = iv (q.hpfSlope);

    eqModule.settings = eqFrom (q);
    auto& rs = eqModule.resonance.settings;
    rs.on = bv (q.rsOn);
    rs.lowHz = std::min (v (q.rsLow), v (q.rsHigh));
    rs.highHz = std::max (v (q.rsLow), v (q.rsHigh));
    rs.depthDb = v (q.rsDepth);
    rs.sharpness = v (q.rsSharp) * 0.01f;
    rs.speed = v (q.rsSpeed) * 0.01f;
    rs.mix = v (q.rsMix) * 0.01f;
    rs.delta = bv (q.rsDelta);
    mbModule.settings = mbFrom (q);

    auto& le = lowEndModule.settings;
    le.monoOn = bv (q.monoOn);
    le.monoFreq = v (q.monoFreq);
    le.monoSlope = iv (q.monoSlope);
    le.monoAmount = v (q.monoAmount) * 0.01f;
    le.rotOn = bv (q.rotOn);
    le.rotFreq = v (q.rotFreq);
    le.rotAngle = v (q.rotAngle);
    le.trOn = bv (q.trOn);
    le.trAmount = v (q.trAmount) * 0.01f;
    le.trMix = v (q.trMix) * 0.01f;

    auto& bc = busModule.settings;
    bc.model = iv (q.busModel);
    bc.thresholdDb = v (q.busThr);
    bc.ratio = params::busRatioValues[std::clamp (iv (q.busRatio), 0, 5)];
    bc.attackMs = v (q.busAtt);
    bc.releaseMs = v (q.busRel);
    bc.releaseAuto = bv (q.busRelAuto);
    bc.hardKnee = iv (q.busKnee) == 1;
    bc.scHpf = v (q.busSc);
    bc.mix = v (q.busMix) * 0.01f;
    bc.rmsAmount = v (q.busDetect) * 0.01f;
    bc.link = v (q.busLink) * 0.01f;
    bc.autoMakeup = bv (q.busMakeup);

    auto& sa = satModule.settings;
    for (int b = 0; b < params::satBands; ++b)
    {
        const auto& s = q.satb[(size_t) b];
        auto& d = sa.bands[(size_t) b];
        d.driveDb = v (s.drive);
        d.type = iv (s.type);
        d.bias = v (s.bias) * 0.01f;
        d.mix = v (s.mix) * 0.01f;
        d.outDb = v (s.out);
        d.solo = v (s.solo) >= 0.5f;
        d.mute = v (s.mute) >= 0.5f;
    }

    auto& st = stereoModule.settings;
    st.lowSplit = (slots[lowEnd].on && le.monoOn) ? le.monoFreq : 120.0f;
    st.split = v (q.stSplit);
    st.midWidth = v (q.stMid) * 0.01f;
    st.highWidth = v (q.stHigh) * 0.01f;
    st.balanceDb = v (q.stBal);
    st.guardOn = bv (q.stGuard);
    st.guardThreshold = v (q.stGuardThr);

    auto& inf = inflModule.settings;
    inf.inputDb = v (q.inflInput);
    inf.effect = v (q.inflEffect) * 0.01f;
    inf.curve = v (q.inflCurve);
    inf.outputDb = v (q.inflOutput);
    inf.clip = bv (q.inflClip);
    inf.split = bv (q.inflSplit);

    auto& li = limModule.settings;
    li.lookaheadMs = v (q.limLook);
    li.releaseMs = v (q.limRel);
    li.releaseAuto = bv (q.limRelAuto);
    li.mode = iv (q.limMode);
    li.driveDb = v (q.limDrive);
    li.ceilingDb = v (q.limCeil);
    li.link = v (q.limLink) * 0.01f;
    li.transient = v (q.limTrans) * 0.01f;
    li.bands = iv (q.limBands);
    li.adaptGainDb = v (q.limAGain);
    li.adaptSpeed = v (q.limASpeed) * 0.01f;
    li.adaptTrans = v (q.limATrans) * 0.01f;
    li.clipDb = v (q.limClip);
    li.leveler = v (q.limLevel) * 0.01f;
    li.soften = v (q.limSoft) * 0.01f;
    li.density = v (q.limDens) * 0.01f;
    li.outClipDb = v (q.limOutClip);

    auto& out = outputModule.settings;
    out.gainDb = v (q.outGain);
    out.dither = iv (q.outDither);
    out.bits = iv (q.outBits);

    const int quality = iv (q.quality);
    const bool high = quality == 2 || (quality == 0 && offlineMode);
    satModule.setHighQuality (high);
    inflModule.setHighQuality (high);
}

void MasterChain::prepare (double sampleRate, int maxBlockSize, bool offline)
{
    const juce::ScopedLock sl (buildLock);
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    offlineMode = offline;
    fadeStep = 1.0 / (0.02 * fs);
    dry.setSize (2, maxBlock, false, true, false);

    for (auto& s : slots)
        s.on = s.onParam->load() >= 0.5f;
    readSettings();

    const Context ctx { fs, maxBlock };
    for (auto& s : slots)
    {
        s.module->prepare (ctx);
        s.bypass.prepare (2, 40000);
        s.fade = s.on ? 1.0 : 0.0;
    }
    lastEqKey.clear();
    lastMbKey.clear();
}

void MasterChain::reset()
{
    for (auto& s : slots)
    {
        s.module->reset();
        s.bypass.reset();
    }
}

int MasterChain::latencySamples() const noexcept
{
    int total = 0;
    for (const auto& s : slots)
        total += s.module->latency();
    return total;
}

void MasterChain::updateLinearPhase()
{
    const juce::ScopedLock sl (buildLock);
    const auto eqs = eqSnapshot();
    if (eqs.linearPhase)
    {
        const auto key = keyOf (eqs);
        if (key != lastEqKey)
        {
            eqModule.buildLinearPhase (eqs);
            lastEqKey = key;
        }
    }
    else
    {
        lastEqKey.clear();
    }

    const auto mbs = multibandSnapshot();
    if (mbs.linearPhase)
    {
        const auto key = keyOf (mbs);
        if (key != lastMbKey)
        {
            mbModule.buildLinearPhase (mbs);
            lastMbKey = key;
        }
    }
    else
    {
        lastMbKey.clear();
    }
}

bool MasterChain::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (containsNonFinite (buffer))
    {
        buffer.clear();
        reset();
        return false;
    }

    readSettings();
    const int total = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : buffer.getWritePointer (0);

    for (int start = 0; start < total; start += maxBlock)
    {
        const int n = std::min (maxBlock, total - start);
        float* bl = l + start;
        float* br = r + start;

        for (size_t si = 0; si < slots.size(); ++si)
        {
            auto& s = slots[si];
            if (si == limiter)
            {
                uint64_t sampleOvers = 0, tpOvers = 0;
                for (int i = 0; i < n; ++i)
                {
                    sampleOvers += (std::abs (bl[i]) > 1.0f) + (std::abs (br[i]) > 1.0f);
                    tpOvers += (preLimTp[0].process (bl[i]) > 1.0f) + (preLimTp[1].process (br[i]) > 1.0f);
                }
                preLimiterSampleOvers.fetch_add (sampleOvers, std::memory_order_relaxed);
                preLimiterTpOvers.fetch_add (tpOvers, std::memory_order_relaxed);
            }
            const bool want = s.onParam->load (std::memory_order_relaxed) >= 0.5f;
            if (want && ! s.on && s.fade <= 0.0)
                s.module->reset(); // start clean, fade in from the delayed dry signal
            s.on = want;
            s.bypass.setDelay (s.module->latency());

            const bool fading = s.on ? s.fade < 1.0 : s.fade > 0.0;
            if (! s.on && ! fading)
            {
                float* io[2] = { bl, br };
                s.bypass.process (io, 2, n);
                continue;
            }

            // the bypass path always runs, so a fade in either direction has the delayed dry signal
            float* d[2] = { dry.getWritePointer (0), dry.getWritePointer (1) };
            std::copy (bl, bl + n, d[0]);
            std::copy (br, br + n, d[1]);
            s.bypass.process (d, 2, n);

            s.module->process (bl, br, n);

            if (fading)
            {
                const double step = s.on ? fadeStep : -fadeStep;
                for (int i = 0; i < n; ++i)
                {
                    s.fade = std::clamp (s.fade + step, 0.0, 1.0);
                    const double w = s.fade;
                    bl[i] = (float) (d[0][i] + w * (bl[i] - d[0][i]));
                    br[i] = (float) (d[1][i] + w * (br[i] - d[1][i]));
                }
            }
        }
    }

    return true;
}
} // namespace mm::dsp
