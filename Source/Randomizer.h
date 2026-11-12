#pragma once
#include "PluginProcessor.h"

namespace comma
{
// Что кубику можно менять. Настраивается в меню настроек → Randomizer, хранится в настройках плагина (не в пресете).
struct RandomizerOptions
{
    enum Group
    {
        Mix = 0,   // Mix модулей, стоящих в rack'е
        Order,     // порядок слотов и параллельные группы (по умолчанию выключено)
        Modules,   // ручки модулей, стоящих в rack'е
        NumGroups
    };

    std::array<bool, NumGroups> enabled { true, false, true };

    bool allows (Group g) const noexcept { return enabled[(size_t) g]; }
    static juce::String name (Group g);
    static juce::String prefKey (Group g);
    static bool defaultFor (Group g) { return RandomizerOptions().allows (g); }
};

// Бросок кубика. Никогда не трогаются: громкость выхода, состав rack'а, bypass, строй, нотный слой и матрица,
// режимы задержки, синхронизация, тормоз Varispeed, порог Spectral, цель Autotune.
// Mix — не ниже 40 %, обратная связь — не выше 75 %.
void randomizePatch (CommaProcessor& p, juce::Random& rng, const RandomizerOptions& options = {});
} // namespace comma
