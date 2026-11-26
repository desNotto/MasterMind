#include "monitoring/Profiles.h"

namespace mm::monitoring
{
namespace
{
double numberAfter (const juce::StringArray& tokens, const juce::String& key, double fallback, bool& found)
{
    const int i = tokens.indexOf (key, true);
    if (i < 0 || i + 1 >= tokens.size())
        return fallback;
    found = true;
    return tokens[i + 1].getDoubleValue();
}

const char* typeName (FilterSpec::Type t)
{
    switch (t)
    {
        case FilterSpec::lowShelf: return "lowShelf";
        case FilterSpec::highShelf: return "highShelf";
        case FilterSpec::lowPass: return "lowPass";
        case FilterSpec::highPass: return "highPass";
        default: return "peak";
    }
}

FilterSpec make (FilterSpec::Type t, double f, double g = 0.0, double q = 0.71, int order = 2)
{
    FilterSpec s;
    s.type = t;
    s.freq = f;
    s.gainDb = g;
    s.q = q;
    s.order = order;
    return s;
}
} // namespace

bool parseParametric (const juce::String& text, ParametricProfile& out, juce::String& error)
{
    out = {};
    juce::StringArray lines;
    lines.addLines (text);
    for (auto line : lines)
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar ('#'))
            continue;
        if (line.startsWithIgnoreCase ("Preamp"))
        {
            out.preampDb = line.fromFirstOccurrenceOf (":", false, false).trim().getDoubleValue();
            continue;
        }
        if (! line.startsWithIgnoreCase ("Filter"))
            continue;

        juce::StringArray t;
        t.addTokens (line.fromFirstOccurrenceOf (":", false, false), " \t", "");
        t.removeEmptyStrings();
        if (t.size() < 2 || ! t[0].equalsIgnoreCase ("ON"))
            continue;

        const auto type = t[1].toUpperCase();
        bool hasFc = false, hasGain = false, hasQ = false;
        const double fc = numberAfter (t, "Fc", 1000.0, hasFc);
        const double gain = numberAfter (t, "Gain", 0.0, hasGain);
        const double q = numberAfter (t, "Q", 0.71, hasQ);
        if (! hasFc || fc < 10.0 || fc > 24000.0)
            continue;

        if (type == "PK" || type == "PEQ")
            out.filters.push_back (make (FilterSpec::peak, fc, gain, hasQ ? q : 1.0));
        else if (type == "LS" || type == "LSC" || type.startsWith ("LS "))
            out.filters.push_back (make (FilterSpec::lowShelf, fc, gain, hasQ ? q : 0.71));
        else if (type == "HS" || type == "HSC")
            out.filters.push_back (make (FilterSpec::highShelf, fc, gain, hasQ ? q : 0.71));
        else if (type == "LP" || type == "LPQ")
            out.filters.push_back (make (FilterSpec::lowPass, fc, 0.0, hasQ ? q : 0.71));
        else if (type == "HP" || type == "HPQ")
            out.filters.push_back (make (FilterSpec::highPass, fc, 0.0, hasQ ? q : 0.71));
    }
    if (out.filters.empty())
    {
        error = "No usable filters found (expected lines like \"Filter 1: ON PK Fc 105 Hz Gain -3.2 dB Q 1.41\").";
        return false;
    }
    return true;
}

std::vector<SystemPreset> defaultSystemPresets()
{
    std::vector<SystemPreset> v;
    {
        SystemPreset p;
        p.name = "Laptop";
        p.filters = { make (FilterSpec::highPass, 180.0, 0, 0.71, 4), make (FilterSpec::lowPass, 12000.0, 0, 0.71, 2),
                      make (FilterSpec::peak, 3000.0, 3.0, 1.2) };
        p.mono = true;
        v.push_back (p);
    }
    {
        SystemPreset p;
        p.name = "Phone";
        p.filters = { make (FilterSpec::highPass, 300.0, 0, 0.71, 4), make (FilterSpec::lowPass, 9000.0, 0, 0.71, 2) };
        p.mono = true;
        v.push_back (p);
    }
    {
        SystemPreset p;
        p.name = "Bluetooth speaker";
        p.filters = { make (FilterSpec::highPass, 90.0, 0, 0.71, 4), make (FilterSpec::peak, 120.0, 4.0, 1.0),
                      make (FilterSpec::lowPass, 14000.0, 0, 0.71, 2) };
        p.softLimit = true;
        v.push_back (p);
    }
    {
        SystemPreset p;
        p.name = "Earbuds";
        p.filters = { make (FilterSpec::lowShelf, 100.0, 2.0, 0.71), make (FilterSpec::highShelf, 8000.0, -3.0, 0.71) };
        v.push_back (p);
    }
    {
        SystemPreset p;
        p.name = "Car";
        p.filters = { make (FilterSpec::lowShelf, 80.0, 5.0, 0.71), make (FilterSpec::peak, 2500.0, -3.0, 1.0),
                      make (FilterSpec::lowPass, 12000.0, 0, 0.71, 2) };
        v.push_back (p);
    }
    {
        SystemPreset p;
        p.name = "Small bookshelf";
        p.filters = { make (FilterSpec::highPass, 60.0, 0, 0.71, 4) };
        v.push_back (p);
    }
    return v;
}

