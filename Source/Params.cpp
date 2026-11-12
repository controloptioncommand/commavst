#include "Params.h"
#include "Tuning/Tuning.h"

namespace comma
{
namespace
{
const std::array<ModuleInfo, kNumModules> infos { {
    { "doubler",    "Doubler",    "Two copies a few cents or one step apart, left up and right down. Focus keeps the lows dry." },
    { "harmonizer", "Harmonizer", "Two extra voices at ratios, cents or scale steps. Quality mode follows the pitch of a solo voice." },
    { "staircase",  "Staircase",  "Each echo is one interval higher. In N-EDO with one step, N repeats climb an octave." },
    { "autotune",   "Autotune",   "Pulls a solo voice to the nearest step of the tuning, or to the notes you hold. Speed sets the glide." },
    { "resonator",  "Resonator",  "Every MIDI or MPE note rings a bank of resonators at its pitch, fed by the input. Needs notes." },
    { "shifter",    "Shifter",    "Moves every partial by the same number of hertz. Feedback spins a barber pole, Sync beats in time." },
    { "varispeed",  "Varispeed",  "Tape speed: pitch in cents, ramps in time with the song, wow and flutter, and a tape brake." },
    { "spectral",   "Spectral",   "Snap moves partials onto the steps of the tuning. Stretch fits harmonics to the period of the tuning." },
} };

juce::StringArray units() { return { "Cents", "Ratio", "Steps" }; }
juce::StringArray modes() { return { "Low latency", "Quality" }; }
juce::StringArray rootNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }

std::vector<ParamSpec> build()
{
    std::vector<ParamSpec> s ((size_t) P::Count);
    auto f = [&s] (int p, juce::String id, juce::String name, juce::String label, int module, float lo, float hi, float def, juce::String unit,
                   bool mod = true, float skewCentre = 0.0f, float step = 0.0f)
    {
        auto& x = s[(size_t) p];
        x.id = id; x.name = name; x.label = label; x.module = module; x.kind = PKind::Float;
        x.range = juce::NormalisableRange<float> (lo, hi, step);
        if (skewCentre > lo && skewCentre < hi) x.range.setSkewForCentre (skewCentre);
        x.def = def; x.unit = unit; x.modulatable = mod;
    };
    auto i = [&s] (int p, juce::String id, juce::String name, juce::String label, int module, int lo, int hi, int def, juce::String unit)
    {
        auto& x = s[(size_t) p];
        x.id = id; x.name = name; x.label = label; x.module = module; x.kind = PKind::Int;
        x.range = juce::NormalisableRange<float> ((float) lo, (float) hi, 1.0f);
        x.def = (float) def; x.unit = unit;
    };
    auto b = [&s] (int p, juce::String id, juce::String name, juce::String label, int module, bool def)
    {
        auto& x = s[(size_t) p];
        x.id = id; x.name = name; x.label = label; x.module = module; x.kind = PKind::Bool;
        x.range = juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f);
        x.def = def ? 1.0f : 0.0f;
    };
    auto c = [&s] (int p, juce::String id, juce::String name, juce::String label, int module, juce::StringArray ch, int def)
    {
        auto& x = s[(size_t) p];
        x.id = id; x.name = name; x.label = label; x.module = module; x.kind = PKind::Choice;
        x.range = juce::NormalisableRange<float> (0.0f, (float) juce::jmax (1, ch.size() - 1), 1.0f);
        x.choices = ch; x.def = (float) def;
    };
    auto key = [] (int m, const char* what) { return juce::String (infos[(size_t) m].key) + "_" + what; };
    auto nm = [] (int m, const char* what) { return juce::String (infos[(size_t) m].name) + " " + what; };
    const auto ratios = ratioNames();
    const int r54 = ratioIndex (5, 4), r32 = ratioIndex (3, 2), r98 = ratioIndex (9, 8), r1615 = ratioIndex (16, 15);

    // строй
    c (P::TunSource, "tun_source", "Tuning", "Tuning", -1, { "12-EDO", "N-EDO", "Ratios", "Scala", "MTS-ESP" }, 0);
    i (P::TunEdo, "tun_edo", "EDO steps", "EDO", -1, 1, 96, 19, "");
    c (P::TunRoot, "tun_root", "Root", "Root", -1, rootNames(), 0);
    f (P::TunRef, "tun_ref", "A4", "A4", -1, 400.0f, 480.0f, 440.0f, "Hz", false, 0.0f, 0.01f);

