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
    for (juce::Component* c : std::initializer_list<juce::Component*> { &logo, &prevPreset, &presetName, &nextPreset, &dice, &tuningChip, &inMeter, &outMeter, &settingsButton })
        content.addAndMakeVisible (c);
    tuningChip.setTitle ("Tuning");
    tuningChip.onOpen = [this] { showTuningMenu(); };
    logo.onClick = [this] { showAbout (true); };
    presetName.setTitle ("Preset");
    presetName.onClick = [this] { showPresetMenu(); };
    prevPreset.onClick = [this] { proc.presets.step (-1); };
    nextPreset.onClick = [this] { proc.presets.step (1); };
    settingsButton.onClick = [this] { showSettingsMenu(); };
    loadRandomizerOptions();
    dice.setDescription ("Randomizes the modules in the rack within safe limits. Volume, bypass, the tuning, the note layer and the set of modules "
                         "stay as they are. Choose what it may change in Settings, Randomizer.");
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
    footHint.setText ("Drag a slot to reorder. Notes come in over MIDI and MPE. Middle C is C3.", juce::dontSendNotification);
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
    m.addItem (13, "Load Scala file...");
    m.addItem (14, "Act as MTS-ESP master", true, proc.isMtsMaster());
    m.addItem (2000, "Save preset...");
    m.addSeparator();
    m.addItem (5, "About Comma");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton), [this] (int r)
    {
        if (r == 5) showAbout (true);
        else if (r == 10) { proc.undo.undo(); rackView.refresh(); }
        else if (r == 11) { proc.undo.redo(); rackView.refresh(); }
        else if (r == 12) rackView.openAddMenu();
        else if (r == 13) openScalaChooser (false);
        else if (r == 14)
        {
            if (! proc.setMtsMaster (! proc.isMtsMaster()))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "MTS-ESP",
                                                        "Comma can't become the MTS-ESP master: another master is running, or the MTS-ESP library is not installed. "
                                                        "Install it with any MTS-ESP master, for example the free MTS-ESP Mini.");
        }
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
// Строй
void CommaEditor::showTuningMenu()
{
    const int src = juce::roundToInt (proc.getParam (P::TunSource));
    const int edo = juce::roundToInt (proc.getParam (P::TunEdo));
    juce::PopupMenu m, edos, ratios, root, ref;
    m.addItem (1, "12-EDO", true, src == 0);
    for (int n : { 5, 7, 10, 15, 17, 19, 22, 24, 31, 41, 53, 72 })
        edos.addItem (100 + n, juce::String (n) + "-EDO", true, src == 1 && edo == n);
    edos.addSeparator();
    edos.addItem (99, "Other...", true, src == 1 && edo != 5 && edo != 7 && edo != 10 && edo != 15 && edo != 17 && edo != 19 && edo != 22
                                                 && edo != 24 && edo != 31 && edo != 41 && edo != 53 && edo != 72);
    m.addSubMenu ("N-EDO", edos, true, nullptr, src == 1);
    ratios.addItem (201, "5-limit just intonation");
    ratios.addItem (202, "7-limit just intonation");
    ratios.addItem (203, "Harmonics 8 to 16");
    ratios.addSeparator();
    ratios.addItem (200, "Edit ratios...");
    m.addSubMenu ("Ratios", ratios, true, nullptr, src == 2);
    m.addItem (3, "Load Scala .scl...", true, src == 3);
    m.addItem (4, "Load keyboard map .kbm...");
    const auto kbmName = proc.apvts.state.getChildWithName ("Tuning").getProperty ("kbmName").toString();
    m.addItem (5, "Clear .kbm" + (kbmName.isNotEmpty() ? " (" + kbmName + ")" : juce::String()), kbmName.isNotEmpty());
    m.addSeparator();
    m.addItem (6, proc.hasMtsMaster() || src == 4 ? "MTS-ESP: follow the master" : "MTS-ESP: no master found", true, src == 4);
    m.addSeparator();
    const int rt = juce::roundToInt (proc.getParam (P::TunRoot));
    const auto names = spec (P::TunRoot).choices;
    for (int i = 0; i < 12; ++i) root.addItem (300 + i, names[i], true, rt == i);
    m.addSubMenu ("Root", root);
    const float a4 = proc.getParam (P::TunRef);
    for (int hz : { 415, 432, 440, 442, 444 }) ref.addItem (400 + hz, "A4 = " + juce::String (hz) + " Hz", true, std::abs (a4 - (float) hz) < 0.01f);
    m.addSubMenu ("Reference", ref);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (tuningChip), [this] (int r)
    {
        juce::String err;
        if (r == 1) proc.setParam (P::TunSource, 0.0f);
        else if (r > 100 && r < 200) { proc.setParam (P::TunEdo, (float) (r - 100)); proc.setParam (P::TunSource, 1.0f); }
        else if (r == 99) askEdo();
        else if (r == 200) editRatios();
        else if (r == 201) proc.setRatios ("1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8 2/1", err);
        else if (r == 202) proc.setRatios ("1/1 15/14 8/7 7/6 5/4 4/3 7/5 3/2 8/5 5/3 7/4 15/8 2/1", err);
        else if (r == 203) proc.setRatios ("9/8 10/8 11/8 12/8 13/8 14/8 15/8 2/1", err);
        else if (r == 3) openScalaChooser (false);
        else if (r == 4) openScalaChooser (true);
        else if (r == 5) proc.clearKbm();
        else if (r == 6) proc.setParam (P::TunSource, 4.0f);
        else if (r >= 300 && r < 312) proc.setParam (P::TunRoot, (float) (r - 300));
        else if (r > 400 && r < 500) proc.setParam (P::TunRef, (float) (r - 400));
        proc.refreshTuning();
    });
}

