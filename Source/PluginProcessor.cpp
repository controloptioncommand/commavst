#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace comma
{
namespace
{
// Защита выхода: ниже 0.98 сигнал не трогается, выше — мягко упирается в 1.0 (0 dBFS)
inline float guard (float x) noexcept
{
    constexpr float k = 0.98f, r = 1.0f - k;
    const float a = std::abs (x);
    if (a <= k) return x;
    return std::copysign (k + r * std::tanh ((a - k) / r), x);
}
}

CommaProcessor::CommaProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "Comma", createLayout()),
      presets (apvts, [this] { syncLayoutFromState(); })
{
    for (int m = 0; m < kNumModules; ++m)
    {
        mixParam[(size_t) m] = apvts.getRawParameterValue (moduleParam (m, "mix"));
        onParam[(size_t) m] = apvts.getRawParameterValue (moduleParam (m, "on"));
    }
    outputParam = apvts.getRawParameterValue (ParamId::output);
    apvts.state.addListener (this);
    presets.loadFactory (0);
    undo.clearUndoHistory();
    startTimerHz (10);
}

CommaProcessor::~CommaProcessor()
{
    stopTimer();
    apvts.state.removeListener (this);
}

bool CommaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == out;
}

void CommaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    rack.setLayout (layout);
    rack.prepare (sampleRate, juce::jmax (1, samplesPerBlock));
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    setLatencySamples (rack.latencyFor (layout));
}

// ---------------------------------------------------------------------------
// Rack в состоянии
RackLayout CommaProcessor::layoutFromTree (const juce::ValueTree& state)
{
    RackLayout l;
    const auto r = state.getChildWithName ("Rack");
    for (int i = 0; i < r.getNumChildren() && l.count < kMaxSlots; ++i)
    {
        const auto s = r.getChild (i);
        const int m = moduleFromKey (s.getProperty ("module").toString());
        if (m < 0) continue;
        l.module[(size_t) l.count] = m;
        l.parallel[(size_t) l.count] = (bool) s.getProperty ("parallel", false);
        ++l.count;
    }
    l.sanitise();
    return l;
}

void CommaProcessor::writeLayoutToTree (juce::ValueTree& state, const RackLayout& l, juce::UndoManager* um)
{
    auto r = state.getOrCreateChildWithName ("Rack", um);
    if (layoutFromTree (state) == l) return;
    r.removeAllChildren (um);
    for (int i = 0; i < l.count; ++i)
    {
        juce::ValueTree s ("Slot");
        s.setProperty ("module", moduleInfo (l.module[(size_t) i]).key, nullptr);
        s.setProperty ("parallel", l.isParallel (i), nullptr);
        r.appendChild (s, um);
    }
}

void CommaProcessor::syncLayoutFromState()
{
    const auto l = layoutFromTree (apvts.state);
    layout = l;
    rack.setLayout (l);
    const int lat = rack.latencyFor (l);
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

void CommaProcessor::setLayout (const RackLayout& l)
{
    auto copy = l;
    copy.sanitise();
    undo.beginNewTransaction();
    writeLayoutToTree (apvts.state, copy, &undo);
    syncLayoutFromState();
}

bool CommaProcessor::addModule (int m)
{
    auto l = layout;
    if (! l.add (m)) return false;
    setLayout (l);
    // новый модуль приходит включённым и на полном Mix
    for (const char* what : { "on", "mix" })
        if (auto* p = apvts.getParameter (moduleParam (m, what)))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    return true;
}

void CommaProcessor::removeSlot (int slot) { auto l = layout; l.remove (slot); setLayout (l); }
void CommaProcessor::moveSlot (int from, int to) { auto l = layout; l.move (from, to); setLayout (l); }

void CommaProcessor::setParallel (int slot, bool p)
{
    if (slot <= 0 || slot >= layout.count) return;
    auto l = layout;
    l.parallel[(size_t) slot] = p;
    setLayout (l);
}

void CommaProcessor::timerCallback()
{
    // задержка модуля может смениться (режим «низкая задержка» / «качество»)
    const int lat = rack.latencyFor (layout);
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

// ---------------------------------------------------------------------------
void CommaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels(), numOut = getTotalNumOutputChannels();
    for (int ch = numIn; ch < numOut; ++ch) buffer.clear (ch, 0, n);
    if (n == 0) return;

    // MIDI: пока только счёт нот (по каналу и ноте — с учётом MPE member channels)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int key = juce::jlimit (0, 16 * 128 - 1, (m.getChannel() - 1) * 128 + m.getNoteNumber());
        if (m.isNoteOn()) { if (heldCount[(size_t) key] < 255) ++heldCount[(size_t) key]; }
        else if (m.isNoteOff() && heldCount[(size_t) key] > 0) --heldCount[(size_t) key];
        else if (m.isAllNotesOff() || m.isAllSoundOff()) heldCount.fill (0);
    }
    int notes = 0;
    for (auto c : heldCount) notes += c;
    midiNotes.store (notes);

    float inPk = 0.0f;
    for (int ch = 0; ch < juce::jmin (numIn, buffer.getNumChannels()); ++ch) inPk = juce::jmax (inPk, buffer.getMagnitude (ch, 0, n));
    inPeak.store (juce::jmax (inPk, inPeak.load() * 0.9f));

    Rack::Controls c;
    for (int m = 0; m < kNumModules; ++m)
    {
        c.mix[(size_t) m] = mixParam[(size_t) m]->load() * 0.01f;
        c.on[(size_t) m] = onParam[(size_t) m]->load() > 0.5f;
    }
    ProcessContext ctx;
    ctx.sampleRate = sr;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) ctx.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) ctx.ppq = *ppq;
            ctx.playing = pos->getIsPlaying();
        }
    rack.process (buffer, n, c, ctx);

    // выход: громкость, защита от NaN и мягкий предел 0 dBFS
    outGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    const int chs = juce::jmin (2, buffer.getNumChannels());
    bool bad = false;
    float outPk = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float g = outGain.getNextValue();
        for (int ch = 0; ch < chs; ++ch)
        {
            float& x = buffer.getWritePointer (ch)[i];
            if (! std::isfinite (x)) { x = 0.0f; bad = true; }
            x = guard (x * g);
            outPk = juce::jmax (outPk, std::abs (x));
        }
    }
    if (bad) rack.reset();
    outPeak.store (juce::jmax (outPk, outPeak.load() * 0.9f));

    const double secs = (double) (juce::Time::getHighResolutionTicks() - t0) / (double) juce::Time::getHighResolutionTicksPerSecond();
    cpuLoad.store (cpuLoad.load() * 0.9f + 0.1f * (float) (secs / ((double) n / sr)));
}

// ---------------------------------------------------------------------------
void CommaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void CommaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            syncLayoutFromState();
            undo.clearUndoHistory();
        }
}

juce::AudioProcessorEditor* CommaProcessor::createEditor() { return new CommaEditor (*this); }
} // namespace comma

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new comma::CommaProcessor(); }
