// Офлайн-тест Comma (этап 0): состав rack'а и его упаковка, математика rack'а на тестовых модулях
// (mix, bypass, параллельные группы, выравнивание задержки, смена состава без щелчка), процессор (проход без изменений,
// защита выхода, NaN, громкость, MIDI и MPE-каналы), состояние, undo, пресеты, кубик, частоты и размеры блоков,
// скорость, скриншоты интерфейса в обеих темах, About.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Randomizer.h"

using namespace comma;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "\n"; } } while (0)

namespace
{
double gRate = 48000.0;
constexpr int kBlock = 512;

// тестовые модули
struct GainModule : Module
{
    explicit GainModule (float g) : gain (g) {}
    void process (float* const* ch, int nc, int n, const ProcessContext&) override
    {
        for (int c = 0; c < nc; ++c) juce::FloatVectorOperations::multiply (ch[c], gain, n);
    }
    float gain;
};

struct DelayModule : Module
{
    explicit DelayModule (int l) { latency.store (l); }
    void prepare (double, int maxBlock) override { d.prepare (maxBlock); }
    void reset() override { d.clear(); }
    void process (float* const* ch, int nc, int n, const ProcessContext&) override { d.process (ch, ch, nc, n, latency.load()); }
    IntDelay d;
};

juce::AudioBuffer<float> sine (double seconds, double hz, float amp, int channels = 2)
{
    const int n = (int) (seconds * gRate);
    juce::AudioBuffer<float> b (channels, n);
    for (int c = 0; c < channels; ++c)
        for (int i = 0; i < n; ++i)
            b.setSample (c, i, amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (i + c * 7) / gRate));
    return b;
}

juce::AudioBuffer<float> runRack (Rack& r, const juce::AudioBuffer<float>& in, const Rack::Controls& c, int block = kBlock)
{
    juce::AudioBuffer<float> out (in);
    juce::AudioBuffer<float> buf (2, block);
    ProcessContext ctx;
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, in.getNumSamples() - pos);
        buf.setSize (2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom (ch, 0, in, ch, pos, n);
        r.process (buf, n, c, ctx);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
    }
    return out;
}

// наибольшее отличие out[i] от k · in[i − delay] на хвосте (после сглаживания)
float tailError (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, float k, int delay = 0, int from = 9600)
{
    float e = 0.0f;
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = juce::jmax (from, delay); i < out.getNumSamples(); ++i)
            e = juce::jmax (e, std::abs (out.getSample (ch, i) - k * in.getSample (ch, i - delay)));
    return e;
}

juce::AudioBuffer<float> runProc (CommaProcessor& p, const juce::AudioBuffer<float>& in, int block = kBlock, std::vector<std::pair<int, juce::MidiMessage>> midi = {},
                                  CommaEditor* ed = nullptr)
{
    juce::AudioBuffer<float> out (in);
    const int nc = in.getNumChannels();
    juce::AudioBuffer<float> buf (nc, block);
    size_t next = 0;
    int blocks = 0;
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, in.getNumSamples() - pos);
        buf.setSize (nc, n, false, false, true);
        for (int ch = 0; ch < nc; ++ch) buf.copyFrom (ch, 0, in, ch, pos, n);
        juce::MidiBuffer mb;
        while (next < midi.size() && midi[next].first < pos + n) { mb.addEvent (midi[next].second, juce::jmax (0, midi[next].first - pos)); ++next; }
        p.processBlock (buf, mb);
        for (int ch = 0; ch < nc; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
        if (ed != nullptr && ++blocks % 3 == 0) ed->pumpForTest();
    }
    return out;
}

bool allFinite (const juce::AudioBuffer<float>& b, float limit = 1.0f)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = b.getSample (ch, i);
            if (! std::isfinite (x) || std::abs (x) > limit + 1e-6f) return false;
        }
    return true;
}

