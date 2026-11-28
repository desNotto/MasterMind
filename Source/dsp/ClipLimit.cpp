#include "dsp/ClipLimit.h"

namespace mm::dsp
{
// ---- Clipper -----------------------------------------------------------------------------------------
double Clipper::clip (int type, double x, double t, double shape) noexcept
{
    const double a = std::abs (x);
    const double sgn = x < 0.0 ? -1.0 : 1.0;
    const double w = std::clamp (shape, 0.0, 1.0) * t * 0.9;

    const auto softTanh = [] (double v, double th, double knee)
    {
        if (knee <= 1.0e-9 || v <= th - knee)
            return std::min (v, th);
        return th - knee + knee * std::tanh ((v - (th - knee)) / knee);
    };

    switch (type)
    {
        case 0: return sgn * std::min (a, t);
        case 1: return sgn * softTanh (a, t, w);
        case 2:
        {
            if (w <= 1.0e-9 || a <= t - w)
                return sgn * std::min (a, t);
            if (a >= t + w)
                return sgn * t;
            const double d = a - t + w;
            return sgn * (a - d * d / (4.0 * w));
        }
        default: // Diode: positive half soft at t, negative half clips later and harder
            if (x >= 0.0)
                return softTanh (a, t, w);
            return -softTanh (a, std::min (1.0, t * 1.12), w * 0.4);
    }
}

void Clipper::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    os.prepare (2, 3, ctx.maxBlock); // 4x normal, 8x high
    capIn.assign ((size_t) captureSize, 0.0f);
    capOut.assign ((size_t) captureSize, 0.0f);
    scratchIn.assign ((size_t) std::max (1, ctx.maxBlock), 0.0f);
    reset();
}

void Clipper::reset()
{
    os.reset();
    lastRate = 0.0;
    for (auto& c : lowProt)
        c.reset();
    thr.reset (dbToGain (settings.thresholdDb));
    mix.reset (settings.mix);
    drive.reset (dbToGain (settings.driveDb));
    prMeter.set (0.0f);
}

int Clipper::readCapture (float* in, float* out, int maxSamples) noexcept
{
    const auto scope = capture.read (std::min (maxSamples, capture.getNumReady()));
    int n = 0;
    for (int k = 0; k < scope.blockSize1; ++k, ++n)
    {
        in[n] = capIn[(size_t) (scope.startIndex1 + k)];
        out[n] = capOut[(size_t) (scope.startIndex1 + k)];
    }
    for (int k = 0; k < scope.blockSize2; ++k, ++n)
    {
        in[n] = capIn[(size_t) (scope.startIndex2 + k)];
        out[n] = capOut[(size_t) (scope.startIndex2 + k)];
    }
    return n;
}

