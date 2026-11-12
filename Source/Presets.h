#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Rack/Rack.h"

namespace comma
{
// Фабричный пресет: состав rack'а, строй и значения ручек (остальные — по умолчанию). Громкость выхода и файлы Scala не трогает.
struct FactoryPreset
{
    juce::String name;
    std::vector<int> rack;                                      // модули по порядку
    std::vector<int> parallel;                                  // слоты, которые идут «‖ с предыдущим»
    std::vector<std::pair<int, float>> values;                  // P::… → значение
    juce::String ratios;                                        // строй «Ratios», если шаблон его задаёт

    FactoryPreset (juce::String n, std::vector<int> r, std::vector<int> par, std::vector<std::pair<int, float>> v, juce::String rat = {})
        : name (std::move (n)), rack (std::move (r)), parallel (std::move (par)), values (std::move (v)), ratios (std::move (rat)) {}
};

const std::vector<FactoryPreset>& factoryPresets();

// Пресеты: фабричные и пользовательские (~/Music/Comma/Presets/*.comma, внутри — состав rack'а и все ручки)
class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& state, std::function<void()> onStateReplaced);

    int getNumFactory() const { return (int) factoryPresets().size(); }
    void loadFactory (int index);
    void step (int dir);
    juce::String getCurrentName() const;
    void setCurrentName (const juce::String& n);

    static juce::File userFolder();
    juce::Array<juce::File> userPresets() const;
    bool loadUser (const juce::File& f);
    bool loadUserTree (const juce::ValueTree& t);   // для тестов и loadUser
    bool save (const juce::String& name);

    static bool isSettingNotSound (const juce::String& paramId);   // пресет их не трогает: Output

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::function<void()> onStateReplaced;
};
} // namespace comma
