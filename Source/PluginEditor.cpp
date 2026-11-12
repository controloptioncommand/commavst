#include "PluginEditor.h"

namespace comma
{
using LnF = CommaLookAndFeel;

namespace
{
std::unique_ptr<juce::PropertiesFile> openPrefs()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Comma";
    o.filenameSuffix = "settings";
    o.folderName = "Comma";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile> (o);
}
} // namespace

CommaEditor::CommaEditor (CommaProcessor& p)
    : AudioProcessorEditor (&p), proc (p), outMeter (p.apvts), view (p), rackView (p)
{
    Accessibility::refresh();
    auto prefs = openPrefs();
    themeMode = (ThemeMode) juce::jlimit (0, 2, prefs->getIntValue ("themeMode", 0));
    applyThemeMode();
    setLookAndFeel (&lnf);
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    setTitle ("Comma");
    addAndMakeVisible (content);

    // тулбар
    for (juce::Component* c : std::initializer_list<juce::Component*> { &logo, &prevPreset, &presetName, &nextPreset, &dice, &inMeter, &outMeter, &settingsButton })
        content.addAndMakeVisible (c);
    logo.onClick = [this] { showAbout (true); };
    presetName.setTitle ("Preset");
    presetName.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { proc.presets.step (-1); };
    nextPreset.onClick = [this] { proc.presets.step (1); };
    settingsButton.onClick = [this] { showSettingsMenu(); };
    loadRandomizerOptions();
    dice.setDescription ("Randomizes the modules in the rack within safe limits. Volume, bypass and the set of modules stay as they are. "
                         "Choose what it may change in Settings, Randomizer.");
    dice.onClick = [this] { randomizePatch (proc, rng, diceOptions); };

    content.addAndMakeVisible (view);
    content.addAndMakeVisible (rackView);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &status, &footHint })
        content.addAndMakeVisible (c);
    status.setFont (LnF::mono (LnF::Type::footnote));
    status.getProperties().set ("dim", true);
    status.setJustificationType (juce::Justification::centredRight);
    footHint.setFont (LnF::font (LnF::Type::callout));
    footHint.setJustificationType (juce::Justification::centredLeft);
    footHint.setText ("Drag a slot to reorder. Notes come in over MIDI and MPE.", juce::dontSendNotification);
    footHint.setColour (juce::Label::textColourId, Colours::ink2);

    content.addChildComponent (about);
    about.onClose = [this] { showAbout (false); };
    disableClickFocus (*this);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (kBaseW / 2, kBaseH / 2, kBaseW * 2, kBaseH * 2);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kBaseW / kBaseH);
    const int scale = juce::jlimit (50, 200, prefs->getIntValue ("scale", 100));
    setSize (kBaseW * scale / 100, kBaseH * scale / 100);
    timerCallback();
    startTimerHz (30);
}

CommaEditor::~CommaEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
    setLookAndFeel (nullptr);
}

// ---------------------------------------------------------------------------
void CommaEditor::applyThemeMode()
{
    ThemeId id = ThemeId::Oxygen;
    if (themeMode == ThemeMode::Rubber) id = ThemeId::Rubber;
    else if (themeMode == ThemeMode::System) id = juce::Desktop::getInstance().isDarkModeActive() ? ThemeId::Rubber : ThemeId::Oxygen;
    applyTheme (id);
    lnf.refreshColours();
    sendLookAndFeelChange();
    footHint.setColour (juce::Label::textColourId, Colours::ink2);
    repaintTree (*this);
}

void CommaEditor::setThemeMode (ThemeMode m)
{
    themeMode = m;
    if (auto prefs = openPrefs()) { prefs->setValue ("themeMode", (int) m); prefs->saveIfNeeded(); }
    applyThemeMode();
}

void CommaEditor::darkModeSettingChanged()
{
    if (themeMode == ThemeMode::System) applyThemeMode();
}

void CommaEditor::showAbout (bool show)
{
    if (show == about.isVisible()) return;
    if (show)
    {
        about.setBounds (0, 0, kBaseW, kBaseH);
        about.setBackLogoBounds (logo.getBounds());
        about.setVisible (true);
        about.toFront (true);
        about.grabKeyboardFocus();
    }
    else
    {
        about.showGame (false);
        about.setVisible (false);
    }
}

