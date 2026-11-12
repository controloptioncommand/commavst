#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"
#include "ModulePanel.h"

namespace comma
{
// Карточка слота: имя модуля, крестик (убрать из rack'а), ручка Mix, переключатель On (иначе bypass).
// Перетаскивание за карточку меняет порядок; с клавиатуры — Alt + ← / →.
class SlotCard : public juce::Component
{
public:
    SlotCard (CommaProcessor& p, int module);
    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    bool keyPressed (const juce::KeyPress& k) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    int getModule() const { return module; }

    std::function<void (int module)> onSelect, onRemove;
    std::function<void (int module, int dir)> onNudge;
    std::function<void (int module, int x)> onDragMove;   // x — центр карточки в координатах родителя
    std::function<void (int module, int x)> onDragEnd;

private:
    struct CloseButton : juce::Button
    {
        CloseButton() : juce::Button ("Remove") {}
        void paintButton (juce::Graphics& g, bool over, bool down) override;
    };

    CommaProcessor& proc;
    int module;
    bool selected = false, dragging = false;
    juce::Point<int> dragStart;
    int dragOffset = 0;
    ParamKnob mix { "Mix" };
    ParamToggle on { "On" };
    CloseButton close;
};

// Связка между соседними слотами: шеврон — последовательно, «‖» — параллельно. Клик переключает.
class LinkButton : public juce::Button
{
public:
    LinkButton() : juce::Button ("Link") {}
    void setParallel (bool p) { parallel = p; repaint(); }
    void paintButton (juce::Graphics& g, bool over, bool down) override;

private:
    bool parallel = false;
};

// Панель rack'а: заголовок, «Add module», ряд карточек, сведения о выбранном модуле
class RackView : public juce::Component
{
public:
    explicit RackView (CommaProcessor& p);
    void paint (juce::Graphics& g) override;
    void paintOverChildren (juce::Graphics& g) override;
    void resized() override;
    void refresh();   // по таймеру редактора: перестроить ряд, если состав сменился

    int getSelectedModule() const { return selected; }
    void selectModule (int m);
    void openAddMenu();

    // для тестов
    void dragForTest (int fromSlot, int toSlot);

private:
    struct AddButton : juce::Button
    {
        AddButton() : juce::Button ("Add module") { setTitle ("Add module"); }
        void paintButton (juce::Graphics& g, bool over, bool down) override;
    };

    void layoutCards();
    int slotIndexForX (int x) const;
    juce::Rectangle<int> cardBounds (int slot) const;

    CommaProcessor& proc;
    std::array<std::unique_ptr<SlotCard>, kNumModules> cards;
    std::array<std::unique_ptr<ModulePanel>, kNumModules> panels;
    std::array<LinkButton, kMaxSlots - 1> links;
    AddButton add;
    RackLayout shown;
    bool hasShown = false;
    int selected = -1;
    int dropIndex = -1, draggingModule = -1;
    juce::Rectangle<int> headArea, rowArea, detailArea;
};
} // namespace comma
