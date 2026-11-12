#pragma once
#include "Widgets.h"
#include "PluginProcessor.h"

namespace comma
{
// Нотный слой: Glide, Snap, характер скольжения, посадка, диапазон бенда
class NotesPanel : public juce::Component
{
public:
    explicit NotesPanel (CommaProcessor& p);
    void refresh();
    void resized() override;

private:
    CommaProcessor& proc;
    PillChoice glideMode, curve, domain, agg;
    ParamKnob glide { "Glide" }, rate { "Rate" }, snap { "Snap" }, snapSpeed { "Snap speed" }, character { "Character" },
              overshoot { "Overshoot" }, vibrato { "Vibrato" }, landing { "Decay" }, bend { "Bend" };
};

// Мод-матрица: 8 строк «источник MPE → ручка модуля, сила»
class MatrixPanel : public juce::Component
{
public:
    explicit MatrixPanel (CommaProcessor& p);
    void refresh();
    void resized() override;

private:
    struct DestPill : MenuPill
    {
        DestPill (CommaProcessor& p, int row);
        void open();
        CommaProcessor& proc;
        int row;
    };
    struct Row
    {
        Row (CommaProcessor& p, int i);
        PillChoice src;
        DestPill dst;
        juce::Slider amount;
        juce::Label value;
        std::unique_ptr<APVTS::SliderAttachment> att;
    };
    CommaProcessor& proc;
    std::vector<std::unique_ptr<Row>> rows;
};

// Главный вид: круг строя (ступени, высота входа, голоса), вкладки Pitch / Notes / Matrix
class MainView : public juce::Component
{
public:
    explicit MainView (CommaProcessor& p);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void tick();
    void setTab (int t);
    int getTab() const { return tab; }
    std::function<void()> onTuningClick;

private:
    void paintCircle (juce::Graphics& g, juce::Rectangle<float> area);
    void paintPitch (juce::Graphics& g, juce::Rectangle<int> area);

    CommaProcessor& proc;
    Segmented tabs;
    NotesPanel notes;
    MatrixPanel matrix;
    int tab = 0;
    juce::Rectangle<int> circleArea, rightArea, contentArea;
};
} // namespace comma