    // нотный слой
    f (P::Glide, "glide", "Glide", "Glide", -1, 0.0f, 3000.0f, 0.0f, "ms", false, 300.0f);
    c (P::GlideMode, "glide_mode", "Glide mode", "Mode", -1, { "Time", "Rate" }, 0);
    f (P::GlideRate, "glide_rate", "Glide rate", "Rate", -1, 10.0f, 20000.0f, 1200.0f, "c/s", false, 1200.0f);
    c (P::GlideCurve, "glide_curve", "Glide curve", "Curve", -1, { "Linear", "Fast start", "Slow start" }, 0);
    c (P::GlideDomain, "glide_domain", "Glide in", "Glide in", -1, { "Cents", "Hertz" }, 0);
    f (P::Snap, "snap", "Snap", "Snap", -1, 0.0f, 100.0f, 0.0f, "%", false);
    f (P::SnapSpeed, "snap_speed", "Snap speed", "Speed", -1, 5.0f, 1000.0f, 120.0f, "ms", false, 120.0f);
    f (P::Character, "character", "Character", "Character", -1, 0.0f, 100.0f, 0.0f, "%", false);
    f (P::Overshoot, "overshoot", "Overshoot", "Overshoot", -1, 0.0f, 50.0f, 0.0f, "c", false);
    f (P::Vibrato, "vibrato", "Landing vibrato", "Vibrato", -1, 0.0f, 50.0f, 0.0f, "c", false);
    f (P::Landing, "landing", "Landing decay", "Decay", -1, 50.0f, 3000.0f, 600.0f, "ms", false, 600.0f);
    i (P::BendRange, "bend_range", "Bend range", "Bend", -1, 1, 96, 48, "st");
    c (P::MpeAgg, "mpe_agg", "MPE for effects", "MPE", -1, { "Last note", "Highest", "Average" }, 0);

    for (int r = 0; r < kNumModRows; ++r)
    {
        const juce::String n = juce::String (r + 1);
        c (P::modSrc (r), "m" + n + "_src", "Mod " + n + " source", "Source", -1, { "None" }, 0);   // список — ниже
        c (P::modDst (r), "m" + n + "_dst", "Mod " + n + " destination", "Destination", -1, { "None" }, 0);
        f (P::modAmt (r), "m" + n + "_amt", "Mod " + n + " amount", "Amount", -1, -100.0f, 100.0f, 0.0f, "%", false);
    }

    // Doubler
    b (P::DblOn, key (0, "on"), nm (0, "On"), "On", 0, true);
    f (P::DblMix, key (0, "mix"), nm (0, "Mix"), "Mix", 0, 0.0f, 100.0f, 100.0f, "%");
    c (P::DblUnit, key (0, "unit"), nm (0, "Unit"), "Unit", 0, units(), 0);
    f (P::DblCents, key (0, "cents"), nm (0, "Cents"), "Detune", 0, 0.0f, 100.0f, 9.0f, "c", true, 15.0f);
    c (P::DblRatio, key (0, "ratio"), nm (0, "Ratio"), "Ratio", 0, ratios, r1615);
    i (P::DblSteps, key (0, "steps"), nm (0, "Steps"), "Steps", 0, 0, 12, 1, "steps");
    f (P::DblDelay, key (0, "delay"), nm (0, "Delay"), "Delay", 0, 0.0f, 30.0f, 8.0f, "ms");
    f (P::DblFocus, key (0, "focus"), nm (0, "Focus"), "Focus", 0, 20.0f, 2000.0f, 150.0f, "Hz", true, 200.0f);
    f (P::DblWidth, key (0, "width"), nm (0, "Width"), "Width", 0, 0.0f, 100.0f, 100.0f, "%");
    f (P::DblWindow, key (0, "window"), nm (0, "Window"), "Window", 0, 20.0f, 60.0f, 30.0f, "ms");