bool CommaEditor::keyPressed (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() && k.getKeyCode() == 'Z')
    {
        if (k.getModifiers().isShiftDown()) proc.undo.redo();
        else proc.undo.undo();
        rackView.refresh();
        return true;
    }
    return false;
}

void CommaEditor::loadRandomizerOptions()
{
    auto prefs = openPrefs();
    for (int gi = 0; gi < RandomizerOptions::NumGroups; ++gi)
    {
        const auto grp = (RandomizerOptions::Group) gi;
        diceOptions.enabled[(size_t) gi] = prefs->getBoolValue (RandomizerOptions::prefKey (grp), RandomizerOptions::defaultFor (grp));
    }
}

void CommaEditor::setRandomizerGroup (RandomizerOptions::Group grp, bool allowed)
{
    diceOptions.enabled[(size_t) grp] = allowed;
    if (auto prefs = openPrefs()) { prefs->setValue (RandomizerOptions::prefKey (grp), allowed); prefs->saveIfNeeded(); }
}

// ---------------------------------------------------------------------------
void CommaEditor::showPresetMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader ("Factory templates");
    const auto& fp = factoryPresets();
    const auto cur = proc.presets.getCurrentName();
    for (int i = 0; i < (int) fp.size(); ++i) m.addItem (1 + i, fp[(size_t) i].name, true, cur == fp[(size_t) i].name);
    const auto users = proc.presets.userPresets();
    if (! users.isEmpty())
    {
        m.addSeparator();
        m.addSectionHeader ("User");
        for (int i = 0; i < users.size(); ++i)
            m.addItem (1000 + i, users[i].getFileNameWithoutExtension(), true, cur == users[i].getFileNameWithoutExtension());
    }
    m.addSeparator();
    m.addItem (2000, "Save preset...");
    m.addItem (2001, "Show presets folder");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetName), [this, users] (int r)
    {
        if (r >= 1 && r < 1000) proc.presets.loadFactory (r - 1);
        else if (r >= 1000 && r < 2000 && r - 1000 < users.size()) proc.presets.loadUser (users[r - 1000]);
        else if (r == 2000) savePreset();
        else if (r == 2001)
        {
            auto dir = PresetManager::userFolder();
            dir.createDirectory();
            dir.startAsProcess();
        }
    });
}

void CommaEditor::savePreset()
{
    dialog = std::make_unique<juce::AlertWindow> ("Save preset", "The rack and all its settings are saved together.", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("name", proc.presets.getCurrentName(), "Name");
    dialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && dialog != nullptr)
            if (! proc.presets.save (dialog->getTextEditorContents ("name")))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Comma", "Could not save preset.");
        dialog.reset();
    }), false);
}

void CommaEditor::showSettingsMenu()
{
    juce::PopupMenu m, appearance, sizeMenu, dicePrefs;
    appearance.addItem (201, "Match system", true, themeMode == ThemeMode::System);
    appearance.addSeparator();
    appearance.addItem (202, "Oxygen (light)", true, themeMode == ThemeMode::Oxygen);
    appearance.addItem (203, "Rubber (dark)", true, themeMode == ThemeMode::Rubber);
    m.addSubMenu ("Appearance", appearance);
    for (int pct : { 75, 100, 125, 150 })
        sizeMenu.addItem (100 + pct, juce::String (pct) + "%", true, std::abs (getWidth() - kBaseW * pct / 100) < 4);
    m.addSubMenu ("Size", sizeMenu);
    dicePrefs.addSectionHeader ("The dice may change");
    for (int gi = 0; gi < RandomizerOptions::NumGroups; ++gi)
        dicePrefs.addItem (300 + gi, RandomizerOptions::name ((RandomizerOptions::Group) gi), true, diceOptions.allows ((RandomizerOptions::Group) gi));
    m.addSubMenu ("Randomizer", dicePrefs);
    m.addSeparator();
    m.addItem (10, "Undo", proc.undo.canUndo());
    m.addItem (11, "Redo", proc.undo.canRedo());
    m.addItem (12, "Add module...", proc.getLayout().count < kMaxSlots);
    m.addItem (2000, "Save preset...");
    m.addSeparator();
    m.addItem (5, "About Comma");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton), [this] (int r)
    {
        if (r == 5) showAbout (true);
        else if (r == 10) { proc.undo.undo(); rackView.refresh(); }
        else if (r == 11) { proc.undo.redo(); rackView.refresh(); }
        else if (r == 12) rackView.openAddMenu();
        else if (r == 2000) savePreset();
        else if (r == 201) setThemeMode (ThemeMode::System);
        else if (r == 202) setThemeMode (ThemeMode::Oxygen);
        else if (r == 203) setThemeMode (ThemeMode::Rubber);
        else if (r >= 300 && r < 300 + RandomizerOptions::NumGroups)
        {
            const auto grp = (RandomizerOptions::Group) (r - 300);
            setRandomizerGroup (grp, ! diceOptions.allows (grp));
        }
        else if (r > 100 && r < 200)
        {
            const int pct = r - 100;
            if (auto prefs = openPrefs()) { prefs->setValue ("scale", pct); prefs->saveIfNeeded(); }
            setSize (kBaseW * pct / 100, kBaseH * pct / 100);
        }
    });
}