void Clipper::process (float* left, float* right, int numSamples) noexcept
{
    // Drive (gain before clipping), ramped; the mono input is kept for the waveform view
    drive.setTarget (dbToGain (settings.driveDb), numSamples);
    const int cap = std::min (numSamples, (int) scratchIn.size());
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = (float) drive.next();
        left[i] *= g;
        right[i] *= g;
        if (i < cap)
            scratchIn[(size_t) i] = 0.5f * (left[i] + right[i]);
    }
    float* chans[2] = { left, right };
    juce::dsp::AudioBlock<float> block (chans, 2, (size_t) numSamples);
    auto up = os.up (block);
    const double rate = fs * os.factor();
    if (rate != lastRate)
    {
        const auto hp = butterworth (4, true, rate, 150.0);
        for (auto& c : lowProt)
            c.setSections (hp);
        lastRate = rate;
    }

    const int n = (int) up.getNumSamples();
    thr.setTarget (dbToGain (settings.thresholdDb), n);
    mix.setTarget (settings.mix, n);
    double inPeak = 0.0, outPeak = 0.0;
    float* d[2] = { up.getChannelPointer (0), up.getChannelPointer (1) };
    for (int i = 0; i < n; ++i)
    {
        const double t = thr.next(), m = mix.next();
        for (int c = 0; c < 2; ++c)
        {
            const double x = d[c][i];
            double e = clip (settings.type, x, t, settings.shape) - x;
            if (settings.lowProtect)
                e = lowProt[(size_t) c].process (e); // the band below ~150 Hz is not clipped
            const double y = x + m * e;
            inPeak = std::max (inPeak, std::abs (x));
            outPeak = std::max (outPeak, std::abs (y));
            d[c][i] = (float) y;
        }
    }
    os.down (block);
    prMeter.set (inPeak > 1.0e-9 ? (float) std::min (0.0, gainToDb (outPeak / inPeak)) : 0.0f);
    inMeter.set ((float) gainToDb (inPeak + 1.0e-9));
    {
        const auto scope = capture.write (std::min (cap, capture.getFreeSpace()));
        int k = 0;
        for (int j = 0; j < scope.blockSize1; ++j, ++k)
        {
            capIn[(size_t) (scope.startIndex1 + j)] = scratchIn[(size_t) k];
            capOut[(size_t) (scope.startIndex1 + j)] = 0.5f * (left[k] + right[k]);
        }
        for (int j = 0; j < scope.blockSize2; ++j, ++k)
        {
            capIn[(size_t) (scope.startIndex2 + j)] = scratchIn[(size_t) k];
            capOut[(size_t) (scope.startIndex2 + j)] = 0.5f * (left[k] + right[k]);
        }
    }
}

// ---- Limiter -----------------------------------------------------------------------------------------
void Limiter::MinWindow::prepare (int capacity)
{
    val.assign ((size_t) capacity, 1.0);
    idx.assign ((size_t) capacity, 0);
    reset();
}

void Limiter::MinWindow::reset()
{
    std::fill (val.begin(), val.end(), 1.0);
    std::fill (idx.begin(), idx.end(), 0);
    head = tail = 0;
}

double Limiter::MinWindow::push (double v, int index, int span) noexcept
{
    const int cap = (int) val.size();
    while (head != tail)
    {
        const int last = (tail - 1 + cap) % cap;
        if (val[(size_t) last] < v)
            break;
        tail = last;
    }
    val[(size_t) tail] = v;
    idx[(size_t) tail] = index;
    tail = (tail + 1) % cap;
    while (index - idx[(size_t) head] > span)
        head = (head + 1) % cap;
    return val[(size_t) head];
}

void Limiter::MinWindow::shift (int by) noexcept
{
    for (auto& i : idx)
        i -= by;
}

void Limiter::Boxcar::prepare (int capacity)
{
    buf.assign ((size_t) capacity, 1.0);
}

void Limiter::Boxcar::reset (int length)
{
    len = std::clamp (length, 1, (int) buf.size());
    std::fill (buf.begin(), buf.end(), 1.0);
    pos = 0;
    sum = (double) len;
}

double Limiter::Boxcar::push (double v) noexcept
{
    sum += v - buf[(size_t) pos];
    buf[(size_t) pos] = v;
    if (++pos >= len)
        pos = 0;
    return sum / len;
}

void Limiter::Pipe::prepare (int holdCapacity, int lookCapacity)
{
    hold.prepare (holdCapacity);
    look.prepare (lookCapacity);
    box1.prepare (lookCapacity);
    box2.prepare (lookCapacity);
}

void Limiter::Pipe::reset (int window)
{
    hold.reset();
    look.reset();
    // two cascaded moving averages spanning `window` samples in total: a smooth attack that
    // still reaches the requirement exactly at the peak
    const int l1 = (window + 1) / 2;
    box1.reset (l1);
    box2.reset (window + 1 - l1);
    fastDb = slowDb = 0.0;
}