    // Harmonizer
    b (P::HarOn, key (1, "on"), nm (1, "On"), "On", 1, true);
    f (P::HarMix, key (1, "mix"), nm (1, "Mix"), "Mix", 1, 0.0f, 100.0f, 100.0f, "%");
    c (P::Har1Unit, key (1, "v1_unit"), nm (1, "Voice 1 unit"), "Unit", 1, units(), 1);
    f (P::Har1Cents, key (1, "v1_cents"), nm (1, "Voice 1 cents"), "Voice 1", 1, -1200.0f, 1200.0f, 386.3f, "c");
    c (P::Har1Ratio, key (1, "v1_ratio"), nm (1, "Voice 1 ratio"), "Voice 1", 1, ratios, r54);
    i (P::Har1Steps, key (1, "v1_steps"), nm (1, "Voice 1 steps"), "Voice 1", 1, -24, 24, 2, "steps");
    f (P::Har1Level, key (1, "v1_level"), nm (1, "Voice 1 level"), "Level 1", 1, 0.0f, 100.0f, 100.0f, "%");
    c (P::Har2Unit, key (1, "v2_unit"), nm (1, "Voice 2 unit"), "Unit", 1, units(), 1);
    f (P::Har2Cents, key (1, "v2_cents"), nm (1, "Voice 2 cents"), "Voice 2", 1, -1200.0f, 1200.0f, 702.0f, "c");
    c (P::Har2Ratio, key (1, "v2_ratio"), nm (1, "Voice 2 ratio"), "Voice 2", 1, ratios, r32);
    i (P::Har2Steps, key (1, "v2_steps"), nm (1, "Voice 2 steps"), "Voice 2", 1, -24, 24, 4, "steps");
    f (P::Har2Level, key (1, "v2_level"), nm (1, "Voice 2 level"), "Level 2", 1, 0.0f, 100.0f, 70.0f, "%");
    c (P::HarMode, key (1, "mode"), nm (1, "Mode"), "Mode", 1, modes(), 1);

    // Staircase
    b (P::StaOn, key (2, "on"), nm (2, "On"), "On", 2, true);
    f (P::StaMix, key (2, "mix"), nm (2, "Mix"), "Mix", 2, 0.0f, 100.0f, 100.0f, "%");
    c (P::StaUnit, key (2, "unit"), nm (2, "Unit"), "Unit", 2, units(), 2);
    f (P::StaCents, key (2, "cents"), nm (2, "Cents"), "Step", 2, -1200.0f, 1200.0f, 100.0f, "c");
    c (P::StaRatio, key (2, "ratio"), nm (2, "Ratio"), "Step", 2, ratios, r98);
    i (P::StaSteps, key (2, "steps"), nm (2, "Steps"), "Step", 2, -24, 24, 1, "steps");
    f (P::StaTime, key (2, "time"), nm (2, "Time"), "Time", 2, 20.0f, 2000.0f, 300.0f, "ms", true, 300.0f);
    b (P::StaSync, key (2, "sync"), nm (2, "Sync"), "Sync", 2, true);
    c (P::StaDiv, key (2, "div"), nm (2, "Division"), "Division", 2, noteDivNames(), 5);
    f (P::StaFeedback, key (2, "feedback"), nm (2, "Feedback"), "Feedback", 2, 0.0f, 95.0f, 70.0f, "%");
    f (P::StaTone, key (2, "tone"), nm (2, "Tone"), "Tone", 2, 500.0f, 20000.0f, 9000.0f, "Hz", true, 4000.0f);
    f (P::StaWindow, key (2, "window"), nm (2, "Window"), "Window", 2, 20.0f, 60.0f, 40.0f, "ms");

    // Autotune
    b (P::AutOn, key (3, "on"), nm (3, "On"), "On", 3, true);
    f (P::AutMix, key (3, "mix"), nm (3, "Mix"), "Mix", 3, 0.0f, 100.0f, 100.0f, "%");
    f (P::AutSpeed, key (3, "speed"), nm (3, "Speed"), "Speed", 3, 0.0f, 500.0f, 25.0f, "ms", true, 60.0f);
    f (P::AutAmount, key (3, "amount"), nm (3, "Amount"), "Amount", 3, 0.0f, 100.0f, 100.0f, "%");
    c (P::AutTarget, key (3, "target"), nm (3, "Target"), "Target", 3, { "Scale", "Held notes" }, 0);
    c (P::AutMode, key (3, "mode"), nm (3, "Mode"), "Mode", 3, modes(), 1);

