// Black-box comparison of the MasterMind limiter with another limiter plugin (D-044). The other
// plugin is loaded like a host loads it and only processes test signals; nothing of it is read or
// copied. Usage:
//   MasterMindCompare params <plugin.vst3>            list the parameters of the plugin
//   MasterMindCompare run <plugin.vst3> <gainParam> [name=value ...]   the comparison
// Local tool, not part of the CI tests.

#include "analysis/Loudness.h"
#include "dsp/MasterChain.h"
#include "state/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>

namespace
{
constexpr double twoPi = 6.283185307179586;
constexpr double fs = 48000.0;
constexpr int block = 512;

// ---- test signals --------------------------------------------------------------------------------------
juce::AudioBuffer<float> metalMix (double seconds, bool pilot)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (2, n);
    juce::Random r (5);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        const double beat = std::fmod (t, 0.25), bar = std::fmod (t, 1.0);
        const double kick = std::exp (-beat * 25.0) * std::sin (twoPi * (48.0 + 120.0 * std::exp (-beat * 40.0)) * beat) * 0.8;
        const double sn = (bar > 0.5 && bar < 0.6) ? std::exp (-(bar - 0.5) * 35.0) * ((r.nextDouble() - 0.5) * 0.9 + 0.3 * std::sin (twoPi * 190.0 * t)) : 0.0;
        double gtr = 0.0;
        for (double f : { 73.4, 110.0, 146.8 })
            gtr += std::tanh (3.0 * std::sin (twoPi * f * t + 0.3 * std::sin (twoPi * 5.0 * t)));
        gtr *= 0.16;
        const double bass = 0.25 * std::sin (twoPi * 36.7 * t);
        const double p = pilot ? 0.03 * std::sin (twoPi * 2000.0 * t) : 0.0;
        b.setSample (0, i, (float) (0.5 * (kick + sn + gtr * 1.05 + bass) + p));
        b.setSample (1, i, (float) (0.5 * (kick + sn + gtr * 0.95 + bass) + p));
    }
    return b;
}

juce::AudioBuffer<float> sine (double freq, double amp, double seconds)
{
    const int n = (int) (fs * seconds);
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float v = (float) (amp * std::sin (twoPi * freq * i / fs));
        b.setSample (0, i, v);
        b.setSample (1, i, v);
    }
    return b;
}

// ---- measurements ------------------------------------------------------------------------------------
double lufs (const juce::AudioBuffer<float>& b, int from)
{
    mm::analysis::LoudnessMeter m;
    m.prepare (fs);
    m.process (b.getReadPointer (0) + from, b.getReadPointer (1) + from, b.getNumSamples() - from);
    return m.integrated();
}

double truePeakDb (const juce::AudioBuffer<float>& b, int from)
{
    mm::analysis::LoudnessMeter m;
    m.prepare (fs);
    m.process (b.getReadPointer (0) + from, b.getReadPointer (1) + from, b.getNumSamples() - from);
    return m.truePeakDb();
}

double goertzel (const float* x, int n, double f)
{
    double re = 0.0, im = 0.0, ws = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (twoPi * i / (n - 1));
        re += x[i] * w * std::cos (twoPi * f * i / fs);
        im -= x[i] * w * std::sin (twoPi * f * i / fs);
        ws += w;
    }
    return 2.0 * std::sqrt (re * re + im * im) / ws;
}

/** Standard deviation (dB) of the 2 kHz pilot in 20 ms windows. */
double pilotPumping (const juce::AudioBuffer<float>& b, int from)
{
    const int win = (int) (0.02 * fs);
    std::vector<double> lv;
    for (int s = from; s + win < b.getNumSamples(); s += win)
        lv.push_back (20.0 * std::log10 (goertzel (b.getReadPointer (0) + s, win, 2000.0) + 1.0e-12));
    double m = 0.0, v = 0.0;
    for (auto x : lv)
        m += x;
    m /= (double) lv.size();
    for (auto x : lv)
        v += (x - m) * (x - m);
    return std::sqrt (v / (double) lv.size());
}

/** THD of a sine: harmonics 2..10 over the fundamental, dB. */
double thdDb (const juce::AudioBuffer<float>& b, int from, double f0)
{
    const int n = std::min (b.getNumSamples() - from, (int) fs);
    const float* x = b.getReadPointer (0) + from;
    const double a1 = goertzel (x, n, f0);
    double h = 0.0;
    for (int k = 2; k <= 10; ++k)
        h += std::pow (goertzel (x, n, f0 * k), 2.0);
    return 20.0 * std::log10 (std::sqrt (h) / (a1 + 1.0e-12) + 1.0e-12);
}