void setParam (CommaProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

float getParam (CommaProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }

RackLayout makeLayout (std::initializer_list<int> mods, std::initializer_list<int> parallelSlots = {})
{
    RackLayout l;
    for (int m : mods) l.add (m);
    for (int s : parallelSlots) l.parallel[(size_t) s] = true;
    return l;
}

void fullRack (CommaProcessor& p)
{
    RackLayout l;
    for (int m = 0; m < kNumModules; ++m) l.add (m);
    p.setLayout (l);
}

void pumpMessages (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "render");
    outDir.createDirectory();

    // ---------------------------------------------------------------- состав rack'а
    std::cout << "layout\n";
    {
        juce::Random rng (7);
        for (int t = 0; t < 500; ++t)
        {
            RackLayout l;
            std::array<int, kNumModules> order { 0, 1, 2, 3, 4, 5, 6, 7 };
            for (int i = kNumModules - 1; i > 0; --i) std::swap (order[(size_t) i], order[(size_t) rng.nextInt (i + 1)]);
            const int n = rng.nextInt (kMaxSlots + 1);
            for (int i = 0; i < n; ++i) { l.add (order[(size_t) i]); l.parallel[(size_t) i] = i > 0 && rng.nextBool(); }
            const auto u = RackLayout::unpack (l.pack());
            CHECK (u == l && u.count == n, "pack / unpack roundtrip");
        }
        auto l = makeLayout ({ 2, 0, 5, 3 }, { 3 });        // 2 → 0 → (5 ‖ 3)
        CHECK (l.groupStart (3) == 2 && l.groupEnd (2) == 3 && l.groupEnd (0) == 0, "groups");
        CHECK (! l.add (2), "a module appears once");
        l.move (3, 0);   // 3 → 2 → 0 → 5
        CHECK (l.module[0] == 3 && l.module[1] == 2 && l.module[3] == 5 && ! l.parallel[0], "move");
        auto g = makeLayout ({ 1, 4, 6, 7 }, { 3 });      // 1 → 4 → (6 ‖ 7)
        g.remove (2);                                    // убрали начало группы: 7 остаётся отдельным слотом
        CHECK (g.count == 3 && g.module[2] == 7 && ! g.parallel[2], "remove group start");
        auto h = makeLayout ({ 1, 4, 6, 7 }, { 1, 2, 3 });
        h.remove (2);
        CHECK (h.count == 3 && h.parallel[1] && h.parallel[2], "remove inside a group keeps the group");
        RackLayout bad;
        bad.count = 4; bad.module = { 3, 3, 9, 1 }; bad.parallel = { true, false, false, true };
        bad.sanitise();
        CHECK (bad.count == 2 && bad.module[0] == 3 && bad.module[1] == 1 && ! bad.parallel[0] && bad.parallel[1], "sanitise");
    }

    // ---------------------------------------------------------------- математика rack'а
    std::cout << "rack\n";
    {
        Rack r;
        r.setModuleForTest (0, std::make_unique<GainModule> (0.0f));    // «тишина»
        r.setModuleForTest (1, std::make_unique<GainModule> (1.0f));    // как есть
        r.setModuleForTest (2, std::make_unique<DelayModule> (100));    // задержка 100 семплов
        r.setModuleForTest (3, std::make_unique<GainModule> (-1.0f));   // переворот фазы
        r.prepare (gRate, kBlock);
        const auto in = sine (0.5, 440.0, 0.5f);
        Rack::Controls c;
        c.on.fill (true);
        c.mix.fill (1.0f);

        auto check = [&] (const char* name, RackLayout l, float k, int delay, int expectLatency)
        {
            r.setLayout (l);
            r.reset();
            const auto out = runRack (r, in, c);
            const float e = tailError (out, in, k, delay);
            CHECK (e < 1e-4f, juce::String (name) + ": error " + juce::String (e));
            CHECK (r.getLatency() == expectLatency, juce::String (name) + ": latency " + juce::String (r.getLatency()));
        };
        check ("empty", {}, 1.0f, 0, 0);
        check ("silence", makeLayout ({ 0 }), 0.0f, 0, 0);
        c.mix[0] = 0.5f;
        check ("mix 50%", makeLayout ({ 0 }), 0.5f, 0, 0);
        c.mix[0] = 1.0f;
        c.on[0] = false;
        check ("bypass", makeLayout ({ 0 }), 1.0f, 0, 0);
        c.on[0] = true;
        check ("parallel average", makeLayout ({ 0, 1 }, { 1 }), 0.5f, 0, 0);
        check ("parallel opposite phase", makeLayout ({ 1, 3 }, { 1 }), 0.0f, 0, 0);
        c.on[1] = false;
        check ("parallel, one lane bypassed", makeLayout ({ 0, 1 }, { 1 }), 0.0f, 0, 0);
        c.on[0] = false;
        check ("parallel, all bypassed", makeLayout ({ 0, 1 }, { 1 }), 1.0f, 0, 0);
        c.on.fill (true);
        check ("latency", makeLayout ({ 2 }), 1.0f, 100, 100);
        check ("latency, aligned lanes", makeLayout ({ 2, 1 }, { 1 }), 1.0f, 100, 100);
        c.mix[2] = 0.3f;
        check ("latency, dry aligned with wet", makeLayout ({ 2 }), 1.0f, 100, 100);
        c.mix[2] = 1.0f;
        check ("serial: delay then gain", makeLayout ({ 2, 3 }), -1.0f, 100, 100);
        check ("two groups", makeLayout ({ 2, 1, 3, 0 }, { 1, 3 }), -0.5f, 100, 100);
        c.on[2] = false;
        check ("bypass keeps latency", makeLayout ({ 2 }), 1.0f, 100, 100);
        c.on[2] = true;

        // смена состава посреди звука: без NaN и без скачков громче входа
        r.setLayout (makeLayout ({ 1 }));
        r.reset();
        juce::AudioBuffer<float> buf (2, kBlock);
        ProcessContext ctx;
        float peak = 0.0f, prev = 0.0f, jump = 0.0f;
        for (int b = 0; b < 40; ++b)
        {
            if (b == 10) r.setLayout (makeLayout ({ 3, 1 }));
            if (b == 11) r.setLayout (makeLayout ({ 1, 3 }, { 1 }));
            if (b == 25) r.setLayout ({});
            for (int i = 0; i < kBlock; ++i) buf.setSample (0, i, in.getSample (0, (b * kBlock + i) % in.getNumSamples())), buf.setSample (1, i, buf.getSample (0, i));
            r.process (buf, kBlock, c, ctx);
            for (int i = 0; i < kBlock; ++i)
            {
                const float y = buf.getSample (0, i);
                CHECK (std::isfinite (y), "switch: finite");
                peak = juce::jmax (peak, std::abs (y));
                jump = juce::jmax (jump, std::abs (y - prev));
                prev = y;
            }
        }
        CHECK (peak <= 0.5f + 1e-4f, "switch: not louder than input");
        CHECK (jump < 0.1f, "switch: no click, jump " + juce::String (jump));
    }

    // ---------------------------------------------------------------- процессор
    std::cout << "processor\n";
    {
        CommaProcessor p;
        p.prepareToPlay (gRate, kBlock);
        CHECK (p.getLayout().count == 0 && p.presets.getCurrentName() == "Init", "starts with an empty rack");
        CHECK (p.getLatencySamples() == 0, "latency 0");
        const auto in = sine (0.5, 220.0, 0.5f);
        CHECK (tailError (runProc (p, in), in, 1.0f, 0, 0) < 1e-6f, "empty rack passes audio unchanged");
        fullRack (p);
        CHECK (p.getLayout().count == kNumModules, "all 8 modules");
        runProc (p, in);
        CHECK (tailError (runProc (p, in), in, 1.0f, 0, 0) < 1e-5f, "stage 0 modules pass audio unchanged");

        // ‖ и mix на модулях-заглушках ничего не меняют
        p.setParallel (3, true);
        setParam (p, moduleParam (1, "mix"), 40.0f);
        runProc (p, in);
        CHECK (tailError (runProc (p, in), in, 1.0f, 0, 0) < 1e-5f, "parallel stubs pass audio unchanged");

        // защита выхода и NaN
        const auto hot = sine (0.3, 220.0, 2.0f);
        CHECK (allFinite (runProc (p, hot)), "output ≤ 0 dBFS");
        auto nan = sine (0.1, 220.0, 0.5f);
        nan.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
        nan.setSample (1, 2000, std::numeric_limits<float>::infinity());
        CHECK (allFinite (runProc (p, nan)), "NaN and inf do not reach the output");
        CHECK (tailError (runProc (p, in), in, 1.0f, 0, 0) < 1e-5f, "recovers after NaN");

        setParam (p, ParamId::output, -6.0f);
        runProc (p, in);
        CHECK (tailError (runProc (p, in), in, juce::Decibels::decibelsToGain (-6.0f), 0, 0) < 1e-4f, "output volume");
        setParam (p, ParamId::output, 0.0f);

        // MIDI и MPE: одна и та же нота на двух member channels — две ноты
        runProc (p, sine (0.05, 220.0, 0.1f), kBlock, { { 0, juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100) }, { 10, juce::MidiMessage::noteOn (3, 60, (juce::uint8) 90) },
                                                         { 20, juce::MidiMessage::pitchWheel (2, 12000) } });
        CHECK (p.midiNotes.load() == 2, "MPE: two notes on two channels");
        runProc (p, sine (0.05, 220.0, 0.1f), kBlock, { { 0, juce::MidiMessage::noteOff (2, 60) }, { 5, juce::MidiMessage::noteOff (3, 60) } });
        CHECK (p.midiNotes.load() == 0, "MPE: notes released");

        // моно
        CommaProcessor mono;
        juce::AudioProcessor::BusesLayout bl;
        bl.inputBuses.add (juce::AudioChannelSet::mono());
        bl.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (mono.setBusesLayout (bl), "mono layout");
        mono.prepareToPlay (gRate, kBlock);
        fullRack (mono);
        const auto m1 = sine (0.3, 220.0, 0.5f, 1);
        runProc (mono, m1);
        CHECK (tailError (runProc (mono, m1), m1, 1.0f, 0, 0) < 1e-5f, "mono");
        CHECK (allFinite (runProc (mono, m1)), "mono: finite");
    }

    // ---------------------------------------------------------------- состояние, undo, пресеты
    std::cout << "state\n";
    {
        CommaProcessor a;
        a.setLayout (makeLayout ({ 2, 0, 5 }, { 1 }));
        setParam (a, moduleParam (0, "mix"), 33.0f);
        setParam (a, moduleParam (5, "on"), 0.0f);
        setParam (a, ParamId::output, -3.0f);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        CommaProcessor b;
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        CHECK (b.getLayout() == a.getLayout(), "state: rack");
        CHECK (std::abs (getParam (b, moduleParam (0, "mix")) - 33.0f) < 0.05f, "state: mix");
        CHECK (getParam (b, moduleParam (5, "on")) < 0.5f, "state: bypass");
        CHECK (std::abs (getParam (b, ParamId::output) + 3.0f) < 0.05f, "state: output");
        CHECK (! b.undo.canUndo(), "state: undo history cleared");

        // мусор и старое состояние без узла Rack
        const char junk[] = "not a state at all";
        b.setStateInformation (junk, (int) sizeof (junk));
        CHECK (b.getLayout() == a.getLayout(), "junk state is ignored");
        juce::ValueTree t ("Comma");
        juce::MemoryBlock old;
        if (auto xml = t.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, old);
        b.setStateInformation (old.getData(), (int) old.getSize());
        CHECK (b.getLayout().count == 0, "state without a rack: empty rack");

        // undo / redo
        CommaProcessor u;
        u.addModule (1);
        u.addModule (4);
        u.moveSlot (1, 0);
        CHECK (u.getLayout().module[0] == 4, "move");
        u.undo.undo();
        CHECK (u.getLayout().module[0] == 1 && u.getLayout().count == 2, "undo move");
        u.undo.undo();
        CHECK (u.getLayout().count == 1, "undo add");
        u.undo.redo();
        CHECK (u.getLayout().count == 2, "redo add");
        u.undo.beginNewTransaction();
        setParam (u, moduleParam (4, "mix"), 20.0f);
        pumpMessages (200);   // APVTS пишет значение в дерево по таймеру
        u.undo.beginNewTransaction();
        u.undo.undo();
        pumpMessages (100);
        CHECK (std::abs (getParam (u, moduleParam (4, "mix")) - 100.0f) < 0.05f, "undo a knob");

        // пресеты: свой пресет несёт rack, громкость не трогает
        CommaProcessor pr;
        setParam (pr, ParamId::output, -9.0f);
        auto tree = a.apvts.copyState();
        tree.setProperty ("presetName", "Mine", nullptr);
        CHECK (pr.presets.loadUserTree (tree), "user preset");
        CHECK (pr.getLayout() == a.getLayout(), "user preset: rack");
        CHECK (std::abs (getParam (pr, ParamId::output) + 9.0f) < 0.05f, "user preset keeps Output");
        CHECK (pr.presets.getCurrentName() == "Mine", "user preset name");
        pr.presets.loadFactory (0);
        CHECK (pr.getLayout().count == 0 && std::abs (getParam (pr, ParamId::output) + 9.0f) < 0.05f, "Init: empty rack, Output kept");
        CHECK (getParam (pr, moduleParam (0, "mix")) > 99.0f, "Init resets knobs");
    }

    // ---------------------------------------------------------------- кубик
    std::cout << "dice\n";
    {
        CommaProcessor p;
        p.prepareToPlay (gRate, kBlock);
        fullRack (p);
        setParam (p, ParamId::output, -2.0f);
        setParam (p, moduleParam (6, "on"), 0.0f);
        juce::Random rng (2026);
        RandomizerOptions all;
        all.enabled.fill (true);
        const auto in = sine (0.1, 330.0, 0.7f);
        for (int roll = 0; roll < 40; ++roll)
        {
            randomizePatch (p, rng, all);
            const auto l = p.getLayout();
            CHECK (l.count == kNumModules, "dice keeps the set of modules");
            for (int m = 0; m < kNumModules; ++m)
            {
                CHECK (getParam (p, moduleParam (m, "mix")) >= 40.0f - 0.05f, "dice: mix ≥ 40%");
                CHECK ((getParam (p, moduleParam (m, "on")) > 0.5f) == (m != 6), "dice never touches bypass");
            }
            CHECK (std::abs (getParam (p, ParamId::output) + 2.0f) < 0.05f, "dice never touches Output");
            const auto out = runProc (p, in);
            CHECK (allFinite (out), "dice: finite, ≤ 0 dBFS");
            float pk = 0.0f;
            for (int i = 2400; i < out.getNumSamples(); ++i) pk = juce::jmax (pk, std::abs (out.getSample (0, i)));
            CHECK (pk > 0.01f, "dice: not silent");
        }
        RandomizerOptions none;
        none.enabled.fill (false);
        const auto before = p.getLayout();
        const float mixBefore = getParam (p, moduleParam (2, "mix"));
        randomizePatch (p, rng, none);
        CHECK (p.getLayout() == before && std::abs (getParam (p, moduleParam (2, "mix")) - mixBefore) < 1e-3f, "forbidden groups stay");
        RandomizerOptions defaults;
        randomizePatch (p, rng, defaults);
        CHECK (p.getLayout() == before, "order is off by default");
    }

    // ---------------------------------------------------------------- частоты и блоки
    std::cout << "rates\n";
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 64, 333, 4096 })
        {
            gRate = rate;
            CommaProcessor p;
            p.prepareToPlay (rate, block);
            fullRack (p);
            p.setParallel (2, true);
            const auto in = sine (0.4, 440.0, 0.5f);
            runProc (p, in, block);
            const auto out = runProc (p, in, block);
            CHECK (tailError (out, in, 1.0f, 0, 0) < 1e-5f, "rate " + juce::String (rate) + " block " + juce::String (block));
        }
    gRate = 48000.0;

    // ---------------------------------------------------------------- скорость
    std::cout << "speed\n";
    {
        CommaProcessor p;
        p.prepareToPlay (gRate, kBlock);
        fullRack (p);
        const auto in = sine (10.0, 440.0, 0.5f);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        runProc (p, in);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  full rack: 10 s of audio in " << juce::String (ms, 1) << " ms\n";
        CHECK (ms < 2000.0, "speed");
    }

    // ---------------------------------------------------------------- интерфейс
    std::cout << "ui\n";
    {
        CommaProcessor p;
        p.prepareToPlay (gRate, kBlock);
        std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
        auto* ed = dynamic_cast<CommaEditor*> (base.get());
        CHECK (ed != nullptr, "editor");
        ed->setSize (CommaEditor::kBaseW, CommaEditor::kBaseH);

        auto shoot = [&] (const juce::String& name)
        {
            ed->pumpForTest();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            auto f = outDir.getChildFile (name + ".png");
            f.deleteFile();
            juce::FileOutputStream fos (f);
            juce::PNGImageFormat().writeImageToStream (img, fos);
            std::cout << "  wrote " << f.getFileName() << "\n";
        };
        const auto in = sine (0.3, 220.0, 0.4f);

        for (auto theme : { ThemeId::Oxygen, ThemeId::Rubber })
        {
            const juce::String tn = theme == ThemeId::Oxygen ? "oxygen" : "rubber";
            ed->setThemeForTest (theme);
            p.presets.loadFactory (0);
            runProc (p, in, kBlock, {}, ed);
            shoot ("ui_" + tn + "_empty");

            // rack: Doubler → Staircase → (Shifter ‖ Resonator) → Varispeed, Resonator выключен
            p.setLayout (makeLayout ({ 0, 2, 5, 4, 6 }, { 3 }));
            setParam (p, moduleParam (4, "on"), 0.0f);
            setParam (p, moduleParam (2, "mix"), 45.0f);
            ed->getRackForTest().selectModule (5);
            runProc (p, in, kBlock, { { 0, juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100) } }, ed);
            shoot ("ui_" + tn + "_rack");

            // перетаскивание: Varispeed в начало
            ed->getRackForTest().dragForTest (4, 0);
            CHECK (p.getLayout().module[0] == 6 && p.getLayout().count == 5, "drag a card to the front");
            p.setLayout (makeLayout ({ 0, 1, 2, 3, 4, 5, 6, 7 }, { 1, 2, 5 }));
            setParam (p, moduleParam (4, "on"), 1.0f);
            ed->getRackForTest().selectModule (1);
            runProc (p, in, kBlock, { { 0, juce::MidiMessage::noteOff (2, 60) } }, ed);
            shoot ("ui_" + tn + "_full");

            // кубик из интерфейса
            ed->rollDiceForTest();
            runProc (p, in, kBlock, {}, ed);

            ed->showAboutForTest (true, false);
            shoot ("about_" + tn);
            {
                auto& fog = ed->getAboutForTest().getFogForTest();
                CHECK (fog.getNumParticles() > 60, "fog: too few particles");
                const auto c = fog.getLocalBounds().getCentre().toFloat();
                fog.blowForTest ({ c.x - 160.0f, c.y }, { c.x + 40.0f, c.y + 10.0f }, 12);
                CHECK (fog.isAnimating(), "fog: did not start");
                fog.stepForTest (90);
                CHECK (! fog.isAnimating(), "fog: did not settle");
            }
            ed->showAboutForTest (true, true);
            pumpMessages (300);
            shoot ("game_" + tn);
            auto& about = ed->getAboutForTest();
            about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (about.isVisible(), "escape in game closes only the game");
            about.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey));
            CHECK (! about.isVisible(), "escape closes About");
        }
        ed->setThemeForTest (ThemeId::Oxygen);
        base.reset();
    }

    std::cout << (failures == 0 ? "ALL OK\n" : juce::String (failures) + " FAILURE(S)\n");
    return failures == 0 ? 0 : 1;
}
