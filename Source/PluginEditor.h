#pragma once
#include "PluginProcessor.h"
#include "Randomizer.h"
#include "UI/LookAndFeel.h"
#include "UI/Widgets.h"
#include "UI/MainView.h"
#include "UI/RackView.h"
#include "UI/About.h"

namespace comma
{
class CommaEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::DarkModeSettingListener
{
public:
    explicit CommaEditor (CommaProcessor&);
    ~CommaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& k) override;   // ⌘Z / ⇧⌘Z — undo / redo

    static constexpr int kBaseW = 1120, kBaseH = 760;

    // для тестов
    void setThemeForTest (ThemeId id) { setThemeMode (id == ThemeId::Rubber ? ThemeMode::Rubber : ThemeMode::Oxygen); }
    AboutPage& getAboutForTest() { return about; }
    RackView& getRackForTest() { return rackView; }
    void showAboutForTest (bool show, bool game) { showAbout (show); if (show) about.showGame (game); }
    void pumpForTest() { timerCallback(); }
    void rollDiceForTest() { dice.triggerClick(); }
    RandomizerOptions& getDiceOptionsForTest() { return diceOptions; }

private:
    void timerCallback() override;
    void darkModeSettingChanged() override;
    void showAbout (bool show);
    void showPresetMenu();
    void showSettingsMenu();
    void savePreset();
    void setThemeMode (ThemeMode m);
    void applyThemeMode();
    void loadRandomizerOptions();
    void setRandomizerGroup (RandomizerOptions::Group g, bool allowed);

    CommaProcessor& proc;
    CommaLookAndFeel lnf;
    ThemeMode themeMode = ThemeMode::System;
    juce::Component content;   // базовый размер, масштабируется трансформом

    // тулбар
    LogoButton logo;
    IconButton prevPreset { IconButton::Prev, "Previous preset" }, nextPreset { IconButton::Next, "Next preset" },
               settingsButton { IconButton::Settings, "Settings" }, dice { IconButton::Dice, "Randomize" };
    TextLinkButton presetName;
    InMeter inMeter;
    OutMeter outMeter;
    juce::Rectangle<int> toolbarArea;
    RandomizerOptions diceOptions;
    juce::Random rng;

    MainView view;
    juce::Rectangle<int> panelArea, footArea;
    RackView rackView;
    juce::Label status, footHint;

    AboutPage about;
    std::unique_ptr<juce::AlertWindow> dialog;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommaEditor)
};
} // namespace comma