void CommaEditor::openScalaChooser (bool kbm)
{
    chooser = std::make_unique<juce::FileChooser> (kbm ? "Load a keyboard map" : "Load a Scala scale",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), kbm ? "*.kbm" : "*.scl");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (! f.existsAsFile()) return;
        juce::String err;
        if (! proc.loadScalaFile (f, err))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Comma", err);
    });
}

void CommaEditor::editRatios()
{
    dialog = std::make_unique<juce::AlertWindow> ("Ratios", "Steps of the scale as ratios or cents, separated by spaces. The last one is the period, for example "
                                                            "1/1 9/8 5/4 4/3 3/2 5/3 15/8 2/1, or 3/2 7/4 11/8 2/1.", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("ratios", proc.getRatios(), "Ratios");
    dialog->addButton ("Apply", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && dialog != nullptr)
        {
            juce::String err;
            if (! proc.setRatios (dialog->getTextEditorContents ("ratios"), err))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Comma", err);
        }
        dialog.reset();
    }), false);
}

void CommaEditor::askEdo()
{
    dialog = std::make_unique<juce::AlertWindow> ("N-EDO", "Equal steps per octave, 1 to 96.", juce::MessageBoxIconType::NoIcon);
    dialog->addTextEditor ("n", juce::String (juce::roundToInt (proc.getParam (P::TunEdo))), "Steps");
    dialog->addButton ("Apply", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int r)
    {
        if (r == 1 && dialog != nullptr)
        {
            const int n = juce::jlimit (1, 96, dialog->getTextEditorContents ("n").getIntValue());
            proc.setParam (P::TunEdo, (float) n);
            proc.setParam (P::TunSource, 1.0f);
            proc.refreshTuning();
        }
        dialog.reset();
    }), false);
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
        tuningChip.setBounds (toolbarArea.withSizeKeepingCentre (300, 32));
    }
    r.removeFromTop (10);
    view.setBounds (r.removeFromTop (200));
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
    proc.analyseInput();
    view.tick();
    if (auto t = proc.getTuning()) tuningChip.setText (t->name + midDot() + t->detail);
    const auto err = proc.getTuningError();
    const auto hint = err.isNotEmpty() ? err : juce::String ("Drag a slot to reorder. Notes come in over MIDI and MPE. Middle C is C3.");
    if (footHint.getText() != hint) footHint.setText (hint, juce::dontSendNotification);
    footHint.setColour (juce::Label::textColourId, err.isNotEmpty() ? Colours::signal : Colours::ink2);
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