    // Resonator
    b (P::ResOn, key (4, "on"), nm (4, "On"), "On", 4, true);
    f (P::ResMix, key (4, "mix"), nm (4, "Mix"), "Mix", 4, 0.0f, 100.0f, 100.0f, "%");
    f (P::ResDecay, key (4, "decay"), nm (4, "Decay"), "Decay", 4, 20.0f, 8000.0f, 1500.0f, "ms", true, 800.0f);
    f (P::ResBright, key (4, "bright"), nm (4, "Brightness"), "Bright", 4, 0.0f, 100.0f, 50.0f, "%");
    i (P::ResPartials, key (4, "partials"), nm (4, "Partials"), "Partials", 4, 1, 8, 6, "");
    f (P::ResStretch, key (4, "stretch"), nm (4, "Stretch"), "Stretch", 4, -50.0f, 50.0f, 0.0f, "%");
    b (P::ResScale, key (4, "scale"), nm (4, "Partials on scale"), "On scale", 4, false);
    f (P::ResRelease, key (4, "release"), nm (4, "Release"), "Release", 4, 20.0f, 8000.0f, 800.0f, "ms", true, 600.0f);
    f (P::ResDrive, key (4, "drive"), nm (4, "Gain"), "Gain", 4, -24.0f, 24.0f, 0.0f, "dB");
    f (P::ResVelocity, key (4, "velocity"), nm (4, "Velocity"), "Velocity", 4, 0.0f, 100.0f, 50.0f, "%");

    // Shifter
    b (P::ShiOn, key (5, "on"), nm (5, "On"), "On", 5, true);
    f (P::ShiMix, key (5, "mix"), nm (5, "Mix"), "Mix", 5, 0.0f, 100.0f, 100.0f, "%");
    {
        auto& x = s[(size_t) P::ShiShift];
        f (P::ShiShift, key (5, "shift"), nm (5, "Shift"), "Shift", 5, -2000.0f, 2000.0f, 5.0f, "Hz");
        x.range = juce::NormalisableRange<float> (-2000.0f, 2000.0f, 0.0f, 0.35f, true);
    }
    b (P::ShiSync, key (5, "sync"), nm (5, "Sync"), "Sync", 5, false);
    c (P::ShiDiv, key (5, "div"), nm (5, "Beat cycle"), "Cycle", 5, cycleDivNames(), 4);
    c (P::ShiDir, key (5, "dir"), nm (5, "Direction"), "Direction", 5, { "Up", "Down" }, 0);
    f (P::ShiFeedback, key (5, "feedback"), nm (5, "Feedback"), "Feedback", 5, 0.0f, 95.0f, 0.0f, "%");
    f (P::ShiSpread, key (5, "spread"), nm (5, "Spread"), "Spread", 5, 0.0f, 100.0f, 0.0f, "%");

    // Varispeed
    b (P::VarOn, key (6, "on"), nm (6, "On"), "On", 6, true);
    f (P::VarMix, key (6, "mix"), nm (6, "Mix"), "Mix", 6, 0.0f, 100.0f, 100.0f, "%");
    f (P::VarSpeed, key (6, "speed"), nm (6, "Speed"), "Speed", 6, -1200.0f, 1200.0f, 0.0f, "c");
    f (P::VarRamp, key (6, "ramp"), nm (6, "Ramp"), "Ramp", 6, -1200.0f, 1200.0f, 0.0f, "c");
    c (P::VarRampLen, key (6, "ramplen"), nm (6, "Ramp length"), "Length", 6, cycleDivNames(), 2);
    f (P::VarWow, key (6, "wow"), nm (6, "Wow"), "Wow", 6, 0.0f, 100.0f, 15.0f, "c");
    f (P::VarFlutter, key (6, "flutter"), nm (6, "Flutter"), "Flutter", 6, 0.0f, 30.0f, 3.0f, "c");
    b (P::VarBrake, key (6, "brake"), nm (6, "Brake"), "Brake", 6, false);
    f (P::VarBrakeTime, key (6, "braketime"), nm (6, "Brake time"), "Brake time", 6, 50.0f, 4000.0f, 800.0f, "ms", true, 800.0f);

