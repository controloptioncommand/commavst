#include "Params.h"

namespace comma
{
namespace
{
const std::array<ModuleInfo, kNumModules> infos { {
    { "doubler",    "Doubler",    "Two copies a few cents or one step apart, left and right in opposite directions.", 2 },
    { "harmonizer", "Harmonizer", "Adds voices at ratios or scale steps above and below the input.", 5 },
    { "staircase",  "Staircase",  "Each echo is one scale step higher. In N-EDO, N repeats climb an octave.", 2 },
    { "autotune",   "Autotune",   "Pulls a monophonic voice to the nearest step of the tuning. Speed sets the glide.", 5 },
    { "resonator",  "Resonator",  "Every MPE note rings a bank of resonators at its pitch, fed by the input.", 3 },
    { "shifter",    "Shifter",    "Frequency shift in hertz, barber-pole feedback, beats in time with the song.", 4 },
    { "varispeed",  "Varispeed",  "Tape slow-down and speed-up, wow and flutter, pitch ramps in cents.", 6 },
    { "spectral",   "Spectral",   "Moves partials onto the steps of the tuning so the timbre fits the scale.", 7 },
} };
}

const ModuleInfo& moduleInfo (int m) { return infos[(size_t) juce::jlimit (0, kNumModules - 1, m)]; }

int moduleFromKey (const juce::String& key)
{
    for (int m = 0; m < kNumModules; ++m)
        if (key == infos[(size_t) m].key) return m;
    return -1;
}

juce::String moduleParam (int module, const char* what) { return juce::String (moduleInfo (module).key) + "_" + what; }

juce::String formatPercent (float v) { return juce::String (juce::roundToInt (v)) + "%"; }

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto pct = AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return formatPercent (v); });

    for (int m = 0; m < kNumModules; ++m)
    {
        const String name = moduleInfo (m).name;
        auto grp = std::make_unique<AudioProcessorParameterGroup> (moduleInfo (m).key, name, " | ");
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { moduleParam (m, "on"), 1 }, name + " On", true));
        grp->addChild (std::make_unique<AudioParameterFloat> (ParameterID { moduleParam (m, "mix"), 1 }, name + " Mix",
                                                              NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pct));
        layout.add (std::move (grp));
    }

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ParamId::output, 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")
                                                           .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " dB"; })));
    return layout;
}
} // namespace comma