/** Level of the steady guitar band (500 Hz - 2 kHz energy proxy: the pilot excluded) is not used;
    the crest factor of the output shows how much the wave shape was changed. */
double crestDb (const juce::AudioBuffer<float>& b, int from)
{
    double pk = 0.0, sum = 0.0;
    const int n = b.getNumSamples() - from;
    for (int i = from; i < b.getNumSamples(); ++i)
    {
        const double v = b.getSample (0, i);
        pk = std::max (pk, std::abs (v));
        sum += v * v;
    }
    return 20.0 * std::log10 (pk / std::sqrt (sum / n + 1.0e-20) + 1.0e-12);
}

// ---- the processors ------------------------------------------------------------------------------------
struct Host final : public juce::AudioProcessor
{
    Host() : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo(), true).withOutput ("Out", juce::AudioChannelSet::stereo(), true)),
             apvts (*this, nullptr, "S", mm::params::createLayout()), chain (apvts) {}
    void set (const juce::String& id, float v)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    const juce::String getName() const override { return "Host"; }
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
    juce::AudioProcessorValueTreeState apvts;
    mm::dsp::MasterChain chain;
};

using Processor = std::function<juce::AudioBuffer<float> (const juce::AudioBuffer<float>&, double gainDb)>;

/** MasterMind limiter with the given extra settings; gain = Limiter Drive. */
Processor mastermind (std::map<juce::String, float> extra)
{
    return [extra] (const juce::AudioBuffer<float>& in, double gainDb)
    {
        Host h;
        h.set ("lim_on", 1.0f);
        h.set ("lim_ceiling", -1.0f);
        for (const auto& [id, v] : extra)
            h.set (id, v);
        h.set ("lim_drive", (float) gainDb);
        h.chain.prepare (fs, block, false);
        juce::AudioBuffer<float> out;
        out.makeCopyOf (in);
        for (int s = 0; s < out.getNumSamples(); s += block)
        {
            const int n = std::min (block, out.getNumSamples() - s);
            float* ch[2] = { out.getWritePointer (0) + s, out.getWritePointer (1) + s };
            juce::AudioBuffer<float> v (ch, 2, n);
            h.chain.process (v);
        }
        return out;
    };
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (100).equalsIgnoreCase (name))
            return prm;
    return nullptr;
}

/** The other plugin; gain = its parameter `gainName` set by text (dB), extra parameters by text. */
Processor other (juce::AudioPluginFormat& format, const juce::PluginDescription& desc, juce::String gainName, std::map<juce::String, juce::String> extra)
{
    return [&format, desc, gainName, extra] (const juce::AudioBuffer<float>& in, double gainDb)
    {
        juce::String err;
        auto p = format.createInstanceFromDescription (desc, fs, block, err);
        juce::AudioBuffer<float> out;
        out.makeCopyOf (in);
        if (p == nullptr)
        {
            std::printf ("cannot create: %s\n", err.toRawUTF8());
            out.clear();
            return out;
        }
        p->setPlayConfigDetails (2, 2, fs, block);
        p->prepareToPlay (fs, block);
        for (const auto& [name, text] : extra)
            if (auto* prm = findParam (*p, name))
                prm->setValueNotifyingHost (prm->getValueForText (text));
        if (auto* g = findParam (*p, gainName))
            g->setValueNotifyingHost (g->getValueForText (juce::String (gainDb, 2)));
        juce::MidiBuffer midi;
        for (int s = 0; s < out.getNumSamples(); s += block)
        {
            const int n = std::min (block, out.getNumSamples() - s);
            juce::AudioBuffer<float> v (2, n);
            for (int c = 0; c < 2; ++c)
                v.copyFrom (c, 0, out, c, s, n);
            p->processBlock (v, midi);
            for (int c = 0; c < 2; ++c)
                out.copyFrom (c, s, v, c, 0, n);
        }
        p->releaseResources();
        return out;
    };
}

/** Gain (dB, 0..maxGain) for the target loudness by bisection; returns the processed signal. */
juce::AudioBuffer<float> toLoudness (const Processor& proc, const juce::AudioBuffer<float>& in, double target, double maxGain, double& gainOut, double& lufsOut)
{
    double lo = 0.0, hi = maxGain;
    juce::AudioBuffer<float> out;
    for (int it = 0; it < 10; ++it)
    {
        const double g = 0.5 * (lo + hi);
        out = proc (in, g);
        const double l = lufs (out, (int) fs);
        (l < target ? lo : hi) = g;
        gainOut = g;
        lufsOut = l;
    }
    return out;
}

