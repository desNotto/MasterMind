#include "reference/MatchEq.h"

#include "dsp/Filters.h"
#include "reference/TrackAnalysis.h"
#include "state/Parameters.h"

namespace mm::reference
{
namespace
{
dsp::Coeffs coeffsOf (const EqSuggestion& s, double fs)
{
    switch (s.type)
    {
        case EqSuggestion::lowShelf: return dsp::Coeffs::lowShelf (fs, s.freq, 0.71, s.gainDb);
        case EqSuggestion::highShelf: return dsp::Coeffs::highShelf (fs, s.freq, 0.71, s.gainDb);
        default: return dsp::Coeffs::peak (fs, s.freq, s.q, s.gainDb);
    }
}
} // namespace

double matchEqResponseDb (const std::vector<EqSuggestion>& s, double fs, double f)
{
    double db = 0.0;
    for (const auto& e : s)
        db += coeffsOf (e, fs).magnitudeDb (fs, f);
    return db;
}

std::vector<EqSuggestion> suggestMatchEq (const std::vector<float>& ours, const std::vector<float>& target, double strength)
{
    std::vector<EqSuggestion> out;
    const auto& freqs = spectrumFreqs();
    if (ours.size() != freqs.size() || target.size() != freqs.size())
        return out;

    // smoothed difference (1/3 octave moving average on the 1/24-octave grid)
    const size_t n = freqs.size();
    std::vector<double> diff (n, 0.0);
    for (size_t k = 0; k < n; ++k)
    {
        double sum = 0.0;
        int c = 0;
        for (int j = -4; j <= 4; ++j)
        {
            const auto idx = (int) k + j;
            if (idx >= 0 && idx < (int) n)
            {
                sum += target[(size_t) idx] - ours[(size_t) idx];
                ++c;
            }
        }
        diff[k] = c > 0 ? sum / c * std::clamp (strength, 0.0, 1.0) : 0.0;
    }

    const double fs = 48000.0; // design rate for fitting; the EQ redesigns at the real rate
    std::vector<double> residual = diff;
    for (int iter = 0; iter < 8; ++iter)
    {
        size_t best = n;
        double bestAbs = 0.5; // stop below 0.5 dB
        for (size_t k = 0; k < n; ++k)
            if (freqs[k] >= 30.0 && freqs[k] <= 16000.0 && std::abs (residual[k]) > bestAbs)
            {
                bestAbs = std::abs (residual[k]);
                best = k;
            }
        if (best == n)
            break;

        EqSuggestion s;
        s.freq = freqs[best];
        s.gainDb = std::clamp (residual[best], -3.0, 3.0);
        // width of the feature: how far the residual keeps the same sign above half its size
        size_t lo = best, hi = best;
        while (lo > 0 && residual[lo - 1] * residual[best] > 0 && std::abs (residual[lo - 1]) > bestAbs * 0.5)
            --lo;
        while (hi + 1 < n && residual[hi + 1] * residual[best] > 0 && std::abs (residual[hi + 1]) > bestAbs * 0.5)
            ++hi;
        const double octaves = std::max (0.1, std::log2 (freqs[hi] / freqs[lo]));
        s.q = std::clamp (1.0 / octaves * 1.4, 0.5, 2.0);
        if (s.freq < 60.0 && lo == 0)
            s.type = EqSuggestion::lowShelf;
        else if (s.freq > 10000.0 && hi == n - 1)
            s.type = EqSuggestion::highShelf;
        out.push_back (s);
        for (size_t k = 0; k < n; ++k)
            residual[k] -= coeffsOf (s, fs).magnitudeDb (fs, freqs[k]);
    }
    return out;
}

std::map<juce::String, float> applyMatchEq (juce::AudioProcessorValueTreeState& apvts, const std::vector<EqSuggestion>& s)
{
    std::map<juce::String, float> previous;
    const auto set = [&] (const juce::String& id, float value)
    {
        auto* p = apvts.getParameter (id);
        if (p == nullptr)
            return;
        if (previous.find (id) == previous.end())
            previous[id] = apvts.getRawParameterValue (id)->load();
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    };

    std::vector<int> freeBells;
    for (int b = 2; b <= 9; ++b)
        if (apvts.getRawParameterValue (params::eq (b, "on"))->load() < 0.5f)
            freeBells.push_back (b);

    set ("eq_on", 1.0f);
    for (const auto& e : s)
    {
        int band = -1;
        if (e.type == EqSuggestion::lowShelf && apvts.getRawParameterValue (params::eq (1, "on"))->load() < 0.5f)
            band = 1;
        else if (e.type == EqSuggestion::highShelf && apvts.getRawParameterValue (params::eq (10, "on"))->load() < 0.5f)
            band = 10;
        bool bell = band < 0;
        if (bell)
        {
            if (freeBells.empty())
                continue;
            band = freeBells.front();
            freeBells.erase (freeBells.begin());
        }
        set (params::eq (band, "on"), 1.0f);
        if (band == 1 || band == 10)
            set (params::eq (band, "type"), 0.0f); // shelf
        else
            set (params::eq (band, "type"), e.type == EqSuggestion::lowShelf ? 1.0f : (e.type == EqSuggestion::highShelf ? 2.0f : 0.0f));
        set (params::eq (band, "freq"), (float) e.freq);
        set (params::eq (band, "gain"), (float) e.gainDb);
        set (params::eq (band, "q"), (float) (bell ? e.q : 0.71));
        set (params::eq (band, "dyn"), 0.0f);
    }
    return previous;
}

void undoMatchEq (juce::AudioProcessorValueTreeState& apvts, const std::map<juce::String, float>& previous)
{
    for (const auto& [id, v] : previous)
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (v));
            p->endChangeGesture();
        }
}
} // namespace mm::reference
