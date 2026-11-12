#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"

namespace comma
{
// Ручки модуля под rack'ом. Колонка — ручка и, при нужде, переключатель над ней:
// единицы интервала (центы, отношение, шаги) меняют ручку под собой, Sync меняет время на деление такта.
class ModulePanel : public juce::Component
{
public:
    ModulePanel (CommaProcessor& p, int module);
    void refresh();
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    struct Column
    {
        std::unique_ptr<juce::Component> top;
        std::vector<std::unique_ptr<ParamKnob>> knobs;   // варианты: виден один
        int selector = -1;                               // параметр, по которому выбирается вариант
        int width = 92;
    };
    Column& knob (int p);
    Column& interval (int unitP, int centsP, int ratioP, int stepsP);
    Column& sync (int syncP, int timeP, int divP);
    Column& toggled (int toggleP, int knobP);
    void header (int choiceP, int width);

    CommaProcessor& proc;
    int module;
    std::vector<Column> cols;
    std::vector<std::pair<std::unique_ptr<ChoiceSegmented>, int>> heads;
    std::function<juce::String()> readout;
    juce::String shownReadout;
    juce::Rectangle<int> readoutArea;
};
} // namespace comma