double Limiter::processPipe (Pipe& p, double required, const Coeffs& k) const noexcept
{
    // 1. hold: the deepest requirement of the last hold time. The gain cannot follow single cycles
    //    of the bass, which is what turns gain riding into audible distortion.
    const double held = p.hold.push (required, sampleIndex, k.holdSamples);
    const double needDb = held < 1.0 ? -gainToDb (held) : 0.0;

    // 2. fast (transient) stage: instant attack, short release
    p.fastDb = needDb > p.fastDb ? needDb : needDb + k.fastRel * (p.fastDb - needDb);
    // 3. slow program-dependent stage: follows the average reduction. The fast stage releases only
    //    down to this level, so dense music is not pumped and single hits recover quickly.
    if (k.slow)
        p.slowDb = needDb + (needDb > p.slowDb ? k.slowAtt : k.slowRel) * (p.slowDb - needDb);
    // Density (D-042): the fast stage may release up to densityDb below the slow stage, so the space
    // between hits fills up (louder, denser); 0 keeps the D-036 behaviour
    const double envDb = std::max (p.fastDb, k.slow ? p.slowDb - k.densityDb : 0.0);
    const double env = envDb > 0.0 ? dbToGain (-envDb) : 1.0;

    // 4. look-ahead: minimum over window + 2 samples, so both output samples around an
    //    inter-sample peak are covered, then the S-shaped attack
    const double wmin = p.look.push (env, sampleIndex, lookahead + 1);
    return p.box2.push (p.box1.push (wmin));
}

void Limiter::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    maxBlock = ctx.maxBlock;
    maxLookahead = (int) std::ceil (0.010 * fs) + 2; // up to 10 ms (D-042)
    maxHold = (int) std::ceil (0.03 * fs) + 2;
    for (auto& p : pipes)
        p.prepare (maxHold + 4, maxLookahead + 4);
    for (auto& d : delayBuf)
        d.assign ((size_t) (maxLookahead + TruePeakDetector8::latency() + 2), 0.0f);
    grTrace.assign ((size_t) maxBlock, 0.0f);
    if (! nested)
    {
        bank = std::make_unique<SpectralBank>();
        bank->prepare (fs);
        clip = std::make_unique<Clipper>();
        clip->settings.type = 0; // hard, 4x oversampled: only the emphasised attacks reach it
        clip->settings.mix = 1.0f;
        clip->settings.lowProtect = false;
        clip->settings.driveDb = 0.0f;
        clip->prepare (ctx);
        safety = std::make_unique<Limiter>();
        safety->nested = true;
        safety->settings.lookaheadMs = 1.0f;
        safety->settings.mode = 2;
        safety->prepare (ctx);
        for (auto* v : { &driven, &shaped, &full, &proc })
            for (auto& c : *v)
                c.assign ((size_t) maxBlock, 0.0f);
        tailBypass.prepare (2, clip->latency() + safety->latency() + 8);
        bandClip = std::make_unique<BandClipper>();
        bandClip->prepare (fs, maxBlock);
    }
    reset();
}

void Limiter::reset()
{
    activeBands = nested ? 0 : std::clamp (settings.bands, 0, 3);
    if (bank != nullptr)
    {
        bank->settings.bandSet = std::max (0, activeBands - 1);
        bank->reset();
        clip->reset();
        safety->reset();
        tailBypass.setDelay (clip->latency() + safety->latency());
        tailBypass.reset();
        tail = Tail::off;
        tailCount = 0;
    }
    lookahead = std::clamp ((int) std::lround (settings.lookaheadMs * 0.001 * fs), 1, maxLookahead);
    if (bandClip != nullptr)
    {
        bandClip->amountDb = settings.clipDb;
        bandClip->reset();
    }
    levelDb = 0.0;
    levelMeter.set (0.0f);
    for (auto& d : detector)
        d.reset();
    for (auto& d : delayBuf)
        std::fill (d.begin(), d.end(), 0.0f);
    delayPos = 0;
    for (auto& p : pipes)
        p.reset (lookahead);
    sampleIndex = 0;
    drive.reset (dbToGain (settings.driveDb));
    grMeter.set (0.0f);
    const int core = lookahead + detector[0].latency() + (bandClip != nullptr ? bandClip->latency() : 0);
    latencyCache.store (spectral() ? bank->latency() + core + clip->latency() + safety->latency() : core, std::memory_order_relaxed);
}