    // Spectral
    b (P::SpeOn, key (7, "on"), nm (7, "On"), "On", 7, true);
    f (P::SpeMix, key (7, "mix"), nm (7, "Mix"), "Mix", 7, 0.0f, 100.0f, 100.0f, "%");
    c (P::SpeMode, key (7, "mode"), nm (7, "Mode"), "Mode", 7, { "Snap", "Stretch" }, 0);
    f (P::SpeAmount, key (7, "amount"), nm (7, "Amount"), "Amount", 7, 0.0f, 100.0f, 100.0f, "%");
    c (P::SpeQuality, key (7, "quality"), nm (7, "Latency"), "Latency", 7, modes(), 1);
    f (P::SpeThreshold, key (7, "threshold"), nm (7, "Threshold"), "Threshold", 7, -90.0f, -20.0f, -60.0f, "dB");

    f (P::Output, "output", "Output", "Out", -1, -24.0f, 12.0f, 0.0f, "dB", false, 0.0f, 0.1f);

    // списки матрицы
    for (int r = 0; r < kNumModRows; ++r)
    {
        auto& src = s[(size_t) P::modSrc (r)];
        src.choices = { "None", "Strike", "Lift", "Pressure", "Slide", "Bend", "Note" };
        src.range = juce::NormalisableRange<float> (0.0f, (float) src.choices.size() - 1, 1.0f);
    }
    return s;
}

const std::vector<ParamSpec>& specsWithDestinations()
{
    static const std::vector<ParamSpec> s = []
    {
        auto v = build();
        juce::StringArray dst { "None" };
        for (int p = 0; p < P::Count; ++p)
            if (v[(size_t) p].modulatable) dst.add (juce::String (infos[(size_t) v[(size_t) p].module].name) + juce::String::fromUTF8 (" \xc2\xb7 ") + v[(size_t) p].label);
        for (int r = 0; r < kNumModRows; ++r)
        {
            auto& d = v[(size_t) P::modDst (r)];
            d.choices = dst;
            d.range = juce::NormalisableRange<float> (0.0f, (float) dst.size() - 1, 1.0f);
        }
        return v;
    }();
    return s;
}
} // namespace

const ModuleInfo& moduleInfo (int m) { return infos[(size_t) juce::jlimit (0, kNumModules - 1, m)]; }

int moduleFromKey (const juce::String& key)
{
    for (int m = 0; m < kNumModules; ++m)
        if (key == infos[(size_t) m].key) return m;
    return -1;
}

const std::vector<ParamSpec>& paramSpecs() { return specsWithDestinations(); }

int paramIndex (const juce::String& id)
{
    static const std::map<juce::String, int> idx = []
    {
        std::map<juce::String, int> m;
        for (int p = 0; p < P::Count; ++p) m[paramSpecs()[(size_t) p].id] = p;
        return m;
    }();
    auto it = idx.find (id);
    return it == idx.end() ? -1 : it->second;
}

juce::StringArray mpeSourceNames() { return spec (P::modSrc (0)).choices; }

const std::vector<int>& modDestinations()
{
    static const std::vector<int> d = []
    {
        std::vector<int> v { -1 };
        for (int p = 0; p < P::Count; ++p) if (paramSpecs()[(size_t) p].modulatable) v.push_back (p);
        return v;
    }();
    return d;
}

juce::String modDestName (int i) { return spec (P::modDst (0)).choices[juce::jlimit (0, (int) modDestinations().size() - 1, i)]; }

