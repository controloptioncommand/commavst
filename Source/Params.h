#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace comma
{
// Модули rack'а. Каждый встречается в rack'е не больше одного раза, поэтому все параметры заведены заранее
// и их ID не меняются: автоматизация не ломается при перестановке слотов.
enum class ModuleId { Doubler = 0, Harmonizer, Staircase, Autotune, Resonator, Shifter, Varispeed, Spectral };
inline constexpr int kNumModules = 8;
inline constexpr int kMaxSlots = kNumModules;
inline constexpr int kNumModRows = 8;

struct ModuleInfo
{
    const char* key;          // часть ID параметров и имя в состоянии: "doubler"
    const char* name;         // в интерфейсе
    const char* description;  // одна-две строки, Callout
};
const ModuleInfo& moduleInfo (int module);
int moduleFromKey (const juce::String& key);   // −1, если такого нет

// Все параметры — одной таблицей. Номер в P — индекс в paramSpecs() и в массиве значений аудиопотока.
namespace P
{
enum : int
{
    // строй
    TunSource, TunEdo, TunRoot, TunRef,
    // нотный слой
    Glide, GlideMode, GlideRate, GlideCurve, GlideDomain, Snap, SnapSpeed, Character, Overshoot, Vibrato, Landing, BendRange, MpeAgg,
    // матрица: источник, назначение, сила × 8
    ModFirst, ModLast = ModFirst + kNumModRows * 3 - 1,
    // Doubler
    DblOn, DblMix, DblUnit, DblCents, DblRatio, DblSteps, DblDelay, DblFocus, DblWidth, DblWindow,
    // Harmonizer
    HarOn, HarMix, Har1Unit, Har1Cents, Har1Ratio, Har1Steps, Har1Level, Har2Unit, Har2Cents, Har2Ratio, Har2Steps, Har2Level, HarMode,
    // Staircase
    StaOn, StaMix, StaUnit, StaCents, StaRatio, StaSteps, StaTime, StaSync, StaDiv, StaFeedback, StaTone, StaWindow,
    // Autotune
    AutOn, AutMix, AutSpeed, AutAmount, AutTarget, AutMode,
    // Resonator
    ResOn, ResMix, ResDecay, ResBright, ResPartials, ResStretch, ResScale, ResRelease, ResDrive, ResVelocity,
    // Shifter
    ShiOn, ShiMix, ShiShift, ShiSync, ShiDiv, ShiDir, ShiFeedback, ShiSpread,
    // Varispeed
    VarOn, VarMix, VarSpeed, VarRamp, VarRampLen, VarWow, VarFlutter, VarBrake, VarBrakeTime,
    // Spectral
    SpeOn, SpeMix, SpeMode, SpeAmount, SpeQuality, SpeThreshold,
    Output,
    Count
};
inline int modSrc (int row) { return ModFirst + row * 3; }
inline int modDst (int row) { return ModFirst + row * 3 + 1; }
inline int modAmt (int row) { return ModFirst + row * 3 + 2; }
inline int moduleOn (int m) { static const int on[] = { DblOn, HarOn, StaOn, AutOn, ResOn, ShiOn, VarOn, SpeOn }; return on[m]; }
inline int moduleMix (int m) { return moduleOn (m) + 1; }
}

enum class PKind { Float, Int, Bool, Choice };

struct ParamSpec
{
    juce::String id, name, label;   // label — короткое имя для ручки
    int module = -1;                // −1 — общий
    PKind kind = PKind::Float;
    juce::NormalisableRange<float> range;
    float def = 0.0f;
    juce::StringArray choices;
    juce::String unit;              // "%", "ms", "Hz", "¢", "dB", "st", "steps"
    bool modulatable = false;       // можно вести из мод-матрицы
};
const std::vector<ParamSpec>& paramSpecs();
inline const ParamSpec& spec (int p) { return paramSpecs()[(size_t) p]; }
int paramIndex (const juce::String& id);   // −1, если нет
juce::String formatValue (int p, float v);

// Мод-матрица: источники и назначения
enum MpeSource { SrcNone = 0, SrcStrike, SrcLift, SrcPressure, SrcSlide, SrcBend, SrcNote, NumMpeSources };
juce::StringArray mpeSourceNames();
const std::vector<int>& modDestinations();   // индексы P для назначений 1…; 0 — нет
juce::String modDestName (int dstIndex);

// Деления такта
juce::StringArray noteDivNames();             // для задержки
double noteDivBeats (int index);
juce::StringArray cycleDivNames();            // для биений Shifter и рамп Varispeed
double cycleDivBeats (int index);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

namespace ParamId
{
    inline const juce::String output = "output";
}
inline juce::String moduleParam (int module, const char* what) { return juce::String (moduleInfo (module).key) + "_" + what; }
juce::String formatPercent (float v);
} // namespace comma