void report (const char* name, const Processor& proc, double maxGain)
{
    const auto mix = metalMix (8.0, true);
    for (double target : { -9.0, -8.0, -7.0, -6.0 })
    {
        double g = 0.0, l = 0.0;
        const auto o = toLoudness (proc, mix, target, maxGain, g, l);
        std::printf ("%-28s target %5.1f: gain %5.1f dB -> %6.2f LUFS, pilot pumping %.2f dB, crest %.1f dB, TP %.2f dBTP\n", name, target, g, l,
                     pilotPumping (o, (int) fs), crestDb (o, (int) fs), truePeakDb (o, (int) fs));
    }
    // distortion: bass sine 55 Hz and a 1 kHz sine, each 12 dB into limiting
    for (double f : { 55.0, 1000.0 })
    {
        const auto o = proc (sine (f, 0.5, 3.0), 12.0);
        std::printf ("%-28s sine %4.0f Hz +12 dB: THD %.1f dB\n", name, f, thdDb (o, (int) fs, f));
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc < 3)
    {
        std::printf ("usage: MasterMindCompare params|run <plugin.vst3> [gainParam] [name=value ...]\n");
        return 1;
    }
    const juce::String mode (argv[1]), path (argv[2]);
    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile (found, path);
    if (found.isEmpty())
    {
        std::printf ("no plugin in %s\n", path.toRawUTF8());
        return 1;
    }
    const auto desc = *found[0];
    std::printf ("plugin: %s by %s, version %s\n", desc.name.toRawUTF8(), desc.manufacturerName.toRawUTF8(), desc.version.toRawUTF8());
    if (mode == "params")
    {
        juce::String err;
        auto p = format.createInstanceFromDescription (desc, fs, block, err);
        if (p == nullptr)
        {
            std::printf ("cannot create: %s\n", err.toRawUTF8());
            return 1;
        }
        std::printf ("latency %d samples\n", p->getLatencySamples());
        for (auto* prm : p->getParameters())
            std::printf ("  %-32s = %-14s (default %s)\n", prm->getName (100).toRawUTF8(), prm->getCurrentValueAsText().toRawUTF8(),
                         prm->getText (prm->getDefaultValue(), 100).toRawUTF8());
        // does it pass audio?
        p->setPlayConfigDetails (2, 2, fs, block);
        p->prepareToPlay (fs, block);
        auto s = sine (1000.0, 0.25, 2.0);
        juce::MidiBuffer midi;
        double rms = 0.0;
        for (int st = 0; st < s.getNumSamples(); st += block)
        {
            juce::AudioBuffer<float> v (2, block);
            for (int c = 0; c < 2; ++c)
                v.copyFrom (c, 0, s, c, st, block);
            p->processBlock (v, midi);
            for (int i = 0; i < block; ++i)
                rms += (double) v.getSample (0, i) * v.getSample (0, i);
        }
        std::printf ("output RMS of a -12 dBFS sine: %.1f dBFS\n", 10.0 * std::log10 (rms / s.getNumSamples() + 1.0e-20));
        return 0;
    }
    const juce::String gainName = argc > 3 ? juce::String (argv[3]) : juce::String ("Gain");
    std::map<juce::String, juce::String> extra;
    for (int i = 4; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        extra[a.upToFirstOccurrenceOf ("=", false, false)] = a.fromFirstOccurrenceOf ("=", false, false);
    }
    if (mode == "ours")
    {
        // tuning loop: MasterMind variants only (D-044)
        const std::map<juce::String, float> loud { { "lim_bands", 3.0f }, { "lim_mode", 2.0f }, { "lim_adapt_gain", 12.0f }, { "lim_density", 100.0f } };
        auto with = [&loud] (std::map<juce::String, float> m) { auto r = loud; for (auto& [k, v] : m) r[k] = v; return r; };
        report ("E A + OutClip 2", mastermind (with ({ { "lim_out_clip", 2.0f } })), 24.0);
        report ("F A + Clip 2 OutClip 2", mastermind (with ({ { "lim_clip", 2.0f }, { "lim_out_clip", 2.0f } })), 24.0);
        report ("G A + OutClip 3", mastermind (with ({ { "lim_out_clip", 3.0f } })), 24.0);
        return 0;
    }
    report ("MasterMind 26 Balanced", mastermind ({ { "lim_bands", 3.0f } }), 24.0);
    // the set measured against Elevate (D-044)
    report ("MasterMind 26 loud", mastermind ({ { "lim_bands", 3.0f }, { "lim_mode", 2.0f }, { "lim_adapt_gain", 12.0f }, { "lim_clip", 2.0f }, { "lim_out_clip", 2.0f },
                                                { "lim_density", 100.0f } }), 24.0);
    report (desc.name.toRawUTF8(), other (format, desc, gainName, extra), 24.0);
    return 0;
}
