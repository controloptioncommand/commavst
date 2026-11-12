#include "Presets.h"
#include "Params.h"
#include "PluginProcessor.h"

namespace comma
{
const std::vector<FactoryPreset>& factoryPresets()
{
    // Этап 0: модули ещё не звучат, поэтому шаблон один — пустой rack. Шаблоны со звуком появятся вместе с модулями.
    static const std::vector<FactoryPreset> list = {
        { "Init", {}, {}, {} },
    };
    return list;
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
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            if (isSettingNotSound (rp->paramID)) continue;
            float v = rp->getDefaultValue();
            for (auto& kv : pr.values)
                if (rp->paramID == kv.first) v = rp->convertTo0to1 (kv.second);
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (v);
            rp->endChangeGesture();
        }
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