void Limiter::process (float* left, float* right, int numSamples) noexcept
{
    const int wantLookahead = std::clamp ((int) std::lround (settings.lookaheadMs * 0.001 * fs), 1, maxLookahead);
    const int wantBands = nested ? 0 : std::clamp (settings.bands, 0, 3);
    if (wantLookahead != lookahead || wantBands != activeBands)
        reset(); // structural change (non-automatable parameters); latency is re-reported by the chain

    drive.setTarget (dbToGain (settings.driveDb), (int) (0.02 * fs));
    float* io[2] = { left, right };
    if (nested || bandClip == nullptr || numSamples > maxBlock)
    {
        coreTransientRelease = true;
        processCore (io, io, io, numSamples, true);
        return;
    }
    if (! spectral())
    {
        // Classic: Drive -> slow first stage -> spectral clipper -> limiter core (D-042)
        const float inPk = preStages (left, right, numSamples);
        float* dv[2] = { driven[0].data(), driven[1].data() };
        coreTransientRelease = true;
        processCore (dv, dv, io, numSamples, false);
        inMeter.set ((float) gainToDb (inPk + 1.0e-9));
        return;
    }
    coreTransientRelease = false;

    // spectral mode (D-037): Drive -> band stage -> limiter core (listens to the shaped bands, applies
    // the gain to the bands with the attack emphasis) -> clipper -> true-peak safety stage
    auto& bs = bank->settings;
    bs.adaptGainDb = settings.adaptGainDb;
    bs.adaptSpeed = settings.adaptSpeed;
    bs.adaptTrans = settings.adaptTrans;
    // the attack emphasis is only on while the clipper and the safety stage run (state machine below)
    const bool wantTail = settings.transient > 1.0e-4f || settings.outClipDb > 0.01f;
    if (tail == Tail::off && wantTail)
    {
        clip->reset();
        safety->reset();
        tail = Tail::warming;
        tailCount = clip->latency() + safety->latency() + 2 * lookahead + 64;
    }
    else if (tail == Tail::on && ! wantTail)
    {
        tail = Tail::draining; // emphasised samples still inside the bands and the core
        tailCount = bank->latency() + lookahead + detector[0].latency() + 64;
    }
    bs.emphasis = tail == Tail::on ? settings.transient : 0.0f;
    bs.soften = settings.soften;
    bs.link = settings.link;
    bs.mode = settings.mode;
    bs.ceiling = dbToGain (settings.ceilingDb);
    const float inPk = preStages (left, right, numSamples);
    bank->process (driven[0].data(), driven[1].data(), shaped[0].data(), shaped[1].data(), full[0].data(), full[1].data(), numSamples);
    float* det[2] = { shaped[0].data(), shaped[1].data() };
    float* app[2] = { full[0].data(), full[1].data() };
    processCore (det, app, io, numSamples, false);
    if (tail != Tail::off)
    {
        std::copy (left, left + numSamples, proc[0].begin());
        std::copy (right, right + numSamples, proc[1].begin());
        clip->settings.thresholdDb = settings.ceilingDb;
        // soft knee for the output clipper (a wide tanh knee started too early: 1 dB of headroom lost)
        clip->settings.type = settings.outClipDb > 0.01f ? 2 : 0;
        clip->settings.shape = 0.5f;
        clip->process (proc[0].data(), proc[1].data(), numSamples);
        safety->settings.ceilingDb = settings.ceilingDb;
        safety->settings.link = settings.link;
        safety->process (proc[0].data(), proc[1].data(), numSamples);
    }
    tailBypass.process (io, 2, numSamples);
    const int fadeLen = (int) (0.01 * fs);
    for (int i = 0; i < numSamples; ++i)
    {
        switch (tail)
        {
            case Tail::off:
            case Tail::warming:
            {
                // no emphasis on this path: the core already keeps the ceiling; the clamp is the last guard
                const float hc = (float) dbToGain (settings.ceilingDb);
                left[i] = std::clamp (left[i], -hc, hc);
                right[i] = std::clamp (right[i], -hc, hc);
                if (tail == Tail::off)
                    break;
                if (--tailCount <= 0)
                    tail = Tail::fadeIn, tailCount = 0;
                break;
            }
            case Tail::fadeIn:
            {
                const float t = (float) ++tailCount / (float) fadeLen;
                left[i] += (proc[0][(size_t) i] - left[i]) * t;
                right[i] += (proc[1][(size_t) i] - right[i]) * t;
                if (tailCount >= fadeLen)
                    tail = Tail::on;
                break;
            }
            case Tail::on:
            case Tail::draining:
                left[i] = proc[0][(size_t) i];
                right[i] = proc[1][(size_t) i];
                if (tail == Tail::draining && --tailCount <= 0)
                    tail = Tail::fadeOut, tailCount = 0;
                break;
            case Tail::fadeOut:
            {
                const float t = 1.0f - (float) ++tailCount / (float) fadeLen;
                left[i] += (proc[0][(size_t) i] - left[i]) * t;
                right[i] += (proc[1][(size_t) i] - right[i]) * t;
                if (tailCount >= fadeLen)
                    tail = Tail::off;
                break;
            }
        }
    }
    float outPk = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        outPk = std::max (outPk, std::max (std::abs (left[i]), std::abs (right[i])));
    inMeter.set ((float) gainToDb (inPk + 1.0e-9));
    outMeter.set ((float) gainToDb (outPk + 1.0e-9));
}