juce::var systemPresetToJson (const SystemPreset& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", p.name);
    o->setProperty ("note", "Rough approximation, not a measurement. Edit freely.");
    o->setProperty ("mono", p.mono);
    o->setProperty ("softLimit", p.softLimit);
    juce::Array<juce::var> filters;
    for (const auto& f : p.filters)
    {
        auto* fo = new juce::DynamicObject();
        fo->setProperty ("type", typeName (f.type));
        fo->setProperty ("freq", f.freq);
        fo->setProperty ("gain", f.gainDb);
        fo->setProperty ("q", f.q);
        fo->setProperty ("order", f.order);
        filters.add (juce::var (fo));
    }
    o->setProperty ("filters", filters);
    return juce::var (o);
}

bool systemPresetFromJson (const juce::var& v, SystemPreset& out)
{
    if (! v.isObject())
        return false;
    out = {};
    out.name = v.getProperty ("name", "").toString();
    out.mono = (bool) v.getProperty ("mono", false);
    out.softLimit = (bool) v.getProperty ("softLimit", false);
    if (auto* arr = v.getProperty ("filters", {}).getArray())
        for (const auto& f : *arr)
        {
            FilterSpec s;
            const auto t = f.getProperty ("type", "peak").toString();
            s.type = t == "lowShelf" ? FilterSpec::lowShelf
                   : t == "highShelf" ? FilterSpec::highShelf
                   : t == "lowPass"   ? FilterSpec::lowPass
                   : t == "highPass"  ? FilterSpec::highPass
                                      : FilterSpec::peak;
            s.freq = juce::jlimit (10.0, 24000.0, (double) f.getProperty ("freq", 1000.0));
            s.gainDb = juce::jlimit (-24.0, 24.0, (double) f.getProperty ("gain", 0.0));
            s.q = juce::jlimit (0.1, 18.0, (double) f.getProperty ("q", 0.71));
            s.order = juce::jlimit (1, 8, (int) f.getProperty ("order", 2));
            out.filters.push_back (s);
        }
    return out.name.isNotEmpty();
}

std::vector<SystemPreset> loadSystemPresets (const juce::File& folder, juce::StringArray& errors)
{
    folder.createDirectory();
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.json");
    if (files.isEmpty())
    {
        int index = 1;
        for (const auto& p : defaultSystemPresets())
        {
            const auto f = folder.getChildFile (juce::String (index++) + " " + p.name + ".json");
            f.replaceWithText (juce::JSON::toString (systemPresetToJson (p)));
        }
        files = folder.findChildFiles (juce::File::findFiles, false, "*.json");
    }
    files.sort();
    std::vector<SystemPreset> out;
    for (const auto& f : files)
    {
        SystemPreset p;
        juce::var v;
        const auto result = juce::JSON::parse (f.loadFileAsString(), v);
        if (result.failed() || ! systemPresetFromJson (v, p))
        {
            errors.add (f.getFileName() + ": " + (result.failed() ? result.getErrorMessage() : juce::String ("missing name")));
            continue;
        }
        out.push_back (p);
    }
    if (out.empty())
        out = defaultSystemPresets();
    return out;
}

std::vector<dsp::Coeffs> design (const FilterSpec& f, double fs)
{
    switch (f.type)
    {
        case FilterSpec::lowShelf: return { dsp::Coeffs::lowShelf (fs, f.freq, f.q, f.gainDb) };
        case FilterSpec::highShelf: return { dsp::Coeffs::highShelf (fs, f.freq, f.q, f.gainDb) };
        case FilterSpec::lowPass: return dsp::butterworth (f.order, false, fs, f.freq);
        case FilterSpec::highPass: return dsp::butterworth (f.order, true, fs, f.freq);
        default: return { dsp::Coeffs::peak (fs, f.freq, f.q, f.gainDb) };
    }
}
} // namespace mm::monitoring
