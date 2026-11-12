#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace comma
{
// Модули rack'а. Каждый встречается в rack'е не больше одного раза, поэтому все параметры заведены заранее
// и их ID не меняются: автоматизация не ломается при перестановке слотов.
enum class ModuleId { Doubler = 0, Harmonizer, Staircase, Autotune, Resonator, Shifter, Varispeed, Spectral };
inline constexpr int kNumModules = 8;
inline constexpr int kMaxSlots = kNumModules;

struct ModuleInfo
{
    const char* key;          // часть ID параметров и имя в состоянии: "doubler"
    const char* name;         // в интерфейсе
    const char* description;  // одна-две строки, Callout
    int stage;                // этап, на котором модуль начинает звучать
};
const ModuleInfo& moduleInfo (int module);
int moduleFromKey (const juce::String& key);   // −1, если такого нет

namespace ParamId
{
    inline constexpr const char* output = "output";   // громкость выхода, dB; пресеты и кубик её не трогают
}

// Параметры слота модуля: "doubler_on" (включён, иначе bypass), "doubler_mix" (0–100 %)
juce::String moduleParam (int module, const char* what);

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
juce::String formatPercent (float v);
} // namespace comma