float Limiter::preStages (const float* left, const float* right, int numSamples) noexcept
{
    float inPk = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        const float g = (float) drive.next();
        driven[0][(size_t) i] = left[i] * g;
        driven[1][(size_t) i] = right[i] * g;
        inPk = std::max (inPk, std::max (std::abs (driven[0][(size_t) i]), std::abs (driven[1][(size_t) i])));
    }
    // slow first stage (two-stage limiting): takes up to 3 dB of the overshoot with a slow attack
    // (40 ms) and release (500 ms), so the fast stage works less on dense passages
    const double lv = std::clamp ((double) settings.leveler, 0.0, 1.0);
    if (lv > 0.0 || levelDb > 1.0e-4)
    {
        constexpr int cb = 32;
        const double att = std::exp (-cb / (0.04 * fs)), rel = std::exp (-cb / (0.5 * fs));
        for (int start = 0; start < numSamples; start += cb)
        {
            const int end = std::min (numSamples, start + cb);
            float pk = 0.0f;
            for (int i = start; i < end; ++i)
                pk = std::max (pk, std::max (std::abs (driven[0][(size_t) i]), std::abs (driven[1][(size_t) i])));
            const double over = std::max (0.0, gainToDb (pk + 1.0e-9) - settings.ceilingDb);
            const double target = lv * std::min (3.0, over);
            const double from = levelDb;
            levelDb = target + (target > levelDb ? att : rel) * (levelDb - target);
            for (int i = start; i < end; ++i)
            {
                const double t = (double) (i - start + 1) / (double) (end - start);
                const float g = (float) dbToGain (-(from + t * (levelDb - from)));
                driven[0][(size_t) i] *= g;
                driven[1][(size_t) i] *= g;
            }
        }
    }
    levelMeter.set ((float) levelDb);
    // spectral clipper: the loudest peaks of each band lose up to Clip dB before the limiter
    bandClip->amountDb = settings.clipDb;
    bandClip->process (driven[0].data(), driven[1].data(), numSamples);
    return inPk;
}

