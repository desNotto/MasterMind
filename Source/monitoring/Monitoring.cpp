#include "monitoring/Monitoring.h"

namespace mm::monitoring
{
namespace
{
constexpr double bandEdges[7] = { 20.0, 60.0, 150.0, 500.0, 2000.0, 6000.0, 16000.0 };
} // namespace

void ReferenceAudio::computeOverview()
{
    wavePeak.assign ((size_t) waveBins, 0.0f);
    waveRms.assign ((size_t) waveBins, 0.0f);
    const int n = audio.getNumSamples();
    if (n <= 0)
        return;
    const int chans = audio.getNumChannels();
    for (int b = 0; b < waveBins; ++b)
    {
        const int from = (int) ((juce::int64) n * b / waveBins);
        const int to = std::max (from + 1, (int) ((juce::int64) n * (b + 1) / waveBins));
        float pk = 0.0f;
        double sum = 0.0;
        for (int ch = 0; ch < chans; ++ch)
        {
            const float* d = audio.getReadPointer (ch);
            for (int i = from; i < std::min (to, n); ++i)
            {
                pk = std::max (pk, std::abs (d[i]));
                sum += (double) d[i] * d[i];
            }
        }
        wavePeak[(size_t) b] = pk;
        waveRms[(size_t) b] = (float) std::sqrt (sum / std::max (1, (to - from) * chans));
    }
}

void Monitoring::Chain::apply (const FilterSet* set)
{
    applied = set;
    if (set == nullptr)
        return;
    const auto& s = set->sections;
    const int first = std::min ((int) s.size(), dsp::Cascade::maxSections);
    const int second = std::min ((int) s.size() - first, dsp::Cascade::maxSections);
    for (int ch = 0; ch < 2; ++ch)
    {
        cascades[(size_t) ch].setSections (s.data(), first);
        cascades2[(size_t) ch].setSections (s.data() + first, std::max (0, second));
        cascades[(size_t) ch].reset();
        cascades2[(size_t) ch].reset();
    }
}

void Monitoring::Chain::process (float* l, float* r, int n) noexcept
{
    if (applied == nullptr)
        return;
    const double g = applied->preampGain;
    float* io[2] = { l, r };
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < n; ++i)
            io[ch][i] = (float) cascades2[(size_t) ch].process (cascades[(size_t) ch].process (io[ch][i] * g));
    if (applied->mono)
        for (int i = 0; i < n; ++i)
            l[i] = r[i] = 0.5f * (l[i] + r[i]);
    if (applied->softLimit)
        for (int i = 0; i < n; ++i)
        {
            l[i] = (float) std::tanh (l[i] * 1.3) / 1.3f;
            r[i] = (float) std::tanh (r[i] * 1.3) / 1.3f;
        }
}

Monitoring::Monitoring() = default;

void Monitoring::prepare (double sampleRate, int maxBlockSize)
{
    fs = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    scratch.setSize (2, maxBlock, false, true, false);
    smoothCoeff = std::exp (-1.0 / (0.5 * fs / 64.0)); // per 64-sample step, ~0.5 s
    const juce::dsp::ProcessSpec spec { fs, (juce::uint32) maxBlock, 2 };
    for (auto& c : irConv)
        c.prepare (spec);
    bandForFilters = -2;
    matchGain = refGain = 1.0;
}

void Monitoring::setCorrection (int slot, std::unique_ptr<FilterSet> set)
{
    correction[std::clamp (slot, 0, 1)].publish (std::move (set));
}

void Monitoring::setCorrectionImpulse (int slot, juce::AudioBuffer<float> ir, double irSampleRate)
{
    slot = std::clamp (slot, 0, 1);
    irConv[(size_t) slot].loadImpulseResponse (std::move (ir), irSampleRate, juce::dsp::Convolution::Stereo::yes,
                                               juce::dsp::Convolution::Trim::yes, juce::dsp::Convolution::Normalise::no);
    irLoaded[(size_t) slot].store (true);
}

void Monitoring::setSystem (std::unique_ptr<FilterSet> set) { system.publish (std::move (set)); }

void Monitoring::setReference (std::unique_ptr<ReferenceAudio> ref)
{
    refShadow.store (ref.get());
    reference.publish (std::move (ref));
}

void Monitoring::collectGarbage()
{
    correction[0].collect();
    correction[1].collect();
    system.collect();
    reference.collect();
}

bool Monitoring::isActive() const noexcept
{
    return correctionSlot.load() != 0 || systemIndex.load() >= 0 || listenMode.load() != 0 || bandSolo.load() >= 0
        || deltaListen.load() || chainBypass.load() || referenceActive.load();
}

