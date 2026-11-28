#include "automatch/MatchEngine.h"

#include "analysis/TapAnalyzer.h"
#include "dsp/MasterChain.h"
#include "state/Parameters.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <thread>

namespace mm::automatch
{
namespace
{
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
constexpr int dimEq0 = 0, dimMb0 = 6, dimBus = 11, dimInfl = 12, dimDrive = 13, dimMidW = 14, dimHighW = 15, dimTrans = 16, dimSatLow = 17;
constexpr float maxLowBoostDb = 3.0f; // Sub and Bass: at most +3 dB by EQ, the rest by harmonics (D-041)

double sq (double v) { return v * v; }

/** Tonal deviation per band with the median removed. The tonal balance is a share of the total, so
    one band that misses energy makes all others look too loud; without the common part the missing
    band stays the only one to correct (D-041). */
std::array<double, 6> tonalDeviation (const std::array<double, 6>& ours, const std::array<double, 6>& target)
{
    std::array<double, 6> d {}, sorted {};
    for (size_t b = 0; b < 6; ++b)
        d[b] = sorted[b] = ours[b] - target[b];
    std::sort (sorted.begin(), sorted.end());
    const double median = 0.5 * (sorted[2] + sorted[3]);
    for (auto& v : d)
        v -= median;
    return d;
}

/** The six tonal crest factors folded onto the five Multiband bands. */
std::array<double, 5> crest5 (const std::array<double, 6>& c) { return { c[0], c[1], 0.5 * (c[2] + c[3]), c[4], c[5] }; }
} // namespace

const std::vector<Dim>& dims()
{
    static const std::vector<Dim> d = [] {
        std::vector<Dim> v;
        for (const auto& m : bandMap)
            // mastering moves: Sub and Bass up to +3 dB, Low-mid (mud) up to +2 dB, the rest +-4 dB
            v.push_back ({ params::eq (m.band, "gain"), m.band <= 2 ? -6.0f : -4.0f, m.band <= 2 ? maxLowBoostDb : (m.band == 3 ? 2.0f : 4.0f), 0.0f });
        for (int b = 1; b <= 5; ++b)
            v.push_back ({ params::mb (b, "thr"), -40.0f, 0.0f, 0.0f });
        v.push_back ({ "bus_thr", -30.0f, 0.0f, 0.0f });
        v.push_back ({ "infl_effect", 0.0f, 60.0f, 0.0f });
        v.push_back ({ "lim_drive", 0.0f, 24.0f, 0.0f });
        v.push_back ({ "st_mid_width", 60.0f, 150.0f, 100.0f });
        v.push_back ({ "st_high_width", 60.0f, 160.0f, 100.0f });
        // a missing low end comes from harmonics rather than energy: Translator and the low band of Saturation
        v.push_back ({ "lem_tr_amount", 0.0f, 60.0f, 0.0f });
        v.push_back ({ params::sat (1, "drive"), 0.0f, 8.0f, 2.0f });
        return v;
    }();
    return d;
}

std::vector<double> toUnit (const ParamMap& p)
{
    const auto& d = dims();
    std::vector<double> x (d.size());
    for (size_t i = 0; i < d.size(); ++i)
    {
        const auto it = p.find (d[i].id);
        const float v = it != p.end() ? it->second : d[i].neutral;
        x[i] = std::clamp ((double) (v - d[i].lo) / (double) (d[i].hi - d[i].lo), 0.0, 1.0);
    }
    return x;
}

void fromUnit (const std::vector<double>& x, ParamMap& p)
{
    const auto& d = dims();
    for (size_t i = 0; i < d.size() && i < x.size(); ++i)
    {
        float v = d[i].lo + (float) std::clamp (x[i], 0.0, 1.0) * (d[i].hi - d[i].lo);
        // the knob steps of the plugin: 0.1 dB, whole percent
        v = (i == (size_t) dimInfl || i == (size_t) dimMidW || i == (size_t) dimHighW || i == (size_t) dimTrans) ? std::round (v) : std::round (v * 10.0f) / 10.0f;
        p[d[i].id] = v;
    }
}

double cost (const Features& f, const Target& t, const std::vector<double>& x, const std::vector<double>& prior, double priorWeight)
{
    double c = 0.0;
    const auto dev = tonalDeviation (f.tonal, t.tonal);
    for (size_t b = 0; b < 6; ++b)
        c += sq (dev[b] / std::max (0.75, t.corridor[b]));
    c += 1.5 * sq ((f.lufs - t.lufs) / 0.5);
    if (t.hasCrest)
        for (size_t b = 0; b < 6; ++b)
            c += 0.4 * sq ((f.crest[b] - t.crest[b]) / 1.5);
    if (t.hasWidth)
        for (size_t b = 1; b <= 3; ++b)
            if (t.width[b] > 1.0)
                c += 0.5 * sq ((f.width[b] - t.width[b]) / std::max (8.0, 0.15 * t.width[b]));
    if (f.truePeak > t.ceiling + 0.05)
        c += 2.0 * sq ((f.truePeak - t.ceiling) / 0.1);
    if (t.hasHarshness)
        c += 0.3 * sq (std::max (0.0, f.harshness - t.harshness - 1.0));
    // a low boost by EQ costs more than a cut: it makes the low end boom (D-041)
    for (size_t i = 0; i < 3 && i < x.size(); ++i)
    {
        const auto& d = dims()[i];
        const double g = d.lo + x[i] * (d.hi - d.lo);
        if (g > 0.0)
            c += i < 2 ? sq (g / 2.0) : 0.5 * sq (g / 1.5); // Low-mid boosts add mud
    }
    // boom: Sub and Bass more sustained (lower crest factor) than the reference
    if (t.hasCrest)
        for (size_t b = 0; b < 2; ++b)
            c += sq (std::max (0.0, t.crest[b] - f.crest[b] - 0.5));
    // pumping: the limiter gain reduction moving with the bass
    c += 0.8 * sq (std::max (0.0, f.grStd - 1.0) / 0.5);
    for (size_t i = 0; i < x.size() && i < prior.size(); ++i)
        c += priorWeight * sq ((x[i] - prior[i]) / 0.5);
    return c;
}

ParamMap fixedSetup (const ParamMap& current, double inputLufs)
{
    ParamMap p;
    p["in_on"] = 1.0f;
    p["hpf_on"] = 1.0f;
    p["hpf_freq"] = 30.0f; // clean low end: no energy below 30 Hz (D-041)
    p["hpf_slope"] = 1.0f;
    p["in_trim"] = inputLufs > -70.0 ? std::clamp (std::round ((-16.0f - (float) inputLufs) * 2.0f) / 2.0f, -12.0f, 12.0f) : 0.0f;
    p["eq_on"] = 1.0f;
    for (const auto& m : bandMap)
    {
        p[params::eq (m.band, "on")] = 1.0f;
        p[params::eq (m.band, "type")] = (float) m.type;
        p[params::eq (m.band, "freq")] = m.freq;
        p[params::eq (m.band, "q")] = m.q;
        p[params::eq (m.band, "dyn")] = 0.0f;
        p[params::eq (m.band, "place")] = 0.0f;
    }
    // Sub and Bass are dynamic: the boost backs off by up to 3 dB while the low band is loud, so a
    // kick keeps its weight and long bass notes do not boom. The threshold is set after the neutral
    // pass (0 dBFS until then: no action).
    for (int b = 1; b <= 2; ++b)
    {
        p[params::eq (b, "dyn")] = 1.0f;
        p[params::eq (b, "mode")] = 0.0f;
        p[params::eq (b, "thr")] = 0.0f;
        p[params::eq (b, "range")] = 3.0f;
        p[params::eq (b, "att")] = 15.0f;
        p[params::eq (b, "rel")] = 120.0f;
    }
    // clean low end: mono below 120 Hz; Translator ready (its amount is a search knob)
    for (auto [id, v] : { std::pair { "lem_on", 1.0f }, { "lem_mono_on", 1.0f }, { "lem_mono_freq", 120.0f }, { "lem_mono_slope", 1.0f },
                          { "lem_mono_amount", 100.0f }, { "lem_tr_on", 1.0f }, { "lem_tr_mix", 50.0f } })
        p[id] = v;
    p["mb_on"] = 1.0f;
    p["mb_xover_type"] = 0.0f;
    for (int b = 1; b <= 5; ++b)
        for (auto [field, v] : { std::pair { "ratio", 2.0f }, { "att", 20.0f }, { "rel", 200.0f }, { "rel_auto", 1.0f }, { "knee", 6.0f }, { "makeup", 0.0f },
                                 { "mix", 100.0f }, { "link", 100.0f }, { "mode", 0.0f }, { "solo", 0.0f }, { "bypass", 0.0f }, { "trim", 0.0f } })
            p[params::mb (b, field)] = v;
    for (auto [id, v] : { std::pair { "bus_on", 1.0f }, { "bus_model", 0.0f }, { "bus_ratio", 1.0f }, { "bus_att", 10.0f }, { "bus_rel_auto", 1.0f },
                          { "bus_knee", 0.0f }, { "bus_sc_hpf", 100.0f }, { "bus_mix", 100.0f }, { "bus_makeup_auto", 0.0f },
                          { "sat_on", 1.0f }, { "clip_on", 1.0f }, { "infl_input", 0.0f }, { "infl_curve", 0.0f },
                          { "infl_output", 0.0f }, { "infl_clip", 1.0f }, { "infl_split", 0.0f },
                          { "st_on", 1.0f }, { "st_guard_on", 1.0f }, { "st_guard_thr", 0.0f }, { "st_ms_balance", 0.0f }, { "st_split_freq", 4500.0f },
                          { "lim_on", 1.0f }, { "out_on", 1.0f }, { "out_gain", 0.0f } })
        p[id] = v;
    // gentle density: low band Tape 2 dB, mid band Tube 1 dB at 50 %, high band clean (D-031)
    const float sat[3][4] = { { 2.0f, 1.0f, 100.0f, 0.0f }, { 1.0f, 0.0f, 50.0f, 0.0f }, { 0.0f, 0.0f, 100.0f, 0.0f } };
    for (int b = 1; b <= 3; ++b)
    {
        if (b > 1) // the low band drive is a search knob
            p[params::sat (b, "drive")] = sat[b - 1][0];
        p[params::sat (b, "type")] = sat[b - 1][1];
        p[params::sat (b, "mix")] = sat[b - 1][2];
        p[params::sat (b, "bias")] = sat[b - 1][3];
        p[params::sat (b, "out")] = 0.0f;
        p[params::sat (b, "solo")] = 0.0f;
        p[params::sat (b, "mute")] = 0.0f;
    }
    const auto ceil = current.find ("lim_ceiling");
    p["lim_ceiling"] = std::min (ceil != current.end() ? ceil->second : -1.0f, -1.0f);
    return p;
}

// ---- Twin --------------------------------------------------------------------------------------------------
struct Twin::Host final : public juce::AudioProcessor
{
    Host()
        : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true).withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "MasterMindTwin", params::createLayout()),
          chain (apvts)
    {
    }
    const juce::String getName() const override { return "Twin"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    void set (const ParamMap& p)
    {
        for (const auto& [id, v] : p)
            if (auto* raw = apvts.getRawParameterValue (id))
                raw->store (v);
    }

    juce::AudioProcessorValueTreeState apvts;
    dsp::MasterChain chain;
    analysis::TapAnalyzer analyzer;
};