void Limiter::processCore (float* const* det, float* const* app, float* const* out, int numSamples, bool applyDrive) noexcept
{
    // internal target slightly below the ceiling leaves room for the interpolation error; with the
    // output clipper (spectral mode) the core aims above the ceiling and the clipper shaves the rest
    const double over = (spectral() && ! nested) ? std::clamp ((double) settings.outClipDb, 0.0, 6.0) : 0.0;
    const double ceiling = dbToGain (settings.ceilingDb + over - 0.15);
    const double hardCeiling = dbToGain (settings.ceilingDb + over);
    // per mode (Transparent, Balanced, Aggressive): hold, fast release, slow attack and release.
    // Every hold is longer than half a period of 40 Hz, so the bass of drop tunings is not modulated.
    static constexpr double holdMs[] = { 25.0, 18.0, 13.0 };
    static constexpr double fastMs[] = { 120.0, 60.0, 25.0 };
    static constexpr double slowAttMs[] = { 250.0, 350.0, 500.0 };
    static constexpr double slowRelMs[] = { 900.0, 700.0, 500.0 };
    const int m = std::clamp (settings.mode, 0, 2);
    // in spectral mode Transients is the attack emphasis of the band stage, not a release change
    const double tr = coreTransientRelease ? std::clamp ((double) settings.transient, 0.0, 1.0) : 0.0;
    Coeffs k;
    // Density (D-044) also shortens the hold: the gain may follow the waves (like Elevate), which
    // lowers the crest factor at the cost of some low-frequency distortion
    const double dens = std::clamp ((double) settings.density, 0.0, 1.0);
    k.holdSamples = std::clamp ((int) std::lround (holdMs[m] * (1.0 - 0.92 * dens) * 0.001 * fs), 0, maxHold);
    k.slow = settings.releaseAuto;
    // Transients: drum hits pass the slow stage and the fast stage recovers sooner after them
    k.fastRel = Envelope::coeff (fs, (settings.releaseAuto ? fastMs[m] : (double) settings.releaseMs) * (1.0 - 0.6 * tr));
    k.slowAtt = Envelope::coeff (fs, slowAttMs[m] * (1.0 + 2.0 * tr));
    k.slowRel = Envelope::coeff (fs, slowRelMs[m]);
    k.densityDb = 12.0 * std::clamp ((double) settings.density, 0.0, 1.0);
    const double link = settings.link;

    const int delay = lookahead + TruePeakDetector8::latency();
    const int cap = (int) delayBuf[0].size();
    float maxGr = 0.0f, inPk = 0.0f, outPk = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const double g = applyDrive ? drive.next() : 1.0;
        double peak[2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = (float) (det[c][i] * g);
            inPk = std::max (inPk, std::abs (x));
            peak[c] = detector[(size_t) c].process (x);
            delayBuf[(size_t) c][(size_t) delayPos] = det == app ? x : (float) (app[c][i] * g);
        }

        const double linked = std::max (peak[0], peak[1]);
        double gains[2];
        for (int c = 0; c < 2; ++c)
        {
            const double pk = link * linked + (1.0 - link) * peak[c];
            gains[c] = processPipe (pipes[(size_t) c], pk > ceiling ? ceiling / pk : 1.0, k);
        }

        int r = delayPos - delay;
        if (r < 0)
            r += cap;
        double minGain = 1.0;
        for (int c = 0; c < 2; ++c)
        {
            const double y = delayBuf[(size_t) c][(size_t) r] * gains[c];
            // final sample clamp; in spectral mode the emphasised attacks pass to the clipper
            out[c][i] = applyDrive ? (float) std::clamp (y, -hardCeiling, hardCeiling) : (float) y;
            outPk = std::max (outPk, std::abs (out[c][i]));
            minGain = std::min (minGain, gains[c]);
        }
        const float gr = (float) gainToDb (minGain);
        if (i < (int) grTrace.size())
            grTrace[(size_t) i] = gr;
        maxGr = std::min (maxGr, gr);

        if (++delayPos == cap)
            delayPos = 0;
        ++sampleIndex;
        if (sampleIndex > (1 << 30))
        {
            // keep indices small; the deque only compares differences
            for (auto& p : pipes)
            {
                p.hold.shift (1 << 29);
                p.look.shift (1 << 29);
            }
            sampleIndex -= (1 << 29);
        }
    }
    grMeter.set (maxGr);
    inMeter.set ((float) gainToDb (inPk + 1.0e-9));
    outMeter.set ((float) gainToDb (outPk + 1.0e-9));
}
} // namespace mm::dsp
