#include "analysis/Engine.h"

namespace mm::analysis
{
// ---- GR histogram --------------------------------------------------------------------------------------
void GrHistogram::reset()
{
    whole.fill (0);
    for (auto& s : perSecond)
        s.fill (0);
    current = filledSeconds = 0;
}

void GrHistogram::add (float grDb)
{
    const int b = std::clamp ((int) (-grDb / binWidth), 0, bins - 1);
    ++whole[(size_t) b];
    ++perSecond[(size_t) current][(size_t) b];
}

void GrHistogram::tickSecond()
{
    current = (current + 1) % (int) perSecond.size();
    perSecond[(size_t) current].fill (0);
    filledSeconds = std::min (filledSeconds + 1, (int) perSecond.size());
}

std::array<uint64_t, GrHistogram::bins> GrHistogram::counts (int seconds) const
{
    if (seconds <= 0)
        return whole;
    std::array<uint64_t, bins> out {};
    const int n = std::min (seconds, (int) perSecond.size());
    for (int s = 0; s < n; ++s)
    {
        const auto& h = perSecond[(size_t) ((current - s + (int) perSecond.size()) % (int) perSecond.size())];
        for (int b = 0; b < bins; ++b)
            out[(size_t) b] += h[(size_t) b];
    }
    return out;
}

// ---- Engine --------------------------------------------------------------------------------------------
Engine::Engine() : juce::Thread ("MasterMind analysis")
{
    fifoData.assign ((size_t) capacity * frameWidth, 0.0f);
    sgIn.resize (1500);
    sgOut.resize (1500);
}

Engine::~Engine() { shutdown(); }

void Engine::shutdown() { stopThread (3000); }

void Engine::prepare (double sampleRate)
{
    {
        const juce::ScopedLock sl (prepareLock);
        fs = sampleRate;
        currentOrder = std::clamp (fftOrder.load(), 12, 14);
        inTap.prepare (fs, currentOrder);
        outTap.prepare (fs, currentOrder);
        clipHist.reset();
        limHist.reset();
        samplesToSecond = 0;
        rtIn.clear();
        rtOut.clear();
        fifo.reset();
        colRate.store (inTap.spectrogramColumnRate());
        sgInSeen = sgOutSeen = 0;
        prepared.store (true);
    }
    if (! isThreadRunning())
        startThread (juce::Thread::Priority::low);
}

void Engine::push (const float* inL, const float* inR, const float* outL, const float* outR, float clipGrDb,
                   const float* limGr, int numSamples) noexcept
{
    if (! prepared.load (std::memory_order_relaxed))
        return;
    int start1, size1, start2, size2;
    fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
    const int written = size1 + size2;
    if (written < numSamples)
        dropped.fetch_add ((uint64_t) (numSamples - written), std::memory_order_relaxed);

    const auto write = [&] (int dest, int count, int srcOffset)
    {
        float* d = fifoData.data() + (size_t) dest * frameWidth;
        for (int i = 0; i < count; ++i)
        {
            const int s = srcOffset + i;
            d[0] = inL[s];
            d[1] = inR[s];
            d[2] = outL[s];
            d[3] = outR[s];
            d[4] = clipGrDb;
            d[5] = limGr != nullptr ? limGr[s] : 0.0f;
            d += frameWidth;
        }
    };
    if (size1 > 0)
        write (start1, size1, 0);
    if (size2 > 0)
        write (start2, size2, size1);
    fifo.finishedWrite (written);
}

reference::TrackAnalysis Engine::makePassport (const juce::String& trackName)
{
    const juce::ScopedLock sl (prepareLock);
    auto p = reference::fromAnalyzer (outTap, (double) samplesSinceReset / fs);
    const auto& h = outTap.loudness().shortTermHistory();
    p.shortTerm.assign (h.begin(), h.end());
    p.name = trackName;
    return p;
}

std::vector<float> Engine::outSpectrumNormalised()
{
    const juce::ScopedLock sl (prepareLock);
    return reference::normalisedSpectrum (outTap);
}

void Engine::setTargets (const Targets& t)
{
    const juce::SpinLock::ScopedLockType sl (targetLock);
    targets = t;
}

Targets Engine::getTargets() const
{
    const juce::SpinLock::ScopedLockType sl (targetLock);
    return targets;
}

void Engine::setThresholds (const Thresholds& t)
{
    const juce::SpinLock::ScopedLockType sl (targetLock);
    thresholds = t;
}

void Engine::run()
{
    constexpr int chunk = 2048;
    std::vector<float> inL (chunk), inR (chunk), outL (chunk), outR (chunk);

    while (! threadShouldExit())
    {
        bool didWork = false;
        {
            const juce::ScopedLock sl (prepareLock);
            if (prepared.load())
            {
                const int wantOrder = std::clamp (fftOrder.load(), 12, 14);
                if (wantOrder != currentOrder)
                {
                    currentOrder = wantOrder;
                    inTap.prepare (fs, currentOrder);
                    outTap.prepare (fs, currentOrder);
                    rtIn.clear();
                    rtOut.clear();
                }
                if (resetRequested.exchange (false))
                {
                    samplesSinceReset = 0;
                    inTap.reset();
                    outTap.reset();
                    clipHist.reset();
                    limHist.reset();
                    rtIn.clear();
                    rtOut.clear();
                }
                inTap.setMonoFrequency (monoFreq.load());
                outTap.setMonoFrequency (monoFreq.load());
                inTap.loudness().setTruePeakCeiling (ceiling.load());
                outTap.loudness().setTruePeakCeiling (ceiling.load());

                for (int guard = 0; guard < 64; ++guard)
                {
                    int start1, size1, start2, size2;
                    fifo.prepareToRead (chunk, start1, size1, start2, size2);
                    const int n = size1 + size2;
                    if (n == 0)
                        break;
                    const auto read = [&] (int src, int count, int destOffset)
                    {
                        const float* s = fifoData.data() + (size_t) src * frameWidth;
                        for (int i = 0; i < count; ++i)
                        {
                            const int d = destOffset + i;
                            inL[(size_t) d] = s[0];
                            inR[(size_t) d] = s[1];
                            outL[(size_t) d] = s[2];
                            outR[(size_t) d] = s[3];
                            clipHist.add (s[4]);
                            limHist.add (s[5]);
                            s += frameWidth;
                        }
                    };
                    if (size1 > 0)
                        read (start1, size1, 0);
                    if (size2 > 0)
                        read (start2, size2, size1);
                    fifo.finishedRead (n);

                    inTap.process (inL.data(), inR.data(), n);
                    outTap.process (outL.data(), outR.data(), n);
                    samplesSinceReset += (uint64_t) n;
                    samplesToSecond += n;
                    while (samplesToSecond >= (int) fs)
                    {
                        samplesToSecond -= (int) fs;
                        clipHist.tickSecond();
                        limHist.tickSecond();
                    }
                    didWork = true;
                }

                // new spectrogram columns into the shared rings
                const auto pull = [this] (TapAnalyzer& a, std::vector<std::array<float, TapAnalyzer::spectrogramBins>>& ring,
                                          uint64_t& count, size_t& seen)
                {
                    const auto total = a.spectrogramTotal();
                    const auto& cols = a.spectrogram();
                    const size_t fresh = (size_t) std::min<uint64_t> (total - seen, cols.size());
                    if (fresh == 0)
                        return;
                    const juce::SpinLock::ScopedLockType sl2 (sgLock);
                    for (size_t i = cols.size() - fresh; i < cols.size(); ++i)
                    {
                        ring[(size_t) (count % ring.size())] = cols[i];
                        ++count;
                    }
                    seen = (size_t) total;
                };
                pull (inTap, sgIn, sgInCount, sgInSeen);
                pull (outTap, sgOut, sgOutCount, sgOutSeen);

                const double now = juce::Time::getMillisecondCounterHiRes();
                if (now - lastBuild >= 33.0)
                {
                    lastBuild = now;
                    buildSnapshot();
                }
            }
        }
        if (! didWork)
            wait (10);
    }
}

void Engine::fillView (TapAnalyzer& a, TapView& v, std::vector<float>& rtState)
{
    const auto& m = a.loudness();
    v.momentary = m.momentary();
    v.shortTerm = m.shortTerm();
    v.integrated = m.integrated();
    v.lra = m.loudnessRange();
    v.maxMomentary = m.maxMomentary();
    v.maxShortTerm = m.maxShortTerm();
    v.truePeak = m.truePeakDb();
    v.recentTruePeak = m.recentTruePeakDb();
    v.samplePeak = m.samplePeakDb();
    v.crest = m.crestDb();
    v.plr = v.integrated > -70.0 ? v.truePeak - v.integrated : 0.0;
    v.psr = v.shortTerm > -70.0 ? v.recentTruePeak - v.shortTerm : 0.0;
    v.sampleOvers = m.sampleOvers();
    v.truePeakOvers = m.truePeakOvers();
    v.history.assign (m.shortTermHistory().begin(), m.shortTermHistory().end());
    v.hasSignal = v.integrated > -70.0;

    const auto ch = (SpecChannel) std::clamp (channel.load(), 0, numSpecChannels - 1);
    const int sm = smoothing.load();
    const double tl = tilt.load();
    powerToDisplay (a.averagePower (ch), fs, a.fftSize(), snap.freqs, sm, tl, v.specAvg);
    powerToDisplay (a.peakPower (ch), fs, a.fftSize(), snap.freqs, sm, tl, v.specPeak);
    std::vector<float> fresh;
    powerToDisplay (a.framePower (ch), fs, a.fftSize(), snap.freqs, sm, tl, fresh);
    if (rtState.size() != fresh.size())
        rtState = fresh;
    const float decay = 20.0f * 0.033f; // real time with peak decay, about 20 dB/s
    for (size_t i = 0; i < fresh.size(); ++i)
        rtState[i] = std::max (fresh[i], rtState[i] - decay);
    v.specRt = rtState;

    v.tonalIntegrated = a.tonalIntegrated();
    v.tonalRecent = a.tonalRecent();
    v.crest6 = a.crestDb();
    v.harshness = a.harshnessDb();
    v.harshHistory.assign (a.harshnessHistory().begin(), a.harshnessHistory().end());

    v.broadShort = a.stereoBroadband (true);
    v.broadIntegrated = a.stereoBroadband (false);
    for (int b = 0; b < numStereoBands; ++b)
    {
        v.bandsShort[(size_t) b] = a.stereoBand (b, true);
        v.bandsIntegrated[(size_t) b] = a.stereoBand (b, false);
    }
    v.balanceDb = a.balanceDb();
    a.copyScope (v.scope);

    const auto lch = (SpecChannel) std::clamp (lowChannel.load(), 0, numSpecChannels - 1);
    powerToDisplay (a.lowAveragePower (lch), a.lowRate(), a.lowFftSize(), snap.lowFreqs, 1, 0.0, v.lowSpec);
    powerToDisplay (a.lowAveragePower (SpecChannel::mid), a.lowRate(), a.lowFftSize(), snap.lowFreqs, 1, 0.0, v.lowMid);
    powerToDisplay (a.lowAveragePower (SpecChannel::side), a.lowRate(), a.lowFftSize(), snap.lowFreqs, 1, 0.0, v.lowSide);
    a.copyLowWave (v.lowWave);
    v.asymmetry = a.peakAsymmetryPercent();
    v.subToBass = a.subToBassDb();
    v.lowMono = a.lowBelowMono();
}

void Engine::buildSnapshot()
{
    Snapshot s;
    if (snap.freqs.empty())
    {
        for (int i = 0; i < 600; ++i)
            snap.freqs.push_back (20.0 * std::pow (1000.0, i / 599.0));
        for (int i = 0; i < 300; ++i)
            snap.lowFreqs.push_back (20.0 * std::pow (10.0, i / 299.0));
    }
    s.freqs = snap.freqs;
    s.lowFreqs = snap.lowFreqs;
    s.sampleRate = fs;
    fillView (inTap, s.in, rtIn);
    fillView (outTap, s.out, rtOut);
    inShortTermLufs.store ((float) s.in.shortTerm);
    outShortTermLufs.store ((float) s.out.shortTerm);
    outIntegratedLufs.store ((float) s.out.integrated);
    s.clipHist = clipHist.counts (grSeconds.load());
    s.limHist = limHist.counts (grSeconds.load());
    s.preLimiterSampleOvers = preLimiterSampleOvers.load();
    s.preLimiterTpOvers = preLimiterTpOvers.load();
    s.droppedFrames = dropped.load();

    Thresholds th;
    {
        const juce::SpinLock::ScopedLockType sl (targetLock);
        s.targets = targets;
        th = thresholds;
    }
    s.targets.truePeakCeiling = ceiling.load();
    CheckInput ci;
    ci.tonal = tonalLast10s.load() ? s.out.tonalRecent : s.out.tonalIntegrated;
    ci.monoLossDb = s.out.broadIntegrated.monoLossDb;
    ci.lowCorrelation = s.out.lowMono.correlation;
    ci.plr = s.out.plr;
    ci.harshness = s.out.harshness;
    ci.truePeak = s.out.truePeak;
    ci.integrated = s.out.integrated;
    ci.hasSignal = s.out.hasSignal;
    s.checks = computeChecks (ci, s.targets, th);

    const juce::SpinLock::ScopedLockType sl (snapLock);
    s.serial = ++serial;
    snap = std::move (s);
}

bool Engine::copySnapshot (Snapshot& dest, uint64_t lastSerial) const
{
    const juce::SpinLock::ScopedLockType sl (snapLock);
    if (snap.serial == lastSerial)
        return false;
    dest = snap;
    return true;
}

uint64_t Engine::copySpectrogram (bool fromOut, uint64_t sinceIndex, std::vector<std::array<float, TapAnalyzer::spectrogramBins>>& out) const
{
    const juce::SpinLock::ScopedLockType sl (sgLock);
    const auto& ring = fromOut ? sgOut : sgIn;
    const uint64_t count = fromOut ? sgOutCount : sgInCount;
    const uint64_t first = std::max<uint64_t> (sinceIndex, count > ring.size() ? count - ring.size() : 0);
    out.clear();
    for (uint64_t i = first; i < count; ++i)
        out.push_back (ring[(size_t) (i % ring.size())]);
    return count;
}
} // namespace mm::analysis
