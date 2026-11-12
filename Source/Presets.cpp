#include "Presets.h"
#include "Params.h"
#include "PluginProcessor.h"

namespace comma
{
const std::vector<FactoryPreset>& factoryPresets()
{
    using namespace P;
    // модули: 0 Doubler, 1 Harmonizer, 2 Staircase, 3 Autotune, 4 Resonator, 5 Shifter, 6 Varispeed, 7 Spectral
    // строй: TunSource 0 — 12-EDO, 1 — N-EDO, 2 — Ratios, 3 — Scala, 4 — MTS-ESP; единицы: 0 — центы, 1 — отношение, 2 — шаги
    // деления задержки: 4 — 1/4, 5 — 1/8 D, 7 — 1/8; циклы: 2 — 1 bar, 4 — 1/4
    auto r = [] (int n, int d) { return (float) ratioIndex (n, d); };
    static const std::vector<FactoryPreset> list = {
        { "Init", {}, {}, {} },
        { "Micro Double", { 0 }, {}, { { DblCents, 9.0f }, { DblDelay, 8.0f }, { DblFocus, 150.0f } } },
        { "Wide Detune", { 0, 5 }, {}, { { DblCents, 18.0f }, { DblDelay, 14.0f }, { DblWindow, 45.0f }, { ShiShift, 0.7f }, { ShiSpread, 100.0f }, { ShiMix, 35.0f } } },
        { "19-EDO Stairs", { 2 }, {}, { { TunSource, 1.0f }, { TunEdo, 19.0f }, { StaUnit, 2.0f }, { StaSteps, 1.0f }, { StaDiv, 7.0f }, { StaFeedback, 82.0f }, { StaMix, 60.0f } } },
        { "Octave in Seven", { 2 }, {}, { { TunSource, 1.0f }, { TunEdo, 7.0f }, { StaUnit, 2.0f }, { StaSteps, 1.0f }, { StaDiv, 5.0f }, { StaFeedback, 86.0f }, { StaTone, 6000.0f }, { StaMix, 55.0f } } },
        { "Falling Stairs", { 2, 0 }, {}, { { StaUnit, 2.0f }, { StaSteps, -1.0f }, { StaDiv, 4.0f }, { StaFeedback, 75.0f }, { StaMix, 50.0f }, { DblCents, 6.0f }, { DblMix, 60.0f } } },
        { "Barber Pole", { 5 }, {}, { { ShiShift, 3.0f }, { ShiFeedback, 85.0f }, { ShiMix, 70.0f } } },
        { "Tempo Beats", { 5 }, {}, { { ShiSync, 1.0f }, { ShiDiv, 4.0f }, { ShiMix, 50.0f } } },
        { "Just Choir", { 1 }, {}, { { TunSource, 2.0f }, { Har1Unit, 1.0f }, { Har1Ratio, r (5, 4) }, { Har2Unit, 1.0f }, { Har2Ratio, r (3, 2) }, { Har2Level, 75.0f }, { HarMix, 55.0f } },
          "1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8 2/1" },
        { "Septimal Fifths", { 1, 0 }, {}, { { Har1Unit, 1.0f }, { Har1Ratio, r (7, 4) }, { Har1Level, 60.0f }, { Har2Unit, 1.0f }, { Har2Ratio, r (3, 2) }, { HarMix, 50.0f }, { DblCents, 7.0f }, { DblMix, 50.0f } } },
        { "Hard Tune", { 3 }, {}, { { AutSpeed, 0.0f }, { AutAmount, 100.0f } } },
        { "Glide Tune 31", { 3 }, {}, { { TunSource, 1.0f }, { TunEdo, 31.0f }, { AutSpeed, 80.0f } } },
        { "Ringing Strings", { 4 }, {}, { { ResDecay, 2500.0f }, { ResPartials, 8.0f }, { ResBright, 60.0f }, { ResMix, 70.0f }, { Snap, 60.0f }, { Glide, 120.0f } } },
        { "Scale Bells", { 4, 0 }, {}, { { TunSource, 1.0f }, { TunEdo, 17.0f }, { ResScale, 1.0f }, { ResStretch, 20.0f }, { ResDecay, 3500.0f }, { ResBright, 70.0f }, { ResMix, 80.0f },
                                         { DblCents, 5.0f }, { DblMix, 40.0f } } },
        { "MPE Glide Rack", { 4, 0 }, {}, { { Glide, 180.0f }, { Snap, 70.0f }, { SnapSpeed, 90.0f }, { Character, 35.0f }, { Overshoot, 18.0f }, { Vibrato, 12.0f },
                                            { ResDecay, 1800.0f }, { ResMix, 75.0f },
                                            { modSrc (0), (float) SrcPressure }, { modDst (0), (float) ResBright }, { modAmt (0), 60.0f },
                                            { modSrc (1), (float) SrcSlide }, { modDst (1), (float) DblCents }, { modAmt (1), 50.0f } } },
        { "Tape Wobble", { 6 }, {}, { { VarWow, 35.0f }, { VarFlutter, 6.0f } } },
        { "Tape Stop", { 6 }, {}, { { VarWow, 8.0f }, { VarFlutter, 2.0f }, { VarBrakeTime, 1200.0f } } },
        { "Pitch Dive", { 6 }, {}, { { VarRamp, -1200.0f }, { VarRampLen, 2.0f }, { VarWow, 10.0f } } },
        { "Sethares Snap", { 7 }, {}, { { TunSource, 1.0f }, { TunEdo, 10.0f }, { SpeMode, 0.0f }, { SpeAmount, 100.0f } } },
        { "Bohlen-Pierce Stretch", { 7, 2 }, {}, { { TunSource, 2.0f }, { SpeMode, 1.0f }, { SpeAmount, 100.0f }, { StaUnit, 2.0f }, { StaSteps, 1.0f }, { StaMix, 35.0f }, { StaFeedback, 60.0f } },
          "27/25 25/21 9/7 7/5 75/49 5/3 9/5 49/25 15/7 7/3 63/25 25/9 3/1" },
    };
    return list;
}

namespace
{
// назначение матрицы для шаблона: индекс в списке назначений по P::…
int dstIndexOf (int p)
{
    const auto& d = modDestinations();
    for (int i = 1; i < (int) d.size(); ++i) if (d[(size_t) i] == p) return i;
    return 0;
}
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, std::function<void()> cb)
    : apvts (s), onStateReplaced (std::move (cb)) {}

bool PresetManager::isSettingNotSound (const juce::String& id) { return id == ParamId::output; }

juce::String PresetManager::getCurrentName() const { return apvts.state.getProperty ("presetName", "Init").toString(); }
void PresetManager::setCurrentName (const juce::String& n) { apvts.state.setProperty ("presetName", n, nullptr); }

void PresetManager::loadFactory (int index)
{
    const auto& list = factoryPresets();
    if (index < 0 || index >= (int) list.size()) return;
    const auto& pr = list[(size_t) index];
    if (auto* um = apvts.undoManager) um->beginNewTransaction();
    // назначение матрицы в шаблоне записано номером параметра P::…, в ручке — номер в списке назначений
    std::vector<std::pair<int, float>> values = pr.values;
    for (auto& kv : values)
        if (kv.first >= P::ModFirst && kv.first <= P::ModLast && (kv.first - P::ModFirst) % 3 == 1)
            kv.second = (float) dstIndexOf ((int) kv.second);
    for (int p = 0; p < P::Count; ++p)
    {
        auto* rp = apvts.getParameter (spec (p).id);
        if (rp == nullptr || isSettingNotSound (spec (p).id)) continue;
        float v = rp->getDefaultValue();
        for (auto& kv : values)
            if (kv.first == p) v = rp->convertTo0to1 (kv.second);
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (v);
        rp->endChangeGesture();
    }
    if (pr.ratios.isNotEmpty())
        apvts.state.getOrCreateChildWithName ("Tuning", nullptr).setProperty ("ratios", pr.ratios, apvts.undoManager);
    RackLayout l;
    for (int m : pr.rack) l.add (m);
    for (int s : pr.parallel) if (s > 0 && s < l.count) l.parallel[(size_t) s] = true;
    CommaProcessor::writeLayoutToTree (apvts.state, l, apvts.undoManager);
    setCurrentName (pr.name);
    if (onStateReplaced) onStateReplaced();
}

juce::File PresetManager::userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Comma").getChildFile ("Presets");
}