// ---------------------------------------------------------------------------
void CommaEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::window);
    g.addTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));

    LnF::drawPlate (g, toolbarArea.toFloat(), 12.0f);
    g.setColour (Colours::outline);
    g.drawVerticalLine (logo.getRight() + 8, (float) toolbarArea.getY() + 15, (float) toolbarArea.getBottom() - 15);

    g.setColour (Colours::surface);
    g.fillRoundedRectangle (panelArea.toFloat(), 12.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (panelArea.toFloat().reduced (0.5f), 12.0f, 1.0f);
    g.drawHorizontalLine (footArea.getY(), (float) footArea.getX(), (float) footArea.getRight());
}

void CommaEditor::resized()
{
    content.setBounds (0, 0, kBaseW, kBaseH);
    content.setTransform (juce::AffineTransform::scale ((float) getWidth() / (float) kBaseW));
    about.setBounds (0, 0, kBaseW, kBaseH);

    auto r = juce::Rectangle<int> (0, 0, kBaseW, kBaseH).reduced (14);
    toolbarArea = r.removeFromTop (52);
    {
        auto h = toolbarArea.reduced (10, 10);
        const auto logoSize = LnF::pixelTextSize ("Comma", 1);
        logo.setBounds (h.removeFromLeft (juce::jmax (64, logoSize.x + 8)));
        h.removeFromLeft (18);
        prevPreset.setBounds (h.removeFromLeft (32));
        presetName.setBounds (h.removeFromLeft (150));
        nextPreset.setBounds (h.removeFromLeft (32));
        h.removeFromLeft (6);
        dice.setBounds (h.removeFromLeft (32));
        settingsButton.setBounds (h.removeFromRight (32));
        h.removeFromRight (10);
        outMeter.setBounds (h.removeFromRight (140));
        h.removeFromRight (14);
        inMeter.setBounds (h.removeFromRight (118));
    }
    r.removeFromTop (10);
    view.setBounds (r.removeFromTop (250));
    r.removeFromTop (10);
    panelArea = r;

    auto inner = panelArea.reduced (18, 14);
    footArea = inner.removeFromBottom (34);
    rackView.setBounds (inner.withTrimmedBottom (6));
    auto b = footArea.withTrimmedTop (8);
    status.setBounds (b.removeFromRight (380));
    footHint.setBounds (b);
}

void CommaEditor::timerCallback()
{
    if (about.isVisible()) return;
    // правки, сделанные за одно движение мыши, — один шаг undo
    if (! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown()) proc.undo.beginNewTransaction();
    rackView.refresh();
    outMeter.setLevel (proc.outPeak.load());
    inMeter.setLevel (proc.inPeak.load());
    presetName.setButtonText (proc.presets.getCurrentName());
    presetName.setDescription ("Preset " + proc.presets.getCurrentName());

    const int notes = proc.midiNotes.load();
    status.setText ("MIDI " + juce::String (notes) + (notes == 1 ? " note" : " notes") + midDot()
                    + "Latency " + juce::String (proc.getReportedLatency()) + " smp"
                    + "   CPU " + juce::String (juce::roundToInt (proc.cpuLoad.load() * 100.0f)) + "%", juce::dontSendNotification);
}
} // namespace comma
