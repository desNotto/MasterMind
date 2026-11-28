#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <vector>

/*  Every host-visible parameter of MasterMind.

    The IDs are FROZEN since M1 (spec section 9, CLAUDE.md). Never rename or remove an ID and never
    change the meaning of a choice index. A breaking change needs a migration in
    MasterMindProcessor::setStateInformation and the user's approval. The full table with ranges and
    defaults is generated into docs/PARAMETERS.md by the test suite. */
namespace mm::params
{
inline constexpr int idVersion = 1; // juce::ParameterID version hint

enum class Kind
{
    floating,
    toggle,
    choice
};

enum class Unit
{
    none,
    dB,
    hz,
    ms,
    percent,
    ratio,
    degrees,
    dBTP,
    dBFS,
    lufs
};

struct Spec
{
    juce::String id;
    juce::String name;
    Kind kind = Kind::floating;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float defaultValue = 0.0f;
    float centre = 0.0f; // skew centre for log-like ranges, 0 = linear
    float step = 0.0f;
    Unit unit = Unit::none;
    juce::StringArray choices;
    bool automatable = true;
};

/** All parameter specs in a fixed order. Built once, thread-safe after the first call. */
const std::vector<Spec>& all();

/** Looks up a spec by ID, nullptr if unknown. */
const Spec* find (const juce::String& id);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

/** Text for a parameter value with its unit, as shown in the UI ("4.80 kHz", "-2.5 dB"). */
juce::String formatValue (const Spec& spec, float value);

// ---- ID helpers --------------------------------------------------------------------------------
inline juce::String eq (int band, const char* field) { return "eq_b" + juce::String (band) + "_" + field; }
inline juce::String mb (int band, const char* field) { return "mb_b" + juce::String (band) + "_" + field; }
inline juce::String mbXo (int index) { return "mb_xo" + juce::String (index) + "_freq"; }
inline juce::String sat (int band, const char* field) { return "sat_b" + juce::String (band) + "_" + field; }

inline constexpr int eqBands = 10;
inline constexpr int mbBands = 5;
inline constexpr int satBands = 3;

// Choice lists (indices are part of the frozen format)
namespace choice
{
inline const juce::StringArray slopeHpf { "12 dB/oct", "24 dB/oct", "48 dB/oct" };
inline const juce::StringArray eqTypeLow { "Low shelf", "Low cut" };
inline const juce::StringArray eqTypeHigh { "High shelf", "High cut" };
inline const juce::StringArray eqTypeMid { "Bell", "Low shelf", "High shelf", "Notch" };
inline const juce::StringArray eqCutSlope { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
inline const juce::StringArray placement { "Stereo", "Mid", "Side", "Left", "Right" };
inline const juce::StringArray dynMode { "Compress", "Expand" };
inline const juce::StringArray phaseMode { "Minimum phase", "Linear phase" };
inline const juce::StringArray xoverType { "Linkwitz-Riley", "Linear phase" };
inline const juce::StringArray stereoMode { "Stereo", "Mid/Side" };
inline const juce::StringArray busModel { "VCA", "Punch" };
inline const juce::StringArray busRatio { "1.5:1", "2:1", "3:1", "4:1", "6:1", "10:1" };
inline const juce::StringArray knee { "Soft", "Hard" };
inline const juce::StringArray satType { "Tube", "Tape", "Transformer", "Diode" };
inline const juce::StringArray clipType { "Hard", "Soft", "Soft knee", "Diode" };
inline const juce::StringArray limMode { "Transparent", "Balanced", "Aggressive" };
inline const juce::StringArray limBands { "Classic", "8", "16", "26" };
inline const juce::StringArray dither { "Off", "TPDF", "Noise-shaped" };
inline const juce::StringArray ditherBits { "16 bit", "24 bit" };
inline const juce::StringArray quality { "Auto", "Normal", "High" };
} // namespace choice

inline constexpr float busRatioValues[] = { 1.5f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f };

/** Cached raw parameter pointers for lock-free reads on the audio thread. */
class Raw
{
public:
    explicit Raw (juce::AudioProcessorValueTreeState& state);

    std::atomic<float>* get (const juce::String& id) const;

    float value (const juce::String& id) const { return get (id)->load (std::memory_order_relaxed); }

private:
    juce::AudioProcessorValueTreeState& apvts;
};
} // namespace mm::params
