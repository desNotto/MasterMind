#include "reference/TrackAnalysis.h"

#include "mm_build_info.h"

namespace mm::reference
{
namespace
{
juce::var arrayToVar (const double* v, size_t n)
{
    juce::Array<juce::var> a;
    for (size_t i = 0; i < n; ++i)
        a.add (v[i]);
    return a;
}

template <size_t N>
void varToArray (const juce::var& v, std::array<double, N>& out)
{
    if (auto* a = v.getArray())
        for (size_t i = 0; i < N && i < (size_t) a->size(); ++i)
            out[i] = (double) (*a)[(int) i];
}

juce::var floatsToVar (const std::vector<float>& v)
{
    juce::Array<juce::var> a;
    for (auto x : v)
        a.add (std::round (x * 100.0f) / 100.0f);
    return a;
}

std::vector<float> varToFloats (const juce::var& v)
{
    std::vector<float> out;
    if (auto* a = v.getArray())
        for (const auto& x : *a)
            out.push_back ((float) (double) x);
    return out;
}
} // namespace

juce::AudioFormatManager& formatManager()
{
    static juce::AudioFormatManager fm;
    static const bool init = [] { fm.registerBasicFormats(); return true; }();
    juce::ignoreUnused (init);
    return fm;
}

const std::vector<double>& spectrumFreqs()
{
    static const std::vector<double> f = []
    {
        std::vector<double> v;
        for (int i = 0; i < 240; ++i)
            v.push_back (20.0 * std::pow (1000.0, i / 239.0));
        return v;
    }();
    return f;
}

std::vector<float> normalisedSpectrum (const analysis::TapAnalyzer& a)
{
    const auto& p = a.averagePower (analysis::SpecChannel::stereo);
    std::vector<float> out;
    // tilt 3.0103 dB/oct turns band power into a flat-for-pink basis (see powerToDisplay)
    analysis::powerToDisplay (p, a.sampleRate(), a.fftSize(), spectrumFreqs(), 1, 3.0103, out);
    double total = 0.0;
    const double binHz = a.sampleRate() / a.fftSize();
    for (size_t k = 0; k < p.size(); ++k)
    {
        const double f = k * binHz;
        if (f >= 20.0 && f < 16000.0)
            total += p[k];
    }
    const auto offset = (float) (10.0 * std::log10 (std::max (total, 1.0e-20)));
    for (auto& v : out)
        v -= offset;
    return out;
}

TrackAnalysis fromAnalyzer (const analysis::TapAnalyzer& a, double durationSeconds)
{
    TrackAnalysis t;
    const auto& m = a.loudness();
    t.integrated = m.integrated();
    t.lra = m.loudnessRange();
    t.maxTruePeak = m.truePeakDb();
    t.plr = t.integrated > -70.0 ? t.maxTruePeak - t.integrated : 0.0;
    t.tonal = a.tonalIntegrated();
    t.crest = a.crestDb();
    for (int b = 0; b < analysis::numStereoBands; ++b)
    {
        const auto s = a.stereoBand (b, false);
        t.width[(size_t) b] = s.widthPercent;
        t.correlation[(size_t) b] = s.correlation;
    }
    t.harshness = a.harshnessDb();
    t.subToBass = a.subToBassDb();
    t.lowCorrelation = a.lowBelowMono().correlation;
    t.spectrum = normalisedSpectrum (a);
    t.durationSeconds = durationSeconds;
    t.date = juce::Time::getCurrentTime().toISO8601 (true);
    t.pluginVersion = mm::build::version;
    return t;
}

juce::var TrackAnalysis::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("formatVersion", 1);
    o->setProperty ("name", name);
    o->setProperty ("path", path);
    o->setProperty ("integrated", integrated);
    o->setProperty ("lra", lra);
    o->setProperty ("plr", plr);
    o->setProperty ("maxTruePeak", maxTruePeak);
    o->setProperty ("harshness", harshness);
    o->setProperty ("subToBass", subToBass);
    o->setProperty ("lowCorrelation", lowCorrelation);
    o->setProperty ("tonal", arrayToVar (tonal.data(), tonal.size()));
    o->setProperty ("crest", arrayToVar (crest.data(), crest.size()));
    o->setProperty ("width", arrayToVar (width.data(), width.size()));
    o->setProperty ("correlation", arrayToVar (correlation.data(), correlation.size()));
    o->setProperty ("spectrum", floatsToVar (spectrum));
    o->setProperty ("shortTerm", floatsToVar (shortTerm));
    o->setProperty ("duration", durationSeconds);
    o->setProperty ("date", date);
    o->setProperty ("pluginVersion", pluginVersion);
    return juce::var (o);
}

