#include "state/Parameters.h"

#include <unordered_map>

namespace mm::params
{
namespace
{
Spec f (juce::String id, juce::String name, float lo, float hi, float def, Unit unit, float centre = 0.0f, float step = 0.0f)
{
    Spec s;
    s.id = std::move (id);
    s.name = std::move (name);
    s.kind = Kind::floating;
    s.minValue = lo;
    s.maxValue = hi;
    s.defaultValue = def;
    s.centre = centre;
    s.step = step;
    s.unit = unit;
    return s;
}

Spec b (juce::String id, juce::String name, bool def)
{
    Spec s;
    s.id = std::move (id);
    s.name = std::move (name);
    s.kind = Kind::toggle;
    s.maxValue = 1.0f;
    s.defaultValue = def ? 1.0f : 0.0f;
    return s;
}

Spec c (juce::String id, juce::String name, const juce::StringArray& choices, int def, bool automatable = true)
{
    Spec s;
    s.id = std::move (id);
    s.name = std::move (name);
    s.kind = Kind::choice;
    s.choices = choices;
    s.maxValue = (float) (choices.size() - 1);
    s.defaultValue = (float) def;
    s.automatable = automatable;
    return s;
}

std::vector<Spec> build()
{
    std::vector<Spec> v;
    v.reserve (420);

    // ---- Global ----------------------------------------------------------------------------
    v.push_back (c ("quality", "Quality", choice::quality, 0, false));

    // ---- 4.1 Input and subsonic filter -----------------------------------------------------
    v.push_back (b ("in_on", "Input On", true));
    v.push_back (f ("in_trim", "Input Trim", -24.0f, 24.0f, 0.0f, Unit::dB));
    v.push_back (b ("in_pol_l", "Input Polarity L", false));
    v.push_back (b ("in_pol_r", "Input Polarity R", false));
    v.push_back (b ("hpf_on", "Subsonic On", false));
    v.push_back (f ("hpf_freq", "Subsonic Freq", 15.0f, 60.0f, 25.0f, Unit::hz, 30.0f));
    v.push_back (c ("hpf_slope", "Subsonic Slope", choice::slopeHpf, 1));

    // ---- 4.2 Dynamic EQ --------------------------------------------------------------------
    static constexpr float eqFreqs[eqBands] = { 30.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4800.0f, 8000.0f, 16000.0f };
    v.push_back (b ("eq_on", "EQ On", false));
    v.push_back (c ("eq_phase", "EQ Phase Mode", choice::phaseMode, 0, false));
    for (int band = 1; band <= eqBands; ++band)
    {
        const auto n = "EQ " + juce::String (band) + " ";
        v.push_back (b (eq (band, "on"), n + "On", false));
        if (band == 1)
            v.push_back (c (eq (band, "type"), n + "Type", choice::eqTypeLow, 0));
        else if (band == eqBands)
            v.push_back (c (eq (band, "type"), n + "Type", choice::eqTypeHigh, 0));
        else
            v.push_back (c (eq (band, "type"), n + "Type", choice::eqTypeMid, 0));
        if (band == 1 || band == eqBands)
            v.push_back (c (eq (band, "slope"), n + "Slope", choice::eqCutSlope, 3));
        v.push_back (f (eq (band, "freq"), n + "Freq", 20.0f, 20000.0f, eqFreqs[band - 1], Unit::hz, 640.0f));
        v.push_back (f (eq (band, "gain"), n + "Gain", -18.0f, 18.0f, 0.0f, Unit::dB));
        v.push_back (f (eq (band, "q"), n + "Q", 0.1f, 18.0f, 0.71f, Unit::none, 1.4f));
        v.push_back (c (eq (band, "place"), n + "Placement", choice::placement, 0));
        v.push_back (b (eq (band, "dyn"), n + "Dynamic", false));
        v.push_back (f (eq (band, "thr"), n + "Threshold", -60.0f, 0.0f, -24.0f, Unit::dB));
        v.push_back (f (eq (band, "range"), n + "Range", 0.0f, 18.0f, 6.0f, Unit::dB));
        v.push_back (c (eq (band, "mode"), n + "Dyn Mode", choice::dynMode, 0));
        v.push_back (f (eq (band, "att"), n + "Attack", 0.1f, 100.0f, 10.0f, Unit::ms, 10.0f));
        v.push_back (f (eq (band, "rel"), n + "Release", 5.0f, 1000.0f, 100.0f, Unit::ms, 100.0f));
    }

    // Resonance suppressor (part of 4.2; added in M7, new IDs only)
    v.push_back (b ("rs_on", "Resonance Suppressor On", false));
    v.back().automatable = false; // changes the latency
    v.push_back (f ("rs_low", "Resonance Low", 150.0f, 12000.0f, 150.0f, Unit::hz, 1300.0f));
    v.push_back (f ("rs_high", "Resonance High", 150.0f, 12000.0f, 12000.0f, Unit::hz, 1300.0f));
    v.push_back (f ("rs_depth", "Resonance Depth", 0.0f, 20.0f, 6.0f, Unit::dB));
    v.push_back (f ("rs_sharp", "Resonance Sharpness", 0.0f, 100.0f, 50.0f, Unit::percent));
    v.push_back (f ("rs_speed", "Resonance Speed", 0.0f, 100.0f, 50.0f, Unit::percent));
    v.push_back (f ("rs_mix", "Resonance Mix", 0.0f, 100.0f, 100.0f, Unit::percent));
    v.push_back (b ("rs_delta", "Resonance Delta Listen", false));

    // ---- 4.3 Multiband dynamics ------------------------------------------------------------
    static constexpr float xoDefaults[4] = { 80.0f, 250.0f, 1500.0f, 5500.0f };
    v.push_back (b ("mb_on", "Multiband On", false));
    v.push_back (c ("mb_xover_type", "Multiband Crossover", choice::xoverType, 0, false));
    for (int i = 1; i <= 4; ++i)
        v.push_back (f (mbXo (i), "Crossover " + juce::String (i), 30.0f, 16000.0f, xoDefaults[i - 1], Unit::hz, 700.0f));
    for (int band = 1; band <= mbBands; ++band)
    {
        const auto n = "MB " + juce::String (band) + " ";
        v.push_back (f (mb (band, "thr"), n + "Threshold", -60.0f, 0.0f, -20.0f, Unit::dB));
        v.push_back (f (mb (band, "ratio"), n + "Ratio", 1.0f, 10.0f, 1.5f, Unit::ratio, 2.5f));
        v.push_back (f (mb (band, "att"), n + "Attack", 0.5f, 200.0f, 20.0f, Unit::ms, 20.0f));
        v.push_back (f (mb (band, "rel"), n + "Release", 10.0f, 1000.0f, 200.0f, Unit::ms, 150.0f));
        v.push_back (b (mb (band, "rel_auto"), n + "Release Auto", false));
        v.push_back (f (mb (band, "knee"), n + "Knee", 0.0f, 12.0f, 6.0f, Unit::dB));
        v.push_back (f (mb (band, "makeup"), n + "Makeup", -12.0f, 12.0f, 0.0f, Unit::dB));
        v.push_back (f (mb (band, "mix"), n + "Mix", 0.0f, 100.0f, 100.0f, Unit::percent));
        v.push_back (f (mb (band, "link"), n + "Stereo Link", 0.0f, 100.0f, 100.0f, Unit::percent));
        v.push_back (c (mb (band, "mode"), n + "Mode", choice::stereoMode, 0));
        v.push_back (b (mb (band, "solo"), n + "Solo", false));
        v.push_back (b (mb (band, "bypass"), n + "Bypass", false));
        v.push_back (f (mb (band, "trim"), n + "Gain Trim", -12.0f, 12.0f, 0.0f, Unit::dB));
    }

    // ---- 4.4 Low-End Manager ---------------------------------------------------------------
    v.push_back (b ("lem_on", "Low-End On", false));
    v.push_back (b ("lem_mono_on", "Mono Maker On", true));
    v.push_back (f ("lem_mono_freq", "Mono Freq", 40.0f, 300.0f, 120.0f, Unit::hz, 120.0f));
    v.push_back (c ("lem_mono_slope", "Mono Slope", choice::slopeHpf, 1));
    v.push_back (f ("lem_mono_amount", "Mono Amount", 0.0f, 100.0f, 100.0f, Unit::percent));
    v.push_back (b ("lem_rot_on", "Phase Rotator On", false));
    v.push_back (f ("lem_rot_freq", "Rotator Freq", 30.0f, 200.0f, 80.0f, Unit::hz, 80.0f));
    v.push_back (f ("lem_rot_angle", "Rotation", 0.0f, 360.0f, 0.0f, Unit::degrees));
    v.push_back (b ("lem_tr_on", "Translator On", false));
    v.push_back (f ("lem_tr_amount", "Translator Amount", 0.0f, 100.0f, 0.0f, Unit::percent));
    v.push_back (f ("lem_tr_mix", "Translator 2nd/3rd", 0.0f, 100.0f, 50.0f, Unit::percent));

    // ---- 4.5 Bus compressor ----------------------------------------------------------------
    v.push_back (b ("bus_on", "Bus Comp On", false));
    v.push_back (c ("bus_model", "Bus Comp Model", choice::busModel, 0));
    v.push_back (f ("bus_thr", "Bus Comp Threshold", -40.0f, 0.0f, -12.0f, Unit::dB));
    v.push_back (c ("bus_ratio", "Bus Comp Ratio", choice::busRatio, 1));
    v.push_back (f ("bus_att", "Bus Comp Attack", 0.1f, 30.0f, 10.0f, Unit::ms, 5.0f));
    v.push_back (f ("bus_rel", "Bus Comp Release", 50.0f, 1200.0f, 200.0f, Unit::ms, 250.0f));
    v.push_back (b ("bus_rel_auto", "Bus Comp Release Auto", true));
    v.push_back (c ("bus_knee", "Bus Comp Knee", choice::knee, 0));
    v.push_back (f ("bus_sc_hpf", "Bus Comp SC HPF", 20.0f, 300.0f, 100.0f, Unit::hz, 90.0f));
    v.push_back (f ("bus_mix", "Bus Comp Mix", 0.0f, 100.0f, 100.0f, Unit::percent));
    v.push_back (f ("bus_detect", "Bus Comp Peak/RMS", 0.0f, 100.0f, 50.0f, Unit::percent));
    v.push_back (f ("bus_link", "Bus Comp Stereo Link", 0.0f, 100.0f, 100.0f, Unit::percent));
    v.push_back (b ("bus_makeup_auto", "Bus Comp Auto Makeup", false));

    // ---- 4.6 Saturation --------------------------------------------------------------------
    v.push_back (b ("sat_on", "Saturation On", false));
    v.push_back (f ("sat_xo1_freq", "Saturation Low/Mid", 60.0f, 500.0f, 150.0f, Unit::hz, 160.0f));
    v.push_back (f ("sat_xo2_freq", "Saturation Mid/High", 1000.0f, 10000.0f, 3000.0f, Unit::hz, 3000.0f));
    for (int band = 1; band <= satBands; ++band)
    {
        static const char* names[] = { "Low", "Mid", "High" };
        const auto n = juce::String ("Saturation ") + names[band - 1] + " ";
        v.push_back (f (sat (band, "drive"), n + "Drive", 0.0f, 24.0f, 0.0f, Unit::dB));
        v.push_back (c (sat (band, "type"), n + "Type", choice::satType, 0));
        v.push_back (f (sat (band, "bias"), n + "Asymmetry", 0.0f, 100.0f, 0.0f, Unit::percent));
        v.push_back (f (sat (band, "mix"), n + "Mix", 0.0f, 100.0f, 100.0f, Unit::percent));
        v.push_back (f (sat (band, "out"), n + "Output", -12.0f, 12.0f, 0.0f, Unit::dB));
    }

    // ---- 4.7 Stereo ------------------------------------------------------------------------
    v.push_back (b ("st_on", "Stereo On", false));
    v.push_back (f ("st_split_freq", "Stereo Mid/High Split", 1000.0f, 10000.0f, 4000.0f, Unit::hz, 3500.0f));
    v.push_back (f ("st_mid_width", "Mid Width", 0.0f, 200.0f, 100.0f, Unit::percent));
    v.push_back (f ("st_high_width", "High Width", 0.0f, 200.0f, 100.0f, Unit::percent));
    v.push_back (f ("st_ms_balance", "Mid/Side Balance", -6.0f, 6.0f, 0.0f, Unit::dB));
    v.push_back (b ("st_guard_on", "Correlation Guard", false));
    v.push_back (f ("st_guard_thr", "Guard Threshold", -1.0f, 1.0f, 0.0f, Unit::none));

    // ---- 4.8 Clipper -----------------------------------------------------------------------
    v.push_back (b ("clip_on", "Clipper On", false));
    v.push_back (c ("clip_type", "Clipper Type", choice::clipType, 1));
    v.push_back (f ("clip_thr", "Clipper Threshold", -12.0f, 0.0f, -1.0f, Unit::dBFS));
    v.push_back (f ("clip_shape", "Clipper Shape", 0.0f, 100.0f, 50.0f, Unit::percent));
    v.push_back (f ("clip_mix", "Clipper Mix", 0.0f, 100.0f, 100.0f, Unit::percent));
    v.push_back (b ("clip_lowprot", "Clipper Low Protect", true));

    // ---- 4.9 Limiter -----------------------------------------------------------------------
    v.push_back (b ("lim_on", "Limiter On", false));
    v.push_back (f ("lim_lookahead", "Limiter Lookahead", 0.5f, 5.0f, 2.0f, Unit::ms, 0.0f, 0.1f));
    v.back().automatable = false;
    v.push_back (f ("lim_rel", "Limiter Release", 10.0f, 500.0f, 100.0f, Unit::ms, 80.0f));
    v.push_back (b ("lim_rel_auto", "Limiter Release Auto", true));
    v.push_back (c ("lim_mode", "Limiter Mode", choice::limMode, 1));
    v.push_back (f ("lim_drive", "Limiter Drive", 0.0f, 24.0f, 0.0f, Unit::dB));
    v.push_back (f ("lim_ceiling", "Limiter Ceiling", -3.0f, 0.0f, -1.0f, Unit::dBTP));
    v.push_back (f ("lim_link", "Limiter Stereo Link", 0.0f, 100.0f, 100.0f, Unit::percent));
    v.push_back (f ("lim_transient", "Limiter Transients", 0.0f, 100.0f, 0.0f, Unit::percent));

    // ---- 4.10 Output -----------------------------------------------------------------------
    v.push_back (b ("out_on", "Output On", true));
    v.push_back (f ("out_gain", "Output Gain", -12.0f, 12.0f, 0.0f, Unit::dB));
    v.push_back (c ("out_dither", "Dither", choice::dither, 0));
    v.push_back (c ("out_bits", "Dither Bits", choice::ditherBits, 1));

    // ---- added in 0.7 (mockup v12, D-032): appended so that older projects keep their order --
    for (int band = 1; band <= satBands; ++band)
    {
        static const char* names[] = { "Low", "Mid", "High" };
        const auto n = juce::String ("Saturation ") + names[band - 1] + " ";
        v.push_back (b (sat (band, "solo"), n + "Solo", false));
        v.push_back (b (sat (band, "mute"), n + "Mute", false));
    }

    return v;
}
} // namespace

const std::vector<Spec>& all()
{
    static const std::vector<Spec> specs = build();
    return specs;
}

const Spec* find (const juce::String& id)
{
    static const auto index = []
    {
        std::unordered_map<std::string, size_t> m;
        const auto& specs = all();
        for (size_t i = 0; i < specs.size(); ++i)
            m.emplace (specs[i].id.toStdString(), i);
        return m;
    }();

    const auto it = index.find (id.toStdString());
    return it == index.end() ? nullptr : &all()[it->second];
}

juce::String formatValue (const Spec& spec, float value)
{
    if (spec.kind == Kind::toggle)
        return value >= 0.5f ? "On" : "Off";
    if (spec.kind == Kind::choice)
        return spec.choices[juce::jlimit (0, spec.choices.size() - 1, juce::roundToInt (value))];

    const auto signedDb = [] (float x, int decimals)
    {
        const auto s = juce::String (std::abs (x), decimals);
        return (x < -0.00049f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String()) + s;
    };

    switch (spec.unit)
    {
        case Unit::dB: return signedDb (value, 1) + " dB";
        case Unit::dBTP: return signedDb (value, 1) + " dBTP";
        case Unit::dBFS: return signedDb (value, 1) + " dBFS";
        case Unit::hz:
            if (value >= 1000.0f)
                return juce::String (value / 1000.0f, value >= 10000.0f ? 1 : 2) + " kHz";
            return juce::String (juce::roundToInt (value)) + " Hz";
        case Unit::ms:
            if (value >= 1000.0f)
                return juce::String (value / 1000.0f, 2) + " s";
            return juce::String (value, value < 10.0f ? 1 : 0) + " ms";
        case Unit::percent: return juce::String (juce::roundToInt (value)) + "%";
        case Unit::ratio: return juce::String (value, value < 10.0f ? 1 : 0) + ":1";
        case Unit::degrees: return juce::String (juce::roundToInt (value)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        case Unit::none: break;
    }

    return juce::String (value, std::abs (value) < 10.0f ? 2 : 1);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& s : all())
    {
        const juce::ParameterID pid { s.id, idVersion };
        const auto attributes = juce::AudioProcessorParameter::Category::genericParameter;
        juce::ignoreUnused (attributes);

        switch (s.kind)
        {
            case Kind::toggle:
                layout.add (std::make_unique<juce::AudioParameterBool> (
                    pid, s.name, s.defaultValue >= 0.5f,
                    juce::AudioParameterBoolAttributes().withAutomatable (s.automatable)));
                break;

            case Kind::choice:
                layout.add (std::make_unique<juce::AudioParameterChoice> (
                    pid, s.name, s.choices, juce::roundToInt (s.defaultValue),
                    juce::AudioParameterChoiceAttributes().withAutomatable (s.automatable)));
                break;

            case Kind::floating:
            {
                juce::NormalisableRange<float> range (s.minValue, s.maxValue, s.step);
                if (s.centre > 0.0f)
                    range.setSkewForCentre (s.centre);

                const Spec* specPtr = &s;
                layout.add (std::make_unique<juce::AudioParameterFloat> (
                    pid, s.name, range, s.defaultValue,
                    juce::AudioParameterFloatAttributes()
                        .withAutomatable (s.automatable)
                        .withStringFromValueFunction ([specPtr] (float x, int) { return formatValue (*specPtr, x); })));
                break;
            }
        }
    }

    return layout;
}

Raw::Raw (juce::AudioProcessorValueTreeState& state) : apvts (state) {}

std::atomic<float>* Raw::get (const juce::String& id) const
{
    auto* p = apvts.getRawParameterValue (id);
    jassert (p != nullptr);
    return p;
}
} // namespace mm::params
