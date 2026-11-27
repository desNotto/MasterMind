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
void Limiter::Pipe::prepare (int capacity)
{
    minVal.assign ((size_t) capacity, 1.0);
    minIdx.assign ((size_t) capacity, 0);
    avgBuf.assign ((size_t) capacity, 1.0);
}

void Limiter::Pipe::reset (int window)
{
    std::fill (minVal.begin(), minVal.end(), 1.0);
    std::fill (minIdx.begin(), minIdx.end(), 0);
    std::fill (avgBuf.begin(), avgBuf.end(), 1.0);
    head = tail = avgPos = 0;
    avgSum = (double) window;
    release = 1.0;
    sustained = 0.0;
}

double Limiter::Pipe::process (double required, int index, int window, double relCoeff) noexcept
{
    // release smoothing: instant down, smooth up (never above the requirement)
    release = required < release ? required : required + relCoeff * (release - required);

    // sliding minimum over window + 1 samples
    const int cap = (int) minVal.size();
    while (head != tail)
    {
        const int last = (tail - 1 + cap) % cap;
        if (minVal[(size_t) last] < release)
            break;
        tail = last;
    }
    minVal[(size_t) tail] = release;
    minIdx[(size_t) tail] = index;
    tail = (tail + 1) % cap;
    // window + 2 samples: both output samples around an inter-sample peak are covered
    while (index - minIdx[(size_t) head] > window + 1)
        head = (head + 1) % cap;
    const double wmin = minVal[(size_t) head];

    // moving average over window samples
    avgSum += wmin - avgBuf[(size_t) avgPos];
    avgBuf[(size_t) avgPos] = wmin;
    if (++avgPos >= window)
        avgPos = 0;
    return avgSum / window;
}

void Limiter::prepare (const Context& ctx)
{
    fs = ctx.sampleRate;
    maxBlock = ctx.maxBlock;
    maxLookahead = (int) std::ceil (0.005 * fs) + 2;
    for (auto& p : pipes)
        p.prepare (maxLookahead + 4);
    for (auto& d : delayBuf)
        d.assign ((size_t) (maxLookahead + TruePeakDetector8::latency() + 2), 0.0f);
    grTrace.assign ((size_t) maxBlock, 0.0f);
    sustainedCoeff = Envelope::coeff (fs, 1000.0);
    reset();
}

void Limiter::reset()
{
    lookahead = std::clamp ((int) std::lround (settings.lookaheadMs * 0.001 * fs), 1, maxLookahead);
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
}

void Limiter::process (float* left, float* right, int numSamples) noexcept
{
    const int wantLookahead = std::clamp ((int) std::lround (settings.lookaheadMs * 0.001 * fs), 1, maxLookahead);
    if (wantLookahead != lookahead)
        reset(); // structural change (non-automatable parameter); latency is re-reported by the chain

    drive.setTarget (dbToGain (settings.driveDb), (int) (0.02 * fs));
    // internal target slightly below the ceiling leaves room for the interpolation error
    const double ceiling = dbToGain (settings.ceilingDb - 0.15);
    const double hardCeiling = dbToGain (settings.ceilingDb);
    static constexpr double baseRelease[] = { 150.0, 80.0, 35.0 };
    const double base = baseRelease[std::clamp (settings.mode, 0, 2)];
    const double link = settings.link;

    const int delay = lookahead + TruePeakDetector8::latency();
    const int cap = (int) delayBuf[0].size();
    float* io[2] = { left, right };
    float maxGr = 0.0f, inPk = 0.0f, outPk = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const double g = drive.next();
        double peak[2];
        for (int c = 0; c < 2; ++c)
        {
            const float x = (float) (io[c][i] * g);
            inPk = std::max (inPk, std::abs (x));
            peak[c] = detector[(size_t) c].process (x);
            delayBuf[(size_t) c][(size_t) delayPos] = x;
        }

        const double linked = std::max (peak[0], peak[1]);
        double gains[2];
        for (int c = 0; c < 2; ++c)
        {
            auto& p = pipes[(size_t) c];
            const double pk = link * linked + (1.0 - link) * peak[c];
            const double required = pk > ceiling ? ceiling / pk : 1.0;

            // program-dependent release: longer when the reduction is sustained
            const double grDb = -gainToDb (p.release);
            p.sustained = grDb + sustainedCoeff * (p.sustained - grDb);
            double relMs = settings.releaseAuto ? base * (1.0 + 3.0 * std::clamp (p.sustained / 6.0, 0.0, 1.0))
                                                : (double) settings.releaseMs;
            if (grDb - p.sustained > 2.0) // a short spike above the program level: fast transient stage
                relMs *= 1.0 - 0.7 * settings.transient;
            gains[c] = p.process (required, sampleIndex, lookahead, Envelope::coeff (fs, relMs));
        }

        int r = delayPos - delay;
        if (r < 0)
            r += cap;
        double minGain = 1.0;
        for (int c = 0; c < 2; ++c)
        {
            const double y = delayBuf[(size_t) c][(size_t) r] * gains[c];
            io[c][i] = (float) std::clamp (y, -hardCeiling, hardCeiling); // final safety stage
            outPk = std::max (outPk, std::abs (io[c][i]));
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
                for (auto& idx : p.minIdx)
                    idx -= (1 << 29);
            sampleIndex -= (1 << 29);
        }
    }
    grMeter.set (maxGr);
    inMeter.set ((float) gainToDb (inPk + 1.0e-9));
    outMeter.set ((float) gainToDb (outPk + 1.0e-9));
}
} // namespace mm::dsp