std::optional<TrackAnalysis> TrackAnalysis::fromJson (const juce::var& v)
{
    if (! v.isObject() || (int) v.getProperty ("formatVersion", 0) != 1)
        return std::nullopt;
    TrackAnalysis t;
    t.name = v.getProperty ("name", "").toString();
    t.path = v.getProperty ("path", "").toString();
    t.integrated = v.getProperty ("integrated", -100.0);
    t.lra = v.getProperty ("lra", 0.0);
    t.plr = v.getProperty ("plr", 0.0);
    t.maxTruePeak = v.getProperty ("maxTruePeak", -100.0);
    t.harshness = v.getProperty ("harshness", 0.0);
    t.subToBass = v.getProperty ("subToBass", 0.0);
    t.lowCorrelation = v.getProperty ("lowCorrelation", 1.0);
    varToArray (v.getProperty ("tonal", {}), t.tonal);
    varToArray (v.getProperty ("crest", {}), t.crest);
    varToArray (v.getProperty ("width", {}), t.width);
    varToArray (v.getProperty ("correlation", {}), t.correlation);
    t.spectrum = varToFloats (v.getProperty ("spectrum", {}));
    t.shortTerm = varToFloats (v.getProperty ("shortTerm", {}));
    t.durationSeconds = v.getProperty ("duration", 0.0);
    t.date = v.getProperty ("date", "").toString();
    t.pluginVersion = v.getProperty ("pluginVersion", "").toString();
    if (t.spectrum.size() != spectrumFreqs().size())
        return std::nullopt;
    return t;
}

std::optional<TrackAnalysis> analyseFile (const juce::File& file, std::function<void (float)> progress,
                                          const std::atomic<bool>& cancelled, juce::String& error,
                                          double fromSeconds, double toSeconds)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Cannot read the file (unsupported or damaged): " + file.getFileName();
        return std::nullopt;
    }
    if (reader->sampleRate < 8000.0 || reader->lengthInSamples <= 0)
    {
        error = "The file has no audio: " + file.getFileName();
        return std::nullopt;
    }

    analysis::TapAnalyzer a;
    a.setLightweight (true);
    a.prepare (reader->sampleRate, 13);
    const int chunk = 32768;
    juce::AudioBuffer<float> buf (2, chunk);
    juce::int64 first = 0, total = reader->lengthInSamples;
    if (toSeconds > fromSeconds)
    {
        first = std::clamp ((juce::int64) (fromSeconds * reader->sampleRate), (juce::int64) 0, total);
        total = std::clamp ((juce::int64) (toSeconds * reader->sampleRate), first, total);
    }
    std::vector<float> shortTerm;
    for (juce::int64 pos = first; pos < total; pos += chunk)
    {
        if (cancelled.load())
        {
            error = "Cancelled";
            return std::nullopt;
        }
        const int n = (int) std::min<juce::int64> (chunk, total - pos);
        buf.clear();
        reader->read (&buf, 0, n, pos, true, true);
        if (reader->numChannels == 1)
            buf.copyFrom (1, 0, buf, 0, 0, n);
        a.process (buf.getReadPointer (0), buf.getReadPointer (1), n);
        if (progress)
            progress ((float) ((double) (pos + n - first) / (double) std::max<juce::int64> (1, total - first)));
    }
    auto t = fromAnalyzer (a, (double) (total - first) / reader->sampleRate);
    const auto& hist = a.loudness().shortTermFullHistory();
    t.shortTerm.assign (hist.begin(), hist.end());
    t.name = file.getFileNameWithoutExtension();
    t.path = file.getFullPathName();
    if (t.integrated < -70.0)
    {
        error = "The file is silent: " + file.getFileName();
        return std::nullopt;
    }
    return t;
}

bool decodeForPlayback (const juce::File& file, double targetRate, juce::AudioBuffer<float>& out, juce::String& error,
                        const std::atomic<bool>& cancelled)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager().createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Cannot read the file: " + file.getFileName();
        return false;
    }
    const auto len = reader->lengthInSamples;
    if (len <= 0 || len > (juce::int64) reader->sampleRate * 60 * 30)
    {
        error = "The file is empty or longer than 30 minutes: " + file.getFileName();
        return false;
    }
    juce::AudioBuffer<float> src (2, (int) len);
    reader->read (&src, 0, (int) len, 0, true, true);
    if (reader->numChannels == 1)
        src.copyFrom (1, 0, src, 0, 0, (int) len);
    if (cancelled.load())
        return false;

    const double ratio = reader->sampleRate / targetRate;
    if (std::abs (ratio - 1.0) < 1.0e-9)
    {
        out = std::move (src);
        return true;
    }
    const int outLen = (int) std::floor ((double) len / ratio);
    out.setSize (2, outLen);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, src.getReadPointer (ch), out.getWritePointer (ch), outLen);
    }
    return true;
}
} // namespace mm::reference