Twin::Twin (double sampleRate, const ParamMap& base) : host (std::make_unique<Host>()), fs (sampleRate)
{
    host->set (base);
    host->chain.prepare (fs, 512, false);
    host->analyzer.setLightweight (true);
    host->analyzer.prepare (fs, 13);
}

Twin::~Twin() = default;

Features Twin::run (const ParamMap& knobs, const juce::AudioBuffer<float>& audio, int preroll)
{
    auto& h = *host;
    h.set (knobs);
    h.chain.updateLinearPhase();
    h.chain.reset();
    h.analyzer.reset();
    const int n = audio.getNumSamples();
    work.setSize (2, n, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
        work.copyFrom (ch, 0, audio, std::min (ch, audio.getNumChannels() - 1), 0, n);
    Features f;
    int meterBlocks = 0;
    std::array<double, 2> eqSum {};
    grMeans.clear();
    constexpr int block = 512;
    for (int start = 0; start < n; start += block)
    {
        const int len = std::min (block, n - start);
        float* chans[2] = { work.getWritePointer (0) + start, work.getWritePointer (1) + start };
        juce::AudioBuffer<float> view (chans, 2, len);
        h.chain.process (view);
        if (start >= preroll)
        {
            h.analyzer.process (chans[0], chans[1], len);
            const auto& mb = h.chain.getMultiband();
            for (int b = 0; b < 5; ++b)
            {
                f.mbLevel[(size_t) b] += std::max (-100.0f, mb.bandLevelDb (b));
                f.mbGr[(size_t) b] += -mb.gainReductionDb (b);
            }
            f.busLevel += std::max (-100.0f, h.chain.getBusComp().detectorLevelDb());
            f.busGr += -h.chain.getBusComp().gainReductionDb();
            for (int b = 0; b < 2; ++b)
                eqSum[(size_t) b] += std::max (-100.0f, h.chain.getEq().detectorLevelDb (b));
            f.clipDb += h.chain.getLimiter().clipReductionDb();
            if (const float* tr = h.chain.limiterTrace())
            {
                double sum = 0.0;
                for (int i = 0; i < len; ++i)
                    sum += tr[i];
                grMeans.push_back (sum / len);
            }
            ++meterBlocks;
        }
    }
    if (meterBlocks > 0)
    {
        for (int b = 0; b < 5; ++b)
        {
            f.mbLevel[(size_t) b] /= meterBlocks;
            f.mbGr[(size_t) b] /= meterBlocks;
        }
        f.busLevel = f.busLevel / meterBlocks;
        f.busGr /= meterBlocks;
        f.clipDb /= meterBlocks;
        for (size_t b = 0; b < 2; ++b)
            f.eqLevel[b] = eqSum[b] / meterBlocks;
    }
    if (grMeans.size() > 2)
    {
        const double mean = std::accumulate (grMeans.begin(), grMeans.end(), 0.0) / (double) grMeans.size();
        f.grMean = -mean;
        double var = 0.0;
        for (auto g : grMeans)
            var += sq (g - mean);
        f.grStd = std::sqrt (var / (double) grMeans.size());
    }
    auto& a = h.analyzer;
    f.tonal = a.tonalIntegrated();
    f.crest = a.crestDb();
    for (int b = 0; b < 4; ++b)
        f.width[(size_t) b] = a.stereoBand (b, false).widthPercent;
    f.lufs = a.loudness().integrated();
    f.truePeak = a.loudness().truePeakDb();
    f.harshness = a.harshnessDb();
    f.lowCorr = a.lowBelowMono().correlation;
    return f;
}

// ---- Matcher -----------------------------------------------------------------------------------------------
int Matcher::plannedEvaluations (Mode m)
{
    // neutral + formula and corrections + search + final check
    switch (m)
    {
        case Mode::fast: return 1 + 4 + 3;
        case Mode::hybrid: return 1 + 4 + 12 * 10 + 1 + 4 + 3;
        case Mode::precise: return 1 + 16 * 22 + 1 + 4 + 3;
    }
    return 1;
}

Matcher::Matcher (Mode m, double sampleRate, const ParamMap& current, const ParamMap& fixed, juce::AudioBuffer<float> excerpt, int pre, Target t, int threads)
    : juce::Thread ("MasterMind Auto match"), mode (m), fs (sampleRate), base (current), audio (std::move (excerpt)), preroll (pre), target (t)
{
    for (const auto& [id, v] : fixed)
        base[id] = v;
    for (const auto& d : dims())
        base[d.id] = d.neutral;
    // every pass runs the user's limiter setting, so the search and the final check agree
    const int n = std::clamp (threads, 1, 8);
    for (int i = 0; i < n; ++i)
        twins.push_back (std::make_unique<Twin> (fs, base));
    twins.push_back (std::make_unique<Twin> (fs, base)); // the last one: final check with the real settings
    plannedEvals = plannedEvaluations (m);
    // the knobs the result sets: the fixed setup plus every dimension
    result = fixed;
}

Matcher::~Matcher() { stopThread (10000); }

Features Matcher::eval (int worker, const ParamMap& p)
{
    auto f = twins[(size_t) worker]->run (p, audio, preroll);
    ++evals;
    return f;
}

void Matcher::evalBatch (std::vector<ParamMap>& ps, std::vector<Features>& out)
{
    out.assign (ps.size(), {});
    const int workers = (int) twins.size() - 1;
    std::vector<std::thread> pool;
    for (int w = 0; w < workers; ++w)
        pool.emplace_back ([&, w]
        {
            for (size_t i = (size_t) w; i < ps.size(); i += (size_t) workers)
                if (! threadShouldExit())
                    out[i] = eval (w, ps[i]);
        });
    for (auto& t : pool)
        t.join();
}

ParamMap Matcher::formula (const ParamMap& start, const Features& f0)
{
    ParamMap p = start;
    // EQ: the tonal difference, a little less than all of it (the dynamics move the balance too)
    double missingLow = 0.0;
    const auto dev = tonalDeviation (f0.tonal, target.tonal);
    for (size_t b = 0; b < 6; ++b)
    {
        const auto& d = dims()[(size_t) dimEq0 + b];
        const float want = std::round (-8.5f * (float) dev[b]) / 10.0f;
        p[d.id] = std::clamp (want, d.lo, d.hi);
        if (b < 2)
            missingLow = std::max (missingLow, (double) (want - d.hi));
    }
    // what the capped low boost cannot give comes from harmonics (Translator, low band Saturation)
    p["lem_tr_amount"] = (float) std::round (std::clamp (missingLow * 12.0, 0.0, 50.0));
    p[params::sat (1, "drive")] = (float) std::clamp (std::round ((2.0 + missingLow) * 10.0) / 10.0, 0.0, 8.0);
    // compression: target gain reduction from the crest factor above the reference (D-031)
    double excess = 0.0;
    if (target.hasCrest)
    {
        const auto ours = crest5 (f0.crest), refs = crest5 (target.crest);
        for (size_t b = 0; b < 5; ++b)
        {
            const double e = ours[b] - refs[b];
            excess += e / 5.0;
            mbTargetGr[b] = e > 0.5 ? std::clamp (e, 0.5, 4.0) : 0.0;
            p[dims()[(size_t) dimMb0 + b].id] = mbTargetGr[b] > 0.0 ? (float) std::clamp (f0.mbLevel[b] - 2.0 * mbTargetGr[b], -40.0, 0.0) : 0.0f;
        }
    }
    p["bus_thr"] = f0.busLevel > -90.0 ? (float) std::clamp (f0.busLevel - 3.0, -30.0, 0.0) : 0.0f;
    // Inflator: density from the mean crest factor above the reference, up to 50 % (D-038)
    p["infl_effect"] = (float) std::round (std::clamp (excess * 12.0, 0.0, 50.0));
    // width from the side share of the reference
    if (target.hasWidth)
    {
        const double mixMid = 0.5 * (f0.width[1] + f0.width[2]), mixHigh = f0.width[3];
        const double refMid = 0.5 * (target.width[1] + target.width[2]), refHigh = target.width[3];
        if (mixMid > 1.0 && refMid > 1.0)
            p["st_mid_width"] = (float) std::round (std::clamp (100.0 * std::pow (refMid / mixMid, 0.8), 60.0, 150.0));
        if (mixHigh > 1.0 && refHigh > 1.0)
            p["st_high_width"] = (float) std::round (std::clamp (100.0 * std::pow (refHigh / mixHigh, 0.8), 60.0, 160.0));
    }
    // loudness: the Maximizer gain of the Ozone assistant is our Limiter Drive
    p["lim_drive"] = (float) std::clamp (std::round ((target.lufs - f0.lufs) * 10.0) / 10.0, 0.0, 24.0);
    return p;
}

void Matcher::correct (ParamMap& p, const Features& f, const std::array<double, 5>& targetGr)
{
    const auto dev = tonalDeviation (f.tonal, target.tonal);
    for (size_t b = 0; b < 6; ++b)
    {
        const auto& d = dims()[(size_t) dimEq0 + b];
        auto& g = p[d.id];
        g = std::clamp (std::round ((g - 0.85f * (float) dev[b]) * 10.0f) / 10.0f, d.lo, d.hi);
    }
    for (size_t b = 0; b < 5; ++b)
        if (targetGr[b] > 0.0)
        {
            auto& thr = p[dims()[(size_t) dimMb0 + b].id];
            thr = (float) std::clamp (thr - 1.6 * (targetGr[b] - f.mbGr[b]), -40.0, 0.0);
        }
    p["bus_thr"] = (float) std::clamp (p["bus_thr"] - 1.6 * (1.5 - f.busGr), -30.0, 0.0);
    if (target.hasWidth)
    {
        const double mixMid = 0.5 * (f.width[1] + f.width[2]), mixHigh = f.width[3];
        const double refMid = 0.5 * (target.width[1] + target.width[2]), refHigh = target.width[3];
        if (mixMid > 1.0 && refMid > 1.0)
            p["st_mid_width"] = (float) std::round (std::clamp (p["st_mid_width"] * std::pow (refMid / mixMid, 0.8), 60.0, 150.0));
        if (mixHigh > 1.0 && refHigh > 1.0)
            p["st_high_width"] = (float) std::round (std::clamp (p["st_high_width"] * std::pow (refHigh / mixHigh, 0.8), 60.0, 160.0));
    }
    p["lim_drive"] = (float) std::clamp (std::round ((p["lim_drive"] + 0.9 * (target.lufs - f.lufs)) * 10.0) / 10.0, 0.0, 24.0);
}

std::vector<double> Matcher::search (const std::vector<double>& start, double sigma, int lambda, int generations, const std::vector<double>& prior, double priorWeight, ParamMap& searchBase,
                                     double& bestOut)
{
    // separable CMA-ES (Ros and Hansen 2008) on the unit cube; candidates outside are clamped
    const int n = (int) start.size();
    const int mu = lambda / 2;
    std::vector<double> w ((size_t) mu);
    for (int i = 0; i < mu; ++i)
        w[(size_t) i] = std::log (mu + 0.5) - std::log (i + 1.0);
    const double wsum = std::accumulate (w.begin(), w.end(), 0.0);
    for (auto& v : w)
        v /= wsum;
    double w2 = 0.0;
    for (auto v : w)
        w2 += v * v;
    const double mueff = 1.0 / w2;
    const double cs = (mueff + 2.0) / (n + mueff + 5.0);
    const double ds = 1.0 + 2.0 * std::max (0.0, std::sqrt ((mueff - 1.0) / (n + 1.0)) - 1.0) + cs;
    const double cc = (4.0 + mueff / n) / (n + 4.0 + 2.0 * mueff / n);
    double c1 = 2.0 / (sq (n + 1.3) + mueff), cmu = std::min (1.0 - c1, 2.0 * (mueff - 2.0 + 1.0 / mueff) / (sq (n + 2.0) + mueff));
    const double sepScale = (n + 2.0) / 3.0;
    c1 *= sepScale;
    cmu = std::min (1.0 - c1, cmu * sepScale);
    const double chiN = std::sqrt ((double) n) * (1.0 - 1.0 / (4.0 * n) + 1.0 / (21.0 * n * n));

    std::vector<double> m = start, C ((size_t) n, 1.0), ps ((size_t) n, 0.0), pc ((size_t) n, 0.0);
    std::vector<double> best = start;
    double bestCostV = bestOut;
    std::mt19937 rng (20261128u);
    std::normal_distribution<double> nd;
    for (int g = 0; g < generations && ! threadShouldExit(); ++g)
    {
        std::vector<std::vector<double>> ys ((size_t) lambda, std::vector<double> ((size_t) n)), xs = ys;
        std::vector<ParamMap> cands ((size_t) lambda, searchBase);
        for (int k = 0; k < lambda; ++k)
        {
            for (int i = 0; i < n; ++i)
            {
                ys[(size_t) k][(size_t) i] = std::sqrt (C[(size_t) i]) * nd (rng);
                xs[(size_t) k][(size_t) i] = std::clamp (m[(size_t) i] + sigma * ys[(size_t) k][(size_t) i], 0.0, 1.0);
            }
            fromUnit (xs[(size_t) k], cands[(size_t) k]);
        }
        std::vector<Features> fs_;
        evalBatch (cands, fs_);
        if (threadShouldExit())
            break;
        std::vector<std::pair<double, int>> order;
        for (int k = 0; k < lambda; ++k)
        {
            const double c = cost (fs_[(size_t) k], target, xs[(size_t) k], prior, priorWeight);
            order.push_back ({ c, k });
            if (c < bestCostV)
            {
                bestCostV = c;
                best = xs[(size_t) k];
                bestCost = (float) c;
            }
        }
        std::sort (order.begin(), order.end());
        std::vector<double> yw ((size_t) n, 0.0);
        for (int i = 0; i < mu; ++i)
            for (int d = 0; d < n; ++d)
                yw[(size_t) d] += w[(size_t) i] * ys[(size_t) order[(size_t) i].second][(size_t) d];
        double psNorm = 0.0;
        for (int d = 0; d < n; ++d)
        {
            m[(size_t) d] = std::clamp (m[(size_t) d] + sigma * yw[(size_t) d], 0.0, 1.0);
            ps[(size_t) d] = (1.0 - cs) * ps[(size_t) d] + std::sqrt (cs * (2.0 - cs) * mueff) * yw[(size_t) d] / std::sqrt (C[(size_t) d]);
            psNorm += sq (ps[(size_t) d]);
        }
        psNorm = std::sqrt (psNorm);
        const bool hsig = psNorm / std::sqrt (1.0 - std::pow (1.0 - cs, 2.0 * (g + 1))) < (1.4 + 2.0 / (n + 1.0)) * chiN;
        for (int d = 0; d < n; ++d)
        {
            pc[(size_t) d] = (1.0 - cc) * pc[(size_t) d] + (hsig ? std::sqrt (cc * (2.0 - cc) * mueff) : 0.0) * yw[(size_t) d];
            double rankMu = 0.0;
            for (int i = 0; i < mu; ++i)
                rankMu += w[(size_t) i] * sq (ys[(size_t) order[(size_t) i].second][(size_t) d]);
            C[(size_t) d] = (1.0 - c1 - cmu) * C[(size_t) d] + c1 * (sq (pc[(size_t) d]) + (hsig ? 0.0 : cc * (2.0 - cc) * C[(size_t) d])) + cmu * rankMu;
            C[(size_t) d] = std::clamp (C[(size_t) d], 1.0e-4, 4.0);
        }
        sigma = std::clamp (sigma * std::exp ((cs / ds) * (psNorm / chiN - 1.0)), 0.005, 0.5);
    }
    bestOut = bestCostV;
    return best;
}

void Matcher::run()
{
    ParamMap searchBase = base;
    const auto neutralUnit = toUnit (searchBase);

    // 1. neutral pass: the measurements for the formulas and the conditional fixes
    stage = 1;
    neutralFeatures = eval (0, searchBase);
    const auto& f0 = neutralFeatures;
    if (threadShouldExit())
        return;
    ParamMap fixes;
    // the dynamic Sub and Bass bands act above the average level of their band
    for (int b = 0; b < 2; ++b)
        if (f0.eqLevel[(size_t) b] > -90.0)
            fixes[params::eq (b + 1, "thr")] = (float) std::clamp (std::round (f0.eqLevel[(size_t) b] + 1.0), -60.0, 0.0);
    if (target.hasHarshness && f0.harshness - target.harshness > 1.5)
        for (auto [id, v] : { std::pair { "rs_on", 1.0f }, { "rs_low", 2000.0f }, { "rs_high", 8000.0f }, { "rs_depth", 4.0f } })
            fixes[id] = v;
    for (const auto& [id, v] : fixes)
    {
        searchBase[id] = v;
        base[id] = v;
        result[id] = v;
    }
    startCost = (float) cost (f0, target, neutralUnit, neutralUnit, 0.0);
    bestCost = startCost.load();

    ParamMap best = searchBase;
    double bestC = 1.0e30;
    const auto consider = [&] (const ParamMap& p, const Features& f)
    {
        const auto x = toUnit (p);
        const double c = cost (f, target, x, x, 0.0);
        if (c < bestC)
        {
            bestC = c;
            best = p;
            bestCost = (float) c;
        }
    };

    if (mode == Mode::fast || mode == Mode::hybrid)
    {
        // 2. formulas, then correction passes on the twin (Matchering does several RMS passes too)
        auto p = formula (searchBase, f0);
        const int passes = 4;
        for (int i = 0; i < passes && ! threadShouldExit(); ++i)
        {
            stage = 1;
            const auto f = eval (0, p);
            consider (p, f);
            correct (p, f, mbTargetGr);
        }
    }
    if (threadShouldExit())
        return;

    if (mode != Mode::fast)
    {
        // 3. search: Precise from neutral (Drive seeded from the loudness gap), Hybrid around the formula
        stage = 2;
        std::vector<double> start, prior;
        double sigma, priorWeight;
        int lambda, generations;
        if (mode == Mode::precise)
        {
            auto seed = searchBase;
            seed["lim_drive"] = (float) std::clamp (target.lufs - f0.lufs, 0.0, 24.0);
            start = toUnit (seed);
            prior = start;
            sigma = 0.22;
            priorWeight = 0.15;
            lambda = 16;
            generations = 22;
        }
        else
        {
            start = toUnit (best);
            prior = start;
            sigma = 0.07;
            priorWeight = 0.4;
            lambda = 12;
            generations = 10;
        }
        double sc = 1.0e30;
        const auto x = search (start, sigma, lambda, generations, prior, priorWeight, searchBase, sc);
        if (threadShouldExit())
            return;
        ParamMap p = searchBase;
        fromUnit (x, p);
        auto f = eval (0, p);
        consider (p, f);
        // two correction passes from the search result (tonal balance and loudness)
        for (int i = 0; i < 2 && ! threadShouldExit(); ++i)
        {
            p = best;
            const auto fb = eval (0, p);
            correct (p, fb, mbTargetGr);
            f = eval (0, p);
            consider (p, f);
        }
    }

    // 4. final check with the user's limiter setting: one loudness correction
    stage = 3;
    ParamMap fin = base;
    for (const auto& d : dims())
        fin[d.id] = best[d.id];
    auto& last = twins.back();
    finalFeatures = last->run (fin, audio, preroll);
    ++evals;
    // loudness: Drive by the secant method (a limiter that works hard gives less than 1 LU per dB)
    double slope = 0.9;
    for (int pass = 0; pass < 3 && std::abs (finalFeatures.lufs - target.lufs) > 0.2 && ! threadShouldExit(); ++pass)
    {
        const float d0 = fin["lim_drive"];
        const double l0 = finalFeatures.lufs;
        const float d1 = (float) std::clamp (std::round ((d0 + (target.lufs - l0) / slope) * 10.0) / 10.0, 0.0, 24.0);
        if (std::abs (d1 - d0) < 0.05f)
            break;
        fin["lim_drive"] = d1;
        finalFeatures = last->run (fin, audio, preroll);
        ++evals;
        slope = std::clamp ((finalFeatures.lufs - l0) / (double) (d1 - d0), 0.25, 1.2);
    }
    ParamMap rounded = fin;
    fromUnit (toUnit (fin), rounded); // the knob steps of the plugin
    for (const auto& d : dims())
        result[d.id] = rounded[d.id];
    const auto xf = toUnit (fin);
    bestCost = (float) cost (finalFeatures, target, xf, xf, 0.0);
    stage = 4;
    done = true;
}
// ---- LoudnessSearch ------------------------------------------------------------------------------------------
double LoudnessSearch::artefactCost (const Features& f, const ParamMap& p)
{
    const auto v = [&p] (const char* id) { const auto it = p.find (id); return it != p.end() ? (double) it->second : 0.0; };
    return sq (f.grMean / 4.0)                  // heavy limiting: distortion of the bass and the attacks
           + sq (f.grStd / 1.0)                 // pumping
           + 0.7 * sq (f.clipDb / 3.0)          // clipping
           + 0.5 * sq (v ("infl_effect") / 50.0) // density of the Inflator
           + 0.5 * sq (v ("lim_soften") / 50.0)  // softer drums
           + 0.3 * sq (v ("lim_density") / 100.0);
}

LoudnessSearch::LoudnessSearch (double sampleRate, const ParamMap& current, juce::AudioBuffer<float> excerpt, int pre, double targetLufs, int threads)
    : juce::Thread ("MasterMind Reach target"), fs (sampleRate), base (current), audio (std::move (excerpt)), preroll (pre), target (targetLufs)
{
    for (auto [id, v] : { std::pair { "clip_on", 1.0f }, { "lim_on", 1.0f } })
        base[id] = v;
    base["lim_ceiling"] = std::min (base.count ("lim_ceiling") > 0 ? base["lim_ceiling"] : -1.0f, -1.0f);
    const int n = std::clamp (threads, 1, 8);
    for (int i = 0; i < n; ++i)
        twins.push_back (std::make_unique<Twin> (fs, base));
}

LoudnessSearch::~LoudnessSearch() { stopThread (10000); }

LoudnessSearch::Outcome LoudnessSearch::solve (int worker, const Candidate& c)
{
    Outcome o;
    o.p = base;
    o.p["infl_effect"] = c.infl;
    o.p["lim_clip"] = c.clip;
    o.p["lim_density"] = c.density;
    o.p["lim_soften"] = c.soften;
    o.p["lim_leveler"] = c.leveler;
    // Drive by the secant method, from the current Drive
    float d = std::clamp (base.count ("lim_drive") > 0 ? base["lim_drive"] : 0.0f, 0.0f, 24.0f);
    double slope = 0.8, lastL = 0.0;
    float lastD = -1.0f;
    for (int it = 0; it < 4 && ! threadShouldExit(); ++it)
    {
        o.p["lim_drive"] = d;
        o.f = twins[(size_t) worker]->run (o.p, audio, preroll);
        ++evals;
        const double err = target - o.f.lufs;
        if (std::abs (err) <= 0.25)
        {
            o.ok = true;
            break;
        }
        if (lastD >= 0.0f && std::abs (d - lastD) > 0.05f)
            slope = std::clamp ((o.f.lufs - lastL) / (double) (d - lastD), 0.15, 1.2);
        lastD = d;
        lastL = o.f.lufs;
        const float next = (float) std::clamp (std::round ((d + err / slope) * 10.0) / 10.0, 0.0, 24.0);
        if (std::abs (next - d) < 0.05f)
            break; // at a limit of the Drive
        d = next;
    }
    return o;
}

void LoudnessSearch::run()
{
    const bool bands = base.count ("lim_bands") > 0 && base["lim_bands"] > 0.5f;
    std::vector<Candidate> cands = {
        { 0, 0, 0, 0, 0 },   { 20, 0, 0, 0, 0 },   { 40, 0, 0, 0, 0 },   { 0, 3, 0, 0, 0 },    { 0, 6, 0, 0, 0 },
        { 20, 3, 0, 0, 50 }, { 20, 6, 0, 0, 50 },  { 0, 3, 50, 0, 50 },  { 20, 3, 50, 0, 50 }, { 40, 3, 0, 0, 50 },
        { 40, 6, 50, 0, 50 },
    };
    if (bands) // Soften works with the band limiter only
        for (const Candidate c : { Candidate { 20, 3, 0, 30, 50 }, Candidate { 20, 6, 50, 30, 50 }, Candidate { 0, 6, 50, 30, 50 } })
            cands.push_back (c);
    plannedEvals = (int) cands.size() * 3;
    std::vector<Outcome> out (cands.size());
    std::vector<std::thread> pool;
    const int workers = (int) twins.size();
    for (int w = 0; w < workers; ++w)
        pool.emplace_back ([&, w]
        {
            for (size_t i = (size_t) w; i < cands.size(); i += (size_t) workers)
                if (! threadShouldExit())
                    out[i] = solve (w, cands[i]);
        });
    for (auto& t : pool)
        t.join();
    if (threadShouldExit())
        return;
    // the cleanest candidate that reaches the target; without one, the loudest
    int best = -1;
    double bestCost = 1.0e30, bestLufs = -1.0e30;
    for (size_t i = 0; i < out.size(); ++i)
    {
        if (out[i].ok)
        {
            const double c = artefactCost (out[i].f, out[i].p);
            if (best < 0 || ! out[(size_t) best].ok || c < bestCost)
            {
                best = (int) i;
                bestCost = c;
            }
        }
        else if ((best < 0 || ! out[(size_t) best].ok) && out[i].f.lufs > bestLufs)
        {
            best = (int) i;
            bestLufs = out[i].f.lufs;
        }
    }
    if (best >= 0)
    {
        const auto& o = out[(size_t) best];
        for (const auto* id : { "clip_on", "lim_on", "lim_ceiling", "infl_effect", "lim_clip", "lim_density", "lim_soften", "lim_leveler", "lim_drive" })
            result[id] = o.p.at (id);
        reachedLufs = o.f.lufs;
        reached = o.ok;
        artefacts = artefactCost (o.f, o.p);
    }
    done = true;
}
} // namespace mm::automatch
