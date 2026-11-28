#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <random>
#include <vector>

namespace mm::automatch
{
/** Auto match engine (D-040, mockup v18). Three modes share one idea: a recording of the user's
    song (about 12 s of the plugin input, the loudest 8 s are used) is played through a private copy
    of the whole processing chain (the "twin") on background threads, and the result is measured
    with the same analysis the plugin shows.

    - Fast: one neutral pass, then the knobs are set by formulas (after Matchering and the Ozone
      Master Assistant: EQ from the tonal difference, compression from the crest factor, width from
      the side share, Maximizer gain from the loudness), then three short correction passes.
    - Precise: a search over 18 knobs (separable CMA-ES, after ST-ITO by Steinmetz et al., ISMIR 2024)
      from neutral settings, with a prior that keeps the knobs near neutral unless the match needs it.
    - Hybrid: Fast gives the starting point, then a short search around it.

    Everything here runs off the audio thread. The twins are created and destroyed on the message
    thread (their parameter tree owns a timer), the search runs on a juce::Thread and its workers. */

using ParamMap = std::map<juce::String, float>; // parameter ID -> plain value

/** What the analysis measures on a processed excerpt. */
struct Features
{
    std::array<double, 6> tonal {}, crest {};
    std::array<double, 4> width {};
    double lufs = -100.0, truePeak = -100.0, harshness = 0.0, lowCorr = 1.0;
    // module meters averaged over the excerpt (for the formulas)
    std::array<double, 5> mbLevel {}, mbGr {};
    double busLevel = -100.0, busGr = 0.0;
    std::array<double, 2> eqLevel { -100.0, -100.0 }; // detector level of the dynamic EQ bands 1 and 2
    double grStd = 0.0; // pumping: standard deviation of the limiter gain reduction (10 ms means), dB
    double grMean = 0.0; // average limiter gain reduction, dB (>= 0)
    double clipDb = 0.0; // average peak reduction of the spectral clipper, dB
};

/** The reference the match aims at (reference set or the selected reference). */
struct Target
{
    std::array<double, 6> tonal {}, corridor {}, crest {};
    std::array<double, 4> width {};
    double lufs = -9.0, harshness = 0.0, ceiling = -1.0;
    bool hasCrest = false, hasWidth = false, hasHarshness = false;
};

/** The knobs the search moves, with their ranges (plain values). */
struct Dim
{
    juce::String id;
    float lo, hi, neutral;
};
const std::vector<Dim>& dims();

/** Distance between measured features and the target (lower is better). The prior term keeps the
    knobs near `prior` (normalised coordinates 0..1) with the given weight. */
double cost (const Features& f, const Target& t, const std::vector<double>& x, const std::vector<double>& prior, double priorWeight);

/** Normalised coordinates <-> plain values of dims(). */
std::vector<double> toUnit (const ParamMap& p);
void fromUnit (const std::vector<double>& x, ParamMap& p);

/** Private copy of the chain with its own parameter tree. */
class Twin
{
public:
    Twin (double sampleRate, const ParamMap& base);
    ~Twin();
    /** Sets the knobs, resets the chain, runs the excerpt (the first `preroll` samples only settle
        the chain) and measures the rest. Thread-safe per instance. */
    Features run (const ParamMap& knobs, const juce::AudioBuffer<float>& audio, int preroll);

private:
    struct Host;
    std::unique_ptr<Host> host;
    double fs;
    juce::AudioBuffer<float> work;
    std::vector<double> grMeans;
};

enum class Mode
{
    fast = 0,
    hybrid = 1,
    precise = 2
};

/** Stages for the strip: 0 listen (outside), 1 analyse or formula, 2 search, 3 verify, 4 apply. */
class Matcher final : public juce::Thread
{
public:
    /** `fixed`: the setup the match always applies (input, EQ band layout, module switches). Must be
        constructed on the message thread. */
    Matcher (Mode mode, double sampleRate, const ParamMap& current, const ParamMap& fixed, juce::AudioBuffer<float> excerpt, int preroll, Target target, int threads);
    ~Matcher() override;

    void run() override;

    std::atomic<int> stage { 1 }, evals { 0 }, plannedEvals { 1 };
    std::atomic<float> bestCost { -1.0f }, startCost { -1.0f };
    std::atomic<bool> done { false };

    /** After done: every parameter to set (fixed setup, conditional fixes, the knobs). */
    ParamMap result;
    Features finalFeatures, neutralFeatures;
    juce::String note; // plain-language summary of what the match changed

    static int plannedEvaluations (Mode m);

private:
    Features eval (int worker, const ParamMap& p);
    void evalBatch (std::vector<ParamMap>& ps, std::vector<Features>& out);
    ParamMap formula (const ParamMap& start, const Features& neutral);
    void correct (ParamMap& p, const Features& f, const std::array<double, 5>& mbTargetGr);
    std::vector<double> search (const std::vector<double>& start, double sigma, int lambda, int generations, const std::vector<double>& prior, double priorWeight, ParamMap& base,
                                double& bestOut);

    Mode mode;
    double fs;
    ParamMap base;
    juce::AudioBuffer<float> audio;
    int preroll;
    Target target;
    std::vector<std::unique_ptr<Twin>> twins;
    std::array<double, 5> mbTargetGr {};
};

/** Reach target with a distortion budget (D-042): every candidate mix of the loudness stages
    (Inflator Effect, Clip, Leveler, Density, Soften) gets the Limiter Drive that reaches the target
    loudness on the twin (secant steps); of those that reach it, the one with the least artefacts
    (limiter gain reduction, pumping, clipping, density) wins. Construct on the message thread. */
class LoudnessSearch final : public juce::Thread
{
public:
    LoudnessSearch (double sampleRate, const ParamMap& current, juce::AudioBuffer<float> excerpt, int preroll, double targetLufs, int threads);
    ~LoudnessSearch() override;
    void run() override;

    std::atomic<int> evals { 0 }, plannedEvals { 1 };
    std::atomic<bool> done { false };
    ParamMap result;     // the parameters to set
    double reachedLufs = -100.0, artefacts = 0.0;
    bool reached = false;

    /** Artefact cost of a processed excerpt with these stage settings (lower is cleaner). */
    static double artefactCost (const Features& f, const ParamMap& p);

private:
    struct Candidate
    {
        float infl, clip, density, soften, leveler;
        int mode = -1;          // Limiter mode; -1 keeps the current one
        float adaptGain = -1.0f; // band Adaptive Gain; -1 keeps the current one
        float outClip = 0.0f;    // output clipper (band modes)
    };
    struct Outcome
    {
        ParamMap p;
        Features f;
        bool ok = false;
    };
    Outcome solve (int worker, const Candidate& c);

    double fs;
    ParamMap base;
    juce::AudioBuffer<float> audio;
    int preroll;
    double target;
    std::vector<std::unique_ptr<Twin>> twins;
};

/** The fixed part of every match: subsonic filter, EQ band layout, neutral dynamics, gentle
    saturation, stereo guard, Inflator neutral, Limiter on with a ceiling of at most -1 dBTP. */
ParamMap fixedSetup (const ParamMap& current, double inputLufs);
} // namespace mm::automatch
