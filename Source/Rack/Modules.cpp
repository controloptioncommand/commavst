#include "Module.h"
#include "Params.h"

namespace comma
{
// Этап 0: все модули пропускают сигнал без изменений. Алгоритмы появляются по этапам (ModuleInfo::stage).
std::unique_ptr<Module> createModule (int moduleId)
{
    juce::ignoreUnused (moduleId);
    return std::make_unique<Module>();
}
} // namespace comma