juce::Array<juce::File> PresetManager::userPresets() const
{
    auto files = userFolder().findChildFiles (juce::File::findFiles, false, "*.comma");
    files.sort();
    return files;
}

bool PresetManager::loadUserTree (const juce::ValueTree& t)
{
    if (! t.hasType (apvts.state.getType())) return false;
    if (auto* um = apvts.undoManager) um->beginNewTransaction();
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (isSettingNotSound (rp->paramID)) continue;
            const auto node = t.getChildWithProperty ("id", rp->paramID);
            const float v = node.isValid() ? rp->convertTo0to1 ((float) node.getProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue())))
                                           : rp->getDefaultValue();
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (v);
            rp->endChangeGesture();
        }
    // свой пресет несёт и строй: отношения и файлы Scala
    const auto tun = t.getChildWithName ("Tuning");
    if (tun.isValid())
    {
        auto dst = apvts.state.getOrCreateChildWithName ("Tuning", nullptr);
        for (const char* key : { "ratios", "scl", "kbm", "sclName", "kbmName" })
            dst.setProperty (key, tun.getProperty (key, ""), apvts.undoManager);
    }
    CommaProcessor::writeLayoutToTree (apvts.state, CommaProcessor::layoutFromTree (t), apvts.undoManager);
    setCurrentName (t.getProperty ("presetName", "Preset").toString());
    if (onStateReplaced) onStateReplaced();
    return true;
}

bool PresetManager::loadUser (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr) return false;
    auto t = juce::ValueTree::fromXml (*xml);
    if (! t.isValid()) return false;
    t.setProperty ("presetName", f.getFileNameWithoutExtension(), nullptr);
    return loadUserTree (t);
}

bool PresetManager::save (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    auto dir = userFolder();
    if (! dir.createDirectory()) return false;
    auto state = apvts.copyState();
    state.setProperty ("presetName", clean, nullptr);
    auto xml = state.createXml();
    if (xml == nullptr || ! xml->writeTo (dir.getChildFile (clean + ".comma"))) return false;
    setCurrentName (clean);
    return true;
}

void PresetManager::step (int dir)
{
    const auto users = userPresets();
    const int nf = getNumFactory(), total = nf + users.size();
    if (total == 0) return;
    const auto cur = getCurrentName();
    int idx = -1;
    for (int i = 0; i < nf; ++i) if (cur == factoryPresets()[(size_t) i].name) idx = i;
    for (int i = 0; i < users.size(); ++i) if (cur == users[i].getFileNameWithoutExtension()) idx = nf + i;
    idx = idx < 0 ? (dir > 0 ? 0 : total - 1) : (idx + dir + total) % total;
    if (idx < nf) loadFactory (idx);
    else loadUser (users[idx - nf]);
}
} // namespace comma
