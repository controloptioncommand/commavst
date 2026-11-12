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
      presets (apvts, [this] { syncLayoutFromState(); tuningDirty = true; refreshTuning(); updateLatencies(); })
{
    for (int p = 0; p < P::Count; ++p) raw[(size_t) p] = apvts.getRawParameterValue (spec (p).id);
    // статические таблицы создаются здесь, а не при первом обращении из аудиопотока
    modDestinations();
    ratioNames();
    apvts.state.addListener (this);
    presets.loadFactory (0);
    undo.clearUndoHistory();
    refreshTuning (true);
    updateLatencies();
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
    maxBlock = juce::jmax (1, samplesPerBlock);
    refreshTuning();
    updateLatencies();
    rack.setLayout (layout);
    rack.prepare (sampleRate, maxBlock);
    notes.prepare (sampleRate, juce::jmax (maxBlock, 4096));
    outGain.reset (sampleRate, 0.05);
    outGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (raw[P::Output]->load()));
    uiYin.prepare (sampleRate, 50.0f, 2000.0f);
    uiYinReady = true;
    inFifo.reset();
    setLatencySamples (rack.latencyFor (layout));
}

void CommaProcessor::setParam (int p, float value)
{
    if (auto* prm = apvts.getParameter (spec (p).id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 (value));
        prm->endChangeGesture();
    }
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
    setParam (P::moduleOn (m), 1.0f);
    setParam (P::moduleMix (m), 100.0f);
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

// ---------------------------------------------------------------------------
// Строй
juce::ValueTree CommaProcessor::tuningTree() { return apvts.state.getOrCreateChildWithName ("Tuning", nullptr); }

juce::String CommaProcessor::getRatios() const
{
    const auto t = apvts.state.getChildWithName ("Tuning");
    const auto r = t.getProperty ("ratios").toString();
    return r.isNotEmpty() ? r : defaultRatios();
}

void CommaProcessor::refreshTuning (bool force)
{
    TuningSpec s;
    s.source = (TuningSource) juce::jlimit (0, 4, juce::roundToInt (raw[P::TunSource]->load()));
    s.edo = juce::roundToInt (raw[P::TunEdo]->load());
    s.root = juce::roundToInt (raw[P::TunRoot]->load());
    s.refHz = raw[P::TunRef]->load();
    const auto t = apvts.state.getChildWithName ("Tuning");
    s.ratios = t.getProperty ("ratios").toString();
    s.scl = t.getProperty ("scl").toString();
    s.kbm = t.getProperty ("kbm").toString();
    s.sclName = t.getProperty ("sclName").toString();
    s.kbmName = t.getProperty ("kbmName").toString();

    // MTS-ESP: снимок пересобирается, когда мастер сменил строй
    if (s.source == TuningSource::MTS)
    {
        std::array<double, 128> hz {};
        juce::String name;
        double period = 1200.0;
        const bool master = mts.hasMaster();
        const bool changed = master && mts.readTuning (hz, name, period);
        if (master && (changed || force || ! builtOnce || builtSpec.source != TuningSource::MTS || name != builtMtsName))
        {
            tuningHolder.publish (buildTuningFromNoteHz (hz, name, period));
            tuningError = {};
            builtSpec = s;
            builtMtsName = name;
            builtOnce = true;
            tuningDirty = false;
            return;
        }
        if (master) return;
        if (builtOnce && builtSpec.source == TuningSource::MTS && tuningError.isNotEmpty() && ! force) return;
        juce::String err;
        tuningHolder.publish (buildTuning (s, err));   // без мастера — 12-EDO
        tuningError = "No MTS-ESP master found: using 12-EDO.";
        builtSpec = s;
        builtOnce = true;
        return;
    }

    const bool same = builtOnce && ! force && ! tuningDirty && builtSpec.source == s.source && builtSpec.edo == s.edo && builtSpec.root == s.root
                   && std::abs (builtSpec.refHz - s.refHz) < 1.0e-6 && builtSpec.ratios == s.ratios && builtSpec.scl == s.scl && builtSpec.kbm == s.kbm;
    if (same) return;
    juce::String err;
    auto snap = buildTuning (s, err);
    tuningError = err;
    tuningHolder.publish (snap);
    builtSpec = s;
    builtOnce = true;
    tuningDirty = false;
    if (mts.isMaster() && snap != nullptr) mts.pushTuning (*snap);
}

bool CommaProcessor::loadScalaFile (const juce::File& f, juce::String& error)
{
    const auto text = f.loadFileAsString();
    if (text.isEmpty()) { error = "Can't read " + f.getFileName() + "."; return false; }
    TuningSpec test;
    test.source = TuningSource::Scala;
    auto t = tuningTree();
    const bool isKbm = f.hasFileExtension ("kbm");
    test.scl = isKbm ? t.getProperty ("scl").toString() : text;
    test.kbm = isKbm ? text : t.getProperty ("kbm").toString();
    if (test.scl.isEmpty()) test.scl = "! 12-EDO\n12-EDO\n 12\n!\n 100.\n 200.\n 300.\n 400.\n 500.\n 600.\n 700.\n 800.\n 900.\n 1000.\n 1100.\n 2/1\n";
    juce::String err;
    buildTuning (test, err);
    if (err.isNotEmpty()) { error = err; return false; }
    undo.beginNewTransaction();
    if (isKbm) { t.setProperty ("kbm", text, &undo); t.setProperty ("kbmName", f.getFileNameWithoutExtension(), &undo); }
    else
    {
        t.setProperty ("scl", text, &undo);
        t.setProperty ("sclName", f.getFileNameWithoutExtension(), &undo);
        if (t.getProperty ("kbm").toString().isEmpty()) t.setProperty ("kbmName", "", &undo);
    }
    if (t.getProperty ("scl").toString().isEmpty()) t.setProperty ("scl", test.scl, &undo);
    setParam (P::TunSource, (float) TuningSource::Scala);
    refreshTuning (true);
    return true;
}

void CommaProcessor::clearKbm()
{
    auto t = tuningTree();
    undo.beginNewTransaction();
    t.setProperty ("kbm", "", &undo);
    t.setProperty ("kbmName", "", &undo);
    refreshTuning (true);
}

bool CommaProcessor::setRatios (const juce::String& text, juce::String& error)
{
    if (ratiosToScl (text, error).isEmpty()) return false;
    undo.beginNewTransaction();
    tuningTree().setProperty ("ratios", text.trim(), &undo);
    setParam (P::TunSource, (float) TuningSource::Ratios);
    refreshTuning (true);
    return true;
}

bool CommaProcessor::setMtsMaster (bool on)
{
    if (! mts.setMaster (on)) return false;
    tuningTree().setProperty ("mtsMaster", on, nullptr);
    if (on) if (auto t = getTuning()) mts.pushTuning (*t);
    return true;
}

void CommaProcessor::updateLatencies()
{
    std::array<float, P::Count> v {};
    for (int p = 0; p < P::Count; ++p) v[(size_t) p] = raw[(size_t) p]->load();
    for (int m = 0; m < kNumModules; ++m)
    {
        auto& mod = rack.getModule (m);
        mod.setLatency (mod.computeLatency (v.data(), sr));
    }
    const int lat = rack.latencyFor (layout);
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

void CommaProcessor::timerCallback()
{
    refreshTuning();
    updateLatencies();
}

// ---------------------------------------------------------------------------
void CommaProcessor::analyseInput()
{
    if (! uiYinReady) return;
    int s1, n1, s2, n2;
    const int ready = inFifo.getNumReady();
    if (ready <= 0) return;
    inFifo.prepareToRead (ready, s1, n1, s2, n2);
    bool any = false;
    auto feed = [&] (int start, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            uiYin.push (inFifoData[(size_t) (start + i)]);
            if (uiYin.justAnalysed()) any = true;
        }
    };
    feed (s1, n1);
    feed (s2, n2);
    inFifo.finishedRead (n1 + n2);
    if (! any) return;
    inputHz = uiYin.isVoiced() && inPeak.load() > 0.003f ? uiYin.getHz() : 0.0f;
    float dev = std::numeric_limits<float>::quiet_NaN();
    if (inputHz > 0.0f)
        if (auto t = getTuning())
        {
            const double c = hzToCents (inputHz);
            dev = (float) (c - t->nearest (c));
        }
    centsHistory[(size_t) historyPos] = dev;
    historyPos = (historyPos + 1) % kHistory;
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
    const TuningSnapshot* tuning = tuningHolder.get();

    // счёт нот (по каналу и ноте — с учётом MPE member channels)
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int key = juce::jlimit (0, 16 * 128 - 1, (m.getChannel() - 1) * 128 + m.getNoteNumber());
        if (m.isNoteOn()) { if (heldCount[(size_t) key] < 255) ++heldCount[(size_t) key]; }
        else if (m.isNoteOff() && heldCount[(size_t) key] > 0) --heldCount[(size_t) key];
        else if (m.isAllNotesOff() || m.isAllSoundOff()) heldCount.fill (0);
    }
    int count = 0;
    for (auto c : heldCount) count += c;
    midiNotes.store (count);

    // нотный слой
    NoteSettings ns;
    ns.glideMs = raw[P::Glide]->load();
    ns.glideMode = juce::roundToInt (raw[P::GlideMode]->load());
    ns.glideRate = raw[P::GlideRate]->load();
    ns.glideCurve = juce::roundToInt (raw[P::GlideCurve]->load());
    ns.glideDomain = juce::roundToInt (raw[P::GlideDomain]->load());
    ns.snap = raw[P::Snap]->load() * 0.01;
    ns.snapMs = raw[P::SnapSpeed]->load();
    ns.character = raw[P::Character]->load() * 0.01;
    ns.overshoot = raw[P::Overshoot]->load();
    ns.vibrato = raw[P::Vibrato]->load();
    ns.landingMs = raw[P::Landing]->load();
    ns.bendRange = juce::roundToInt (raw[P::BendRange]->load());
    notes.process (midi, n, ns, tuning);
    mpeMembers.store (notes.memberChannels());
    for (int v = 0; v < kMaxVoices; ++v)
    {
        const auto& vo = notes.voice (v);
        auto& vv = voiceView[(size_t) v];
        vv.active.store (vo.active ? 1 : 0); vv.held.store (vo.held ? 1 : 0); vv.note.store (vo.note);
        vv.pitch.store ((float) vo.pitch); vv.pressure.store (vo.pressure); vv.slide.store (vo.slide); vv.bend.store (vo.bend); vv.strike.store (vo.strike);
    }

    // значения ручек и мод-матрица: эффекты без голосов получают сведённое MPE, Resonator — на каждый голос
    const auto& dests = modDestinations();
    const int agg = juce::roundToInt (raw[P::MpeAgg]->load());
    for (int p = 0; p < P::Count; ++p)
    {
        const float r = raw[(size_t) p]->load();
        values[(size_t) p] = r;
        if (spec (p).modulatable) matrix.norm[(size_t) p] = spec (p).range.convertTo0to1 (r);
        offsets[(size_t) p] = 0.0f;
    }
    matrix.bendRange = ns.bendRange;
    for (int r = 0; r < kNumModRows; ++r)
    {
        auto& row = matrix.rows[(size_t) r];
        row.src = juce::roundToInt (values[(size_t) P::modSrc (r)]);
        const int di = juce::roundToInt (values[(size_t) P::modDst (r)]);
        row.dst = di > 0 && di < (int) dests.size() ? dests[(size_t) di] : -1;
        row.amt = values[(size_t) P::modAmt (r)] * 0.01f;
        if (row.dst >= 0 && row.src != SrcNone && std::abs (row.amt) > 1.0e-6f)
            offsets[(size_t) row.dst] += row.amt * notes.aggregate (row.src, agg, ns.bendRange);
    }
    for (int r = 0; r < kNumModRows; ++r)
    {
        const int d = matrix.rows[(size_t) r].dst;
        if (d >= 0 && std::abs (offsets[(size_t) d]) > 1.0e-9f)
            values[(size_t) d] = spec (d).range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, matrix.norm[(size_t) d] + offsets[(size_t) d]));
    }

    // вход: уровень и копия для анализатора высоты
    float inPk = 0.0f;
    const int chs = juce::jmin (2, buffer.getNumChannels());
    for (int ch = 0; ch < juce::jmin (numIn, chs); ++ch) inPk = juce::jmax (inPk, buffer.getMagnitude (ch, 0, n));
    inPeak.store (juce::jmax (inPk, inPeak.load() * 0.9f));
    {
        int s1, n1, s2, n2;
        inFifo.prepareToWrite (n, s1, n1, s2, n2);
        auto put = [&] (int start, int cnt, int off)
        {
            for (int i = 0; i < cnt; ++i)
            {
                float x = buffer.getSample (0, off + i);
                if (chs > 1) x = 0.5f * (x + buffer.getSample (1, off + i));
                inFifoData[(size_t) (start + i)] = x;
            }
        };
        put (s1, n1, 0);
        put (s2, n2, n1);
        inFifo.finishedWrite (n1 + n2);
    }

    Rack::Controls c;
    for (int m = 0; m < kNumModules; ++m)
    {
        c.mix[(size_t) m] = values[(size_t) P::moduleMix (m)] * 0.01f;
        c.on[(size_t) m] = values[(size_t) P::moduleOn (m)] > 0.5f;
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
    ctx.tuning = tuning;
    ctx.values = values.data();
    ctx.notes = &notes;
    ctx.matrix = &matrix;
    rack.process (buffer, n, c, ctx);

    // выход: громкость, защита от NaN и мягкий предел 0 dBFS
    outGain.setTargetValue (juce::Decibels::decibelsToGain (raw[P::Output]->load()));
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
            const bool wantMaster = (bool) apvts.state.getChildWithName ("Tuning").getProperty ("mtsMaster", false);
            if (wantMaster != mts.isMaster()) mts.setMaster (wantMaster);
            refreshTuning (true);
            updateLatencies();
        }
}

juce::AudioProcessorEditor* CommaProcessor::createEditor() { return new CommaEditor (*this); }
} // namespace comma

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new comma::CommaProcessor(); }