juce::StringArray noteDivNames() { return { "1/1", "1/2", "1/2 T", "1/4 D", "1/4", "1/8 D", "1/4 T", "1/8", "1/16 D", "1/8 T", "1/16", "1/16 T", "1/32" }; }
double noteDivBeats (int i)
{
    static const double b[] = { 4.0, 2.0, 4.0 / 3.0, 1.5, 1.0, 0.75, 2.0 / 3.0, 0.5, 0.375, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
    return b[juce::jlimit (0, 12, i)];
}

juce::StringArray cycleDivNames() { return { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32" }; }
double cycleDivBeats (int i)
{
    static const double b[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125 };
    return b[juce::jlimit (0, 7, i)];
}

juce::String formatPercent (float v) { return juce::String (juce::roundToInt (v)) + "%"; }

juce::String formatValue (int p, float v)
{
    const auto& s = spec (p);
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
    auto num = [&] (float x, int dec)
    {
        auto t = juce::String (std::abs (x), dec);
        return (x < 0.0f && t != juce::String (0.0f, dec) ? minus : juce::String()) + t;
    };
    switch (s.kind)
    {
        case PKind::Bool: return v > 0.5f ? "On" : "Off";
        case PKind::Choice: return s.choices[juce::jlimit (0, s.choices.size() - 1, juce::roundToInt (v))];
        case PKind::Int:
        {
            const int n = juce::roundToInt (v);
            if (s.unit == "steps") return (n > 0 && s.range.start < 0 ? "+" : "") + juce::String (n) + (std::abs (n) == 1 ? " step" : " steps");
            if (s.unit == "st") return juce::String::fromUTF8 ("\xc2\xb1") + juce::String (n) + " st";
            return juce::String (n);
        }
        case PKind::Float: break;
    }
    if (s.unit == "%") return (s.range.start < 0 && v > 0.5f ? "+" : "") + num (v, 0) + "%";
    if (s.unit == "ms") return v >= 1000.0f ? num (v / 1000.0f, 2) + " s" : num (v, v < 10.0f ? 1 : 0) + " ms";
    if (s.unit == "Hz")
    {
        if (std::abs (v) >= 1000.0f) return num (v / 1000.0f, 2) + " kHz";
        return (s.range.start < 0 && v > 0.0f ? "+" : "") + num (v, std::abs (v) < 10.0f ? 2 : (std::abs (v) < 100.0f ? 1 : 0)) + " Hz";
    }
    if (s.unit == "c") return (s.range.start < 0 && v > 0.05f ? "+" : "") + num (v, std::abs (v) < 100.0f ? 1 : 0) + juce::String::fromUTF8 (" \xc2\xa2");
    if (s.unit == "c/s") return num (v, 0) + juce::String::fromUTF8 (" \xc2\xa2/s");
    if (s.unit == "dB") return (v > 0.05f ? "+" : "") + num (v, 1) + " dB";
    return num (v, 2);
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    std::array<std::unique_ptr<AudioProcessorParameterGroup>, kNumModules> groups;
    for (int m = 0; m < kNumModules; ++m) groups[(size_t) m] = std::make_unique<AudioProcessorParameterGroup> (infos[(size_t) m].key, infos[(size_t) m].name, " | ");
    auto tuning = std::make_unique<AudioProcessorParameterGroup> ("tuning", "Tuning", " | ");
    auto notes = std::make_unique<AudioProcessorParameterGroup> ("notes", "Notes", " | ");
    auto matrix = std::make_unique<AudioProcessorParameterGroup> ("matrix", "Matrix", " | ");

    for (int p = 0; p < P::Count; ++p)
    {
        const auto& s = paramSpecs()[(size_t) p];
        // ID из v0.1 (on, mix, output) — версия 1, остальные — 2
        const bool v1 = p == P::Output || (s.module >= 0 && (s.id.endsWith ("_on") || s.id.endsWith ("_mix")));
        const ParameterID pid { s.id, v1 ? 1 : 2 };
        std::unique_ptr<RangedAudioParameter> prm;
        switch (s.kind)
        {
            case PKind::Bool: prm = std::make_unique<AudioParameterBool> (pid, s.name, s.def > 0.5f); break;
            case PKind::Choice: prm = std::make_unique<AudioParameterChoice> (pid, s.name, s.choices, (int) s.def); break;
            case PKind::Int:
                prm = std::make_unique<AudioParameterInt> (pid, s.name, (int) s.range.start, (int) s.range.end, (int) s.def,
                                                           AudioParameterIntAttributes().withStringFromValueFunction ([p] (int v, int) { return formatValue (p, (float) v); }));
                break;
            case PKind::Float:
                prm = std::make_unique<AudioParameterFloat> (pid, s.name, s.range, s.def,
                                                             AudioParameterFloatAttributes().withLabel (s.unit)
                                                                 .withStringFromValueFunction ([p] (float v, int) { return formatValue (p, v); }));
                break;
        }
        if (s.module >= 0) groups[(size_t) s.module]->addChild (std::move (prm));
        else if (p >= P::TunSource && p <= P::TunRef) tuning->addChild (std::move (prm));
        else if (p >= P::ModFirst && p <= P::ModLast) matrix->addChild (std::move (prm));
        else if (p == P::Output) layout.add (std::move (prm));
        else notes->addChild (std::move (prm));
    }
    layout.add (std::move (tuning), std::move (notes), std::move (matrix));
    for (auto& g : groups) layout.add (std::move (g));
    return layout;
}
} // namespace comma
