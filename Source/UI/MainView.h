#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"

namespace comma
{
// Главный вид. Этап 0: круг строя 12-EDO (без живых высот) и место под анализатор отклонения.
// На этапе 1 круг показывает текущий строй и высоты голосов, справа — анализатор в центах; на этапе 3 — MPE-голоса.
class MainView : public juce::Component
{
public:
    explicit MainView (CommaProcessor& p) : proc (p) { setTitle ("Tuning view"); setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override;

private:
    CommaProcessor& proc;
};
} // namespace comma