std::unique_ptr<Monitoring::FilterSet> Monitoring::buildCorrection (const ParametricProfile& p, double fs, double strength,
                                                                    double boostLimitDb, bool cutOnlyBelow300)
{
    auto set = std::make_unique<FilterSet>();
    std::vector<FilterSpec> scaled;
    for (auto f : p.filters)
    {
        f.gainDb *= strength;
        f.gainDb = std::min (f.gainDb, boostLimitDb);
        if (cutOnlyBelow300 && f.freq < 300.0 && (f.type == FilterSpec::peak || f.type == FilterSpec::lowShelf))
            f.gainDb = std::min (f.gainDb, 0.0);
        scaled.push_back (f);
        for (const auto& c : design (f, fs))
            set->sections.push_back (c);
    }
    // automatic preamp: the corrected response never boosts above 0 dB
    double maxDb = -100.0;
    for (int i = 0; i < 400; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, i / 399.0);
        double db = 0.0;
        for (const auto& c : set->sections)
            db += c.magnitudeDb (fs, f);
        maxDb = std::max (maxDb, db);
    }
    set->preampGain = dsp::dbToGain (-std::max (0.0, maxDb));
    juce::ignoreUnused (scaled);
    return set;
}

std::unique_ptr<Monitoring::FilterSet> Monitoring::buildSystem (const SystemPreset& p, double fs)
{
    auto set = std::make_unique<FilterSet>();
    for (const auto& f : p.filters)
        for (const auto& c : design (f, fs))
            set->sections.push_back (c);
    set->mono = p.mono;
    set->softLimit = p.softLimit;
    return set;
}

