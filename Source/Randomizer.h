#pragma once
#include "PluginProcessor.h"

namespace comma
{
// Что кубику можно менять. Настраивается в меню настроек → Randomizer, хранится в настройках плагина (не в пресете).
// Группы растут вместе с модулями: у каждого звучащего модуля появится своя.
struct RandomizerOptions
{
    enum Group
    {
        Mix = 0,   // Mix модулей, стоящих в rack'е
        Order,     // порядок слотов и параллельные группы (по умолчанию выключено)
        NumGroups
    };

    std::array<bool, NumGroups> enabled { true, false };

    bool allows (Group g) const noexcept { return enabled[(size_t) g]; }
    static juce::String name (Group g);
    static juce::String prefKey (Group g);
    static bool defaultFor (Group g) { return RandomizerOptions().allows (g); }
};

// Бросок кубика. Никогда не трогаются: громкость выхода, состав rack'а (какие модули стоят), их включение (bypass).
// Mix — не ниже 40 %, чтобы модуль было слышно.
void randomizePatch (CommaProcessor& p, juce::Random& rng, const RandomizerOptions& options = {});
} // namespace comma
