#include "dsp/Resonance.h"

namespace mm::dsp
{
void ResonanceSuppressor::prepare (double sampleRate, int)
{
    fs = sampleRate;
    order = fs <= 50000.0 ? 11 : (fs <= 100000.0 ? 12 : 13);
    frameSize = 1 << order;
    hop = frameSize / 4;
    fft = std::make_unique<juce::dsp::FFT> (order);
    window.resize ((size_t) frameSize);
    for (int i = 0; i < frameSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * i / frameSize)); // periodic Hann
    for (auto* v : { &inL, &inR, &outL, &outR, &dryL, &dryR })
        v->assign ((size_t) frameSize, 0.0f);
    bufL.assign ((size_t) (2 * frameSize), 0.0f);
    bufR.assign ((size_t) (2 * frameSize), 0.0f);
    mag.assign ((size_t) (frameSize / 2 + 1), 0.0f);
    smooth.assign ((size_t) (frameSize / 2 + 2), 0.0f);
    gain.assign ((size_t) (frameSize / 2 + 1), 1.0f);
    reset();
}

void ResonanceSuppressor::reset()
{
    for (auto* v : { &inL, &inR, &outL, &outR, &dryL, &dryR })
        std::fill (v->begin(), v->end(), 0.0f);
    std::fill (gain.begin(), gain.end(), 1.0f);
    inPos = hopCount = outPos = 0;
    meter.set (0.0f);
}

void ResonanceSuppressor::processFrame() noexcept
{
    // analysis: the latest frameSize input samples, Hann windowed
    for (int i = 0; i < frameSize; ++i)
    {
        bufL[(size_t) i] = inL[(size_t) i] * window[(size_t) i];
        bufR[(size_t) i] = inR[(size_t) i] * window[(size_t) i];
    }
    std::fill (bufL.begin() + frameSize, bufL.end(), 0.0f);
    std::fill (bufR.begin() + frameSize, bufR.end(), 0.0f);
    fft->performRealOnlyForwardTransform (bufL.data(), true);
    fft->performRealOnlyForwardTransform (bufR.data(), true);

    const int bins = frameSize / 2 + 1;
    for (int k = 0; k < bins; ++k)
    {
        const float ml = bufL[(size_t) (2 * k)] * bufL[(size_t) (2 * k)] + bufL[(size_t) (2 * k + 1)] * bufL[(size_t) (2 * k + 1)];
        const float mr = bufR[(size_t) (2 * k)] * bufR[(size_t) (2 * k)] + bufR[(size_t) (2 * k + 1)] * bufR[(size_t) (2 * k + 1)];
        mag[(size_t) k] = 10.0f * std::log10 (std::max (ml, mr) + 1.0e-20f); // linked: the louder channel decides
    }
    // smoothed spectrum (log domain moving average over a fraction of an octave)
    smooth[0] = 0.0f;
    for (int k = 0; k < bins; ++k)
        smooth[(size_t) k + 1] = smooth[(size_t) k] + mag[(size_t) k];

    const double binHz = fs / frameSize;
    const double width = 1.0 - 0.75 * std::clamp ((double) settings.sharpness, 0.0, 1.0); // octaves
    const double threshold = 2.0 + 6.0 * settings.sharpness;
    const double depth = std::clamp ((double) settings.depthDb, 0.0, 20.0);
    const int k0 = std::max (1, (int) (settings.lowHz / binHz)), k1 = std::min (bins - 1, (int) (settings.highHz / binHz) + 1);
    float maxRed = 0.0f;
    for (int k = 1; k < bins; ++k)
    {
        double target = 1.0;
        if (k >= k0 && k <= k1)
        {
            const int a = std::clamp ((int) (k * std::pow (2.0, -width * 0.5)), 0, bins - 1);
            const int b = std::clamp ((int) (k * std::pow (2.0, width * 0.5)) + 1, a + 1, bins);
            const double avg = (smooth[(size_t) b] - smooth[(size_t) a]) / (b - a);
            const double red = std::clamp ((double) mag[(size_t) k] - avg - threshold, 0.0, depth);
            target = dbToGain (-red);
        }
        auto& gk = gain[(size_t) k];
        const double c = target < gk ? attackCoeff : releaseCoeff;
        gk = (float) (target + c * (gk - target));
        maxRed = std::min (maxRed, (float) gainToDb (gk));
        bufL[(size_t) (2 * k)] *= gk;
        bufL[(size_t) (2 * k + 1)] *= gk;
        bufR[(size_t) (2 * k)] *= gk;
        bufR[(size_t) (2 * k + 1)] *= gk;
    }
    meter.set (maxRed);

    fft->performRealOnlyInverseTransform (bufL.data());
    fft->performRealOnlyInverseTransform (bufR.data());
    // synthesis window; Hann^2 at 75 % overlap sums to 1.5
    const float scale = 1.0f / 1.5f;
    for (int i = 0; i < frameSize; ++i)
    {
        outL[(size_t) i] += bufL[(size_t) i] * window[(size_t) i] * scale;
        outR[(size_t) i] += bufR[(size_t) i] * window[(size_t) i] * scale;
    }
}

void ResonanceSuppressor::process (float* left, float* right, int numSamples) noexcept
{
    if (! settings.on)
    {
        wasOn = false;
        return;
    }
    if (! wasOn)
    {
        reset();
        wasOn = true;
    }
    const double hopSeconds = (double) hop / fs;
    const double attackMs = 60.0 - 50.0 * settings.speed, releaseMs = 600.0 - 500.0 * settings.speed;
    attackCoeff = std::exp (-hopSeconds / (attackMs * 0.001));
    releaseCoeff = std::exp (-hopSeconds / (releaseMs * 0.001));

    const float mix = std::clamp (settings.mix, 0.0f, 1.0f);
    for (int i = 0; i < numSamples; ++i)
    {
        // input goes to the end of the analysis buffer; output and dry come out after frameSize samples
        const int writeAt = frameSize - hop + hopCount;
        inL[(size_t) writeAt] = left[i];
        inR[(size_t) writeAt] = right[i];

        const float wetL = outL[(size_t) hopCount], wetR = outR[(size_t) hopCount];
        const float dl = dryL[(size_t) outPos], dr = dryR[(size_t) outPos];
        dryL[(size_t) outPos] = left[i];
        dryR[(size_t) outPos] = right[i];
        outPos = (outPos + 1) % frameSize;

        const float yl = dl + mix * (wetL - dl), yr = dr + mix * (wetR - dr);
        left[i] = settings.delta ? dl - yl : yl;
        right[i] = settings.delta ? dr - yr : yr;

        if (++hopCount == hop)
        {
            hopCount = 0;
            // shift the finished output out and the input along by one hop
            std::move (outL.begin() + hop, outL.end(), outL.begin());
            std::move (outR.begin() + hop, outR.end(), outR.begin());
            std::fill (outL.end() - hop, outL.end(), 0.0f);
            std::fill (outR.end() - hop, outR.end(), 0.0f);
            processFrame();
            std::move (inL.begin() + hop, inL.end(), inL.begin());
            std::move (inR.begin() + hop, inR.end(), inR.begin());
        }
    }
}
} // namespace mm::dsp