bool Monitoring::process (juce::AudioBuffer<float>& buffer, const juce::AudioBuffer<float>& delayedInput, bool nonRealtime) noexcept
{
    // Adopt new objects even when bypassed, so the audio thread owns the latest state.
    const FilterSet* corr[2] = { correction[0].get(), correction[1].get() };
    const FilterSet* sys = system.get();
    ReferenceAudio* ref = reference.get();

    if (nonRealtime) // spec 6.0 rule 2: the whole monitoring section is bypassed when rendering
        return false;

    const int n = buffer.getNumSamples();
    float* l = buffer.getWritePointer (0);
    float* r = buffer.getWritePointer (1);
    const float* dl = delayedInput.getReadPointer (0);
    const float* dr = delayedInput.getReadPointer (1);
    bool changed = false;

    // ---- 1. Source: reference (B), loudness-matched chain bypass, delta listen ---------------------
    // A paused reference keeps its player and position, but the project plays (mockup v19)
    const bool refLoaded = referenceActive.load() && ref != nullptr && ref->audio.getNumSamples() > 0;
    const bool useRef = refLoaded && refPlaying.load();
    if (refLoaded && ! useRef)
    {
        const double len = ref->audio.getNumSamples();
        refLengthSeconds.store (len / fs);
        if (ref != lastRef)
        {
            lastRef = ref;
            if (refPos >= len)
                refPos = 0.0;
        }
        const double seek = seekRequest.exchange (-1.0);
        if (seek >= 0.0)
            refPos = std::clamp (seek * fs, 0.0, len - 1.0);
        refPositionSeconds.store (refPos / fs);
    }
    if (useRef)
    {
        const double len = ref->audio.getNumSamples();
        refLengthSeconds.store (len / fs);
        if (ref != lastRef)
        {
            // another song: keep the position when it exists in the new one (switching 1-6)
            lastRef = ref;
            if (refPos >= len)
                refPos = 0.0;
        }
        const double seek = seekRequest.exchange (-1.0);
        if (seek >= 0.0)
            refPos = std::clamp (seek * fs, 0.0, len - 1.0);
        double loopA = refLoopStart.load() * fs, loopB = std::min (len, refLoopEnd.load() * fs);
        if (loopB <= loopA + 1.0)
        {
            loopA = 0.0;
            loopB = len;
        }
        if (refPos >= loopB || refPos < 0.0)
            refPos = loopA;

        // loudness match (spec 7.1): reference gain = our loudness minus reference loudness
        double targetDb = refManualGainDb.load();
        const int mode = refMatchMode.load();
        const auto stIndex = (size_t) std::clamp ((int) (refPos / fs * 10.0), 0, std::max (0, (int) ref->shortTerm.size() - 1));
        if (mode == (int) RefMatch::autoSection && ! ref->shortTerm.empty() && outShortTerm.load() > -70.0f && ref->shortTerm[stIndex] > -70.0f)
            targetDb = outShortTerm.load() - ref->shortTerm[stIndex];
        else if (mode == (int) RefMatch::autoWhole && outIntegrated.load() > -70.0f && ref->integrated > -70.0)
            targetDb = outIntegrated.load() - ref->integrated;
        else if (mode != (int) RefMatch::manual)
            targetDb = dsp::gainToDb (refGain); // keep the last gain while there is no measurement
        targetDb = std::clamp (targetDb, -24.0, 24.0);
        const double targetGain = dsp::dbToGain (targetDb);

        const float* rl = ref->audio.getReadPointer (0);
        const float* rr = ref->audio.getReadPointer (ref->audio.getNumChannels() > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
        {
            if ((i & 63) == 0)
                refGain = targetGain + smoothCoeff * (refGain - targetGain);
            const int p = (int) refPos;
            l[i] = (float) (rl[p] * refGain);
            r[i] = (float) (rr[p] * refGain);
            refPos += 1.0;
            if (refPos >= loopB)
                refPos = loopA;
        }
        refPositionSeconds.store (refPos / fs);
        appliedRefGainDb.store ((float) dsp::gainToDb (refGain));
        changed = true;
    }
    else if (chainBypass.load())
    {
        // loudness-matched A/B (spec 6.4): bypassed signal gets out minus in short-term loudness
        double targetDb = 0.0;
        if (matchOn.load() && inShortTerm.load() > -70.0f && outShortTerm.load() > -70.0f)
            targetDb = std::clamp ((double) (outShortTerm.load() - inShortTerm.load()), -12.0, 12.0);
        else if (matchOn.load())
            targetDb = dsp::gainToDb (matchGain);
        const double targetGain = dsp::dbToGain (targetDb);
        for (int i = 0; i < n; ++i)
        {
            if ((i & 63) == 0)
                matchGain = targetGain + smoothCoeff * (matchGain - targetGain);
            l[i] = (float) (dl[i] * matchGain);
            r[i] = (float) (dr[i] * matchGain);
        }
        appliedMatchDb.store ((float) dsp::gainToDb (matchGain));
        changed = true;
    }
    else if (deltaListen.load())
    {
        for (int i = 0; i < n; ++i)
        {
            l[i] -= dl[i];
            r[i] -= dr[i];
        }
        changed = true;
    }

    // ---- 2. Listening tools -----------------------------------------------------------------------
    switch ((Listen) listenMode.load())
    {
        case Listen::mono:
            for (int i = 0; i < n; ++i)
                l[i] = r[i] = 0.5f * (l[i] + r[i]);
            changed = true;
            break;
        case Listen::midOnly:
            for (int i = 0; i < n; ++i)
                l[i] = r[i] = 0.5f * (l[i] + r[i]);
            changed = true;
            break;
        case Listen::sideOnly:
            for (int i = 0; i < n; ++i)
            {
                const float s = 0.5f * (l[i] - r[i]);
                l[i] = s;
                r[i] = -s;
            }
            changed = true;
            break;
        case Listen::leftOnly:
            for (int i = 0; i < n; ++i)
                r[i] = l[i];
            changed = true;
            break;
        case Listen::rightOnly:
            for (int i = 0; i < n; ++i)
                l[i] = r[i];
            changed = true;
            break;
        case Listen::stereo: break;
    }

    const int band = bandSolo.load();
    if (band >= 0 && band < 6)
    {
        if (band != bandForFilters)
        {
            for (int ch = 0; ch < 2; ++ch)
            {
                bandLo[(size_t) ch].setup (8, fs, bandEdges[band]);
                bandHi[(size_t) ch].setup (8, fs, std::min (bandEdges[band + 1], fs * 0.45));
                bandLo[(size_t) ch].reset();
                bandHi[(size_t) ch].reset();
            }
            bandForFilters = band;
        }
        float* io[2] = { l, r };
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                double lo, hi, lo2, hi2;
                bandLo[(size_t) ch].process (io[ch][i], lo, hi);
                bandHi[(size_t) ch].process (hi, lo2, hi2);
                io[ch][i] = (float) lo2;
            }
        changed = true;
    }
    else
    {
        bandForFilters = -2;
    }

    // ---- 3. Monitor correction ----------------------------------------------------------------------
    const int slot = correctionSlot.load() - 1;
    if (slot >= 0 && slot < 2)
    {
        if (corr[slot] != nullptr && corr[slot]->useConvolution && irLoaded[(size_t) slot].load())
        {
            juce::dsp::AudioBlock<float> block (buffer);
            irConv[(size_t) slot].process (juce::dsp::ProcessContextReplacing<float> (block));
        }
        else if (corr[slot] != nullptr)
        {
            if (correctionChain[slot].applied != corr[slot])
                correctionChain[slot].apply (corr[slot]);
            correctionChain[slot].process (l, r, n);
        }
        changed = true;
    }

    // ---- 4. System emulation ------------------------------------------------------------------------
    if (systemIndex.load() >= 0 && sys != nullptr)
    {
        if (systemChain.applied != sys)
            systemChain.apply (sys);
        systemChain.process (l, r, n);
        changed = true;
    }
    return changed;
}
} // namespace mm::monitoring
