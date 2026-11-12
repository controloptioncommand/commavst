// Офлайн-тест Comma: строй (Scala .scl/.kbm, EDO, отношения, единицы интервалов, MTS-ESP без мастера), питч-шифтеры
// по БПФ (±10 центов), модули (Doubler, Staircase, Shifter, Harmonizer, Autotune, Resonator, Varispeed, Spectral),
// нотный слой MPE (MCM, бенд, Glide, Snap, характер, посадка), мод-матрица, rack (mix, bypass, параллель, задержка),
// процессор, состояние, undo, пресеты, кубик, частоты и блоки, скорость, скриншоты интерфейса в обеих темах, About.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Randomizer.h"
#include "Rack/Modules.h"

using namespace comma;

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  FAIL: " << msg << "\n"; } } while (0)
#define NEAR(a, b, tol, msg) do { const double va_ = (a), vb_ = (b); if (! (std::abs (va_ - vb_) <= (tol))) { ++failures; \
    std::cout << "  FAIL: " << msg << ": " << va_ << " vs " << vb_ << "\n"; } } while (0)

namespace
{
double gRate = 48000.0;
constexpr int kBlock = 512;
using Midi = std::vector<std::pair<int, juce::MidiMessage>>;

// тестовые модули для математики rack'а
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

juce::AudioBuffer<float> noise (double seconds, float amp, int seed = 1)
{
    juce::Random rng (seed);
    juce::AudioBuffer<float> b (2, (int) (seconds * gRate));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float x = amp * (rng.nextFloat() * 2.0f - 1.0f);
        b.setSample (0, i, x);
        b.setSample (1, i, x);
    }
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

// наибольшее отличие out[i] от k · in[i − delay] на хвосте
float tailError (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in, float k, int delay = 0, int from = 9600)
{
    float e = 0.0f;
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        for (int i = juce::jmax (from, delay); i < out.getNumSamples(); ++i)
            e = juce::jmax (e, std::abs (out.getSample (ch, i) - k * in.getSample (ch, i - delay)));
    return e;
}

struct TestPlayHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0;
    bool playing = false;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo i;
        i.setPpqPosition (ppq);
        i.setBpm (bpm);
        i.setIsPlaying (playing);
        return i;
    }
};

juce::AudioBuffer<float> runProc (CommaProcessor& p, const juce::AudioBuffer<float>& in, int block = kBlock, Midi midi = {}, CommaEditor* ed = nullptr,
                                  TestPlayHead* ph = nullptr)
{
    juce::AudioBuffer<float> out (in);
    const int nc = in.getNumChannels();
    juce::AudioBuffer<float> buf (nc, block);
    size_t next = 0;
    int blocks = 0;
    std::sort (midi.begin(), midi.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    for (int pos = 0; pos < in.getNumSamples(); pos += block)
    {
        const int n = juce::jmin (block, in.getNumSamples() - pos);
        buf.setSize (nc, n, false, false, true);
        for (int ch = 0; ch < nc; ++ch) buf.copyFrom (ch, 0, in, ch, pos, n);
        juce::MidiBuffer mb;
        while (next < midi.size() && midi[next].first < pos + n) { mb.addEvent (midi[next].second, juce::jmax (0, midi[next].first - pos)); ++next; }
        p.processBlock (buf, mb);
        for (int ch = 0; ch < nc; ++ch) out.copyFrom (ch, pos, buf, ch, 0, n);
        if (ph != nullptr && ph->playing) ph->ppq += n * ph->bpm / 60.0 / gRate;
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

float rms (const juce::AudioBuffer<float>& b, int ch, int start, int len)
{
    double s = 0.0;
    len = juce::jmin (len, b.getNumSamples() - start);
    for (int i = start; i < start + len; ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return (float) std::sqrt (s / juce::jmax (1, len));
}

// пик спектра: Ханн, БПФ на 65536 с дополнением нулями, параболическая интерполяция по логарифму амплитуды
struct Spectrum
{
    std::vector<float> mag;
    double binHz = 1.0;
};

Spectrum spectrum (const juce::AudioBuffer<float>& b, int ch, int start, int len)
{
    constexpr int order = 16, N = 1 << order;
    len = juce::jmin (len, juce::jmin (N, b.getNumSamples() - start));
    std::vector<float> d ((size_t) N * 2, 0.0f);
    for (int i = 0; i < len; ++i)
        d[(size_t) i] = b.getSample (ch, start + i) * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) (len - 1)));
    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (d.data(), true);
    Spectrum s;
    s.mag.assign (d.begin(), d.begin() + N / 2);
    s.binHz = gRate / N;
    return s;
}

double peakHz (const juce::AudioBuffer<float>& b, int ch, int start, int len, double lo = 30.0, double hi = 15000.0)
{
    const auto s = spectrum (b, ch, start, len);
    int best = (int) (lo / s.binHz);
    for (int k = (int) (lo / s.binHz); k < juce::jmin ((int) s.mag.size() - 1, (int) (hi / s.binHz)); ++k)
        if (s.mag[(size_t) k] > s.mag[(size_t) best]) best = k;
    const double a = std::log (s.mag[(size_t) best - 1] + 1e-12), m = std::log (s.mag[(size_t) best] + 1e-12), c = std::log (s.mag[(size_t) best + 1] + 1e-12);
    const double off = 0.5 * (a - c) / (a - 2.0 * m + c);
    return (best + (std::isfinite (off) ? off : 0.0)) * s.binHz;
}

double bandDb (const Spectrum& s, double hz)
{
    const int k = (int) std::round (hz / s.binHz);
    float m = 0.0f;
    for (int i = k - 4; i <= k + 4; ++i) if (i > 0 && i < (int) s.mag.size()) m = juce::jmax (m, s.mag[(size_t) i]);
    return juce::Decibels::gainToDecibels (m, -200.0f);
}

// высота по YIN (для больших сдвигов линии задержки: перекрёстное затухание головок даёт боковые полосы,
// и пик спектра уходит с ноты, хотя слышимая высота верная)
double yinHz (const juce::AudioBuffer<float>& b, int ch, int start, int len)
{
    dsp::YinTracker y;
    y.prepare (gRate, 50.0f, 2000.0f);
    std::vector<double> v;
    for (int i = start; i < juce::jmin (b.getNumSamples(), start + len); ++i)
    {
        y.push (b.getSample (ch, i));
        if (y.justAnalysed() && y.isVoiced() && i > start + 4800) v.push_back (y.getHz());
    }
    if (v.empty()) return 0.0;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];   // медиана
}

void setP (CommaProcessor& p, int prm, float v)
{
    auto* x = p.apvts.getParameter (spec (prm).id);
    x->setValueNotifyingHost (x->convertTo0to1 (v));
}

float getP (CommaProcessor& p, int prm) { return p.getParam (prm); }

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

// процессор с одним модулем: состав, параметры, задержки
void only (CommaProcessor& p, int module)
{
    p.presets.loadFactory (0);
    p.setLayout (makeLayout ({ module }));
    p.updateLatencies();
}

int bendFor (double semis, int range = 48) { return juce::jlimit (0, 16383, 8192 + (int) std::lround (semis / range * 8191.0)); }

void pumpMessages (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

void nlRun (NoteLayer& nl, const NoteSettings& s, const TuningSnapshot* t, double seconds, juce::MidiBuffer first = {}, std::vector<double>* trace = nullptr)
{
    const int block = 480;
    const int blocks = (int) std::round (seconds * gRate / block);
    for (int b = 0; b < blocks; ++b)
    {
        nl.process (b == 0 ? first : juce::MidiBuffer(), block, s, t);
        if (trace != nullptr) trace->push_back (nl.voice (nl.lastVoice() >= 0 ? nl.lastVoice() : 0).pitch);
    }
}

juce::MidiBuffer mb (std::initializer_list<juce::MidiMessage> msgs)
{
    juce::MidiBuffer b;
    for (auto& m : msgs) b.addEvent (m, 0);
    return b;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "render");
    outDir.createDirectory();

    // ================================================================ строй
    std::cout << "tuning\n";
    {
        juce::String err;
        TuningSpec s12;
        auto t12 = buildTuning (s12, err);
        CHECK (t12 != nullptr && err.isEmpty(), "12-EDO builds");
        NEAR (t12->noteHz (69), 440.0, 1e-6, "12-EDO A4");
        NEAR (t12->noteHz (60), 261.6255653, 1e-5, "12-EDO C3");
        NEAR (t12->periodCents, 1200.0, 1e-9, "12-EDO period");
        CHECK (t12->degrees == 12 && t12->degreeNames[9] == "A", "12-EDO degrees and names");
        NEAR (t12->nearest (6049.0), 6000.0, 1e-3, "nearest step");
        NEAR (t12->shiftSteps (6030.0, 2), 6230.0, 1e-3, "shift by steps keeps the offset");
        CHECK (t12->degreeOf (6900.0) == 9, "degree of A");

        TuningSpec s19;
        s19.source = TuningSource::EDO;
        s19.edo = 19;
        auto t19 = buildTuning (s19, err);
        NEAR (t19->noteHz (60), 261.6255653, 1e-3, "19-EDO keeps C3 on the root");
        NEAR (t19->noteHz (61) / t19->noteHz (60), std::exp2 (1.0 / 19.0), 1e-9, "19-EDO step");
        NEAR (t19->noteHz (79) / t19->noteHz (60), 2.0, 1e-9, "19-EDO octave after 19 keys");
        NEAR (t19->meanStep, 1200.0 / 19.0, 1e-9, "19-EDO mean step");
        s19.root = 9;   // корень на A: 440 Гц
        auto t19a = buildTuning (s19, err);
        NEAR (t19a->noteHz (69), 440.0, 1e-4, "19-EDO rooted on A");

        TuningSpec sj;
        sj.source = TuningSource::Ratios;
        sj.ratios = "1/1 9/8 5/4 4/3 3/2 5/3 15/8 2/1";
        auto tj = buildTuning (sj, err);
        CHECK (err.isEmpty() && tj->degrees == 7, "ratios: 7 steps");
        NEAR (tj->noteHz (62) / tj->noteHz (60), 1.25, 1e-9, "ratios: 5/4 on the third key");
        NEAR (tj->noteHz (64) / tj->noteHz (60), 1.5, 1e-9, "ratios: 3/2");
        NEAR (tj->noteHz (67) / tj->noteHz (60), 2.0, 1e-9, "ratios: period 2/1");
        CHECK (tj->degreeNames[2] == "5/4", "ratio names");
        // единицы интервала
        NEAR (centsToRatio (701.955), 1.5, 1e-5, "cents to ratio");
        NEAR (ratioToCents (7.0 / 4.0), 968.826, 1e-3, "ratio to cents");
        NEAR (ratioToCents (11.0 / 8.0), 551.318, 1e-3, "11/8 in cents");
        NEAR (intervalCents (IntervalUnit::Ratio, 0.0, ratioIndex (3, 2), 0, tj.get()), 701.955, 1e-3, "ratio unit");
        NEAR (intervalCents (IntervalUnit::Ratio, 0.0, ratioIndex (2, 3), 0, tj.get()), -701.955, 1e-3, "ratio unit below");
        NEAR (intervalCents (IntervalUnit::Cents, 12.5, 0, 0, tj.get()), 12.5, 1e-9, "cents unit");
        NEAR (intervalCents (IntervalUnit::Steps, 0.0, 0, 1, tj.get(), tj->rootCents), 203.91, 1e-2, "steps from a known pitch are real steps");
        NEAR (intervalCents (IntervalUnit::Steps, 0.0, 0, 2, tj.get(), tj->rootCents), 386.31, 1e-2, "two real steps");
        NEAR (intervalCents (IntervalUnit::Steps, 0.0, 0, 1, tj.get()), 1200.0 / 7.0, 1e-6, "steps without a pitch use the mean step");
        NEAR (intervalCents (IntervalUnit::Steps, 0.0, 0, 1, t19.get()), 1200.0 / 19.0, 1e-9, "one step of 19-EDO");
        CHECK (ratioNames()[unisonRatioIndex()] == "1/1", "unison in the ratio list");

        // ошибки ввода отношений
        juce::String e1, e2;
        CHECK (ratiosToScl ("abc", e1).isEmpty() && e1.isNotEmpty(), "bad ratios are rejected");
        CHECK (ratiosToScl ("1/1", e2).isEmpty() && e2.isNotEmpty(), "empty scale is rejected");
        juce::String e3;
        CHECK (ratiosToScl ("701.955, 3/2 2", e3).isNotEmpty() && e3.isEmpty(), "cents, ratios and integers");

        // Scala .scl + .kbm
        TuningSpec ss;
        ss.source = TuningSource::Scala;
        ss.scl = "! test.scl\n!\nMeantone fifths\n 5\n!\n 193.157\n 386.314\n 3/2\n 1082.893\n 2/1\n";
        ss.kbm = "! test.kbm\n12\n0\n127\n60\n69\n432.0\n5\n! mapping\n0\n1\n2\nx\nx\n3\nx\nx\nx\n4\nx\nx\n";
        auto ts = buildTuning (ss, err);
        CHECK (err.isEmpty(), "Scala parses: " + err);
        NEAR (ts->noteHz (69), 432.0, 1e-3, "kbm reference note 69 = 432 Hz");
        NEAR (ts->periodCents, 1200.0, 1e-6, "scl period");
        CHECK (ts->degrees == 5, "scl: 5 steps");
        NEAR (ts->degreeCents[3], 701.955, 1e-3, "scl ratio line 3/2");
        NEAR (ts->noteHz (61) / ts->noteHz (60), centsToRatio (193.157), 1e-6, "kbm maps key 61 to degree 1");
        TuningSpec bad;
        bad.source = TuningSource::Scala;
        bad.scl = "this is not a scale";
        juce::String badErr;
        auto tb = buildTuning (bad, badErr);
        CHECK (badErr.isNotEmpty() && tb != nullptr && tb->degrees == 12, "broken .scl: error and 12-EDO");
        // Болен — Пирс: период 3/1
        TuningSpec bp;
        bp.source = TuningSource::Ratios;
        bp.ratios = "27/25 25/21 9/7 7/5 75/49 5/3 9/5 49/25 15/7 7/3 63/25 25/9 3/1";
        auto tbp = buildTuning (bp, err);
        NEAR (tbp->periodCents, 1901.955, 1e-3, "Bohlen-Pierce period");
        CHECK (tbp->degrees == 13, "Bohlen-Pierce: 13 steps");

        // процессор: MTS-ESP без мастера и загрузка .scl из файла
        CommaProcessor p;
        setP (p, P::TunSource, 4.0f);
        p.refreshTuning (true);
        CHECK (p.getTuning()->degrees == 12 && p.getTuningError().contains ("MTS-ESP"), "MTS-ESP without a master: 12-EDO and a note");
        CHECK (! p.hasMtsMaster(), "no MTS-ESP master in the test");
        const auto scl = outDir.getChildFile ("test.scl");
        scl.replaceWithText (ss.scl);
        juce::String le;
        CHECK (p.loadScalaFile (scl, le), "load .scl: " + le);
        CHECK (juce::roundToInt (getP (p, P::TunSource)) == 3 && p.getTuning()->degrees == 5, "Scala source after loading");
        const auto kbm = outDir.getChildFile ("test.kbm");
        kbm.replaceWithText (ss.kbm);
        CHECK (p.loadScalaFile (kbm, le), "load .kbm: " + le);
        NEAR (p.getTuning()->noteHz (69), 432.0, 1e-3, "kbm loaded");
        juce::MemoryBlock st;
        p.getStateInformation (st);
        CommaProcessor q;
        q.setStateInformation (st.getData(), (int) st.getSize());
        NEAR (q.getTuning()->noteHz (69), 432.0, 1e-3, "Scala files travel with the project");
        juce::String re;
        CHECK (q.setRatios ("3/2 7/4 2/1", re) && q.getTuning()->degrees == 3, "set ratios");
        scl.deleteFile();
        kbm.deleteFile();
    }

    // ================================================================ питч-шифтеры по БПФ
    std::cout << "shifters\n";
    {
        const auto in = sine (2.0, 440.0, 0.5f, 1);
        for (double cents : { 10.0, -10.0, 386.31, -1200.0 })
        {
            dsp::DelayShifter sh;
            sh.prepare (gRate);
            juce::AudioBuffer<float> out (1, in.getNumSamples());
            for (int i = 0; i < in.getNumSamples(); ++i) out.setSample (0, i, sh.process (in.getSample (0, i), (float) centsToRatio (cents), 40.0f));
            const double want = 440.0 * centsToRatio (cents);
            if (std::abs (cents) <= 10.0) NEAR (peakHz (out, 0, 24000, 65536), want, 0.35, "delay shifter " + juce::String (cents) + " c (FFT)");
            else
            {
                // без периода высота гуляет в пределах ± половины частоты окна |1 − r| · sr / W
                const double fw = std::abs (1.0 - centsToRatio (cents)) * gRate / (0.040 * gRate);
                NEAR (yinHz (out, 0, 24000, 48000), want, fw * 0.5 + 0.5, "delay shifter " + juce::String (cents) + " c, free window");
                // с известным периодом (окно — чётное число периодов) — точно
                dsp::DelayShifter sp;
                sp.prepare (gRate);
                for (int i = 0; i < in.getNumSamples(); ++i)
                    out.setSample (0, i, sp.process (in.getSample (0, i), (float) centsToRatio (cents), 40.0f, 0.0f, (float) (gRate / 440.0)));
                NEAR (peakHz (out, 0, 24000, 65536), want, 0.35, "delay shifter " + juce::String (cents) + " c, window in periods");
            }
        }
        for (double cents : { 10.0, -10.0, 701.955 })
        {
            dsp::YinTracker yin;
            yin.prepare (gRate);
            dsp::Psola ps;
            ps.prepare (gRate);
            juce::AudioBuffer<float> out (1, in.getNumSamples());
            for (int i = 0; i < in.getNumSamples(); ++i)
            {
                yin.push (in.getSample (0, i));
                out.setSample (0, i, ps.process (in.getSample (0, i), yin.getPeriod(), (float) centsToRatio (cents)));
            }
            NEAR (peakHz (out, 0, 24000, 65536), 440.0 * centsToRatio (cents), 0.35, "PSOLA " + juce::String (cents) + " c");
        }
        // YIN
        dsp::YinTracker yin;
        yin.prepare (gRate);
        const auto a = sine (0.5, 196.0, 0.5f, 1);
        for (int i = 0; i < a.getNumSamples(); ++i) yin.push (a.getSample (0, i));
        NEAR (yin.getHz(), 196.0, 0.05, "YIN 196 Hz");
        // Hermite: дробная задержка синуса
        dsp::DelayLine dl;
        dl.prepare (1000);
        float err = 0.0f;
        for (int i = 0; i < 4000; ++i)
        {
            dl.push ((float) std::sin (0.05 * i));
            if (i > 100) err = juce::jmax (err, std::abs (dl.read (10.25f) - (float) std::sin (0.05 * (i - 9.25))));
        }
        CHECK (err < 1e-4f, "Hermite read error " + juce::String (err));
    }

    // ================================================================ математика rack'а
    std::cout << "rack\n";
    {
        Rack r;
        r.setModuleForTest (0, std::make_unique<GainModule> (0.0f));
        r.setModuleForTest (1, std::make_unique<GainModule> (1.0f));
        r.setModuleForTest (2, std::make_unique<DelayModule> (100));
        r.setModuleForTest (3, std::make_unique<GainModule> (-1.0f));
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
            for (int i = 0; i < kBlock; ++i) { buf.setSample (0, i, in.getSample (0, (b * kBlock + i) % in.getNumSamples())); buf.setSample (1, i, buf.getSample (0, i)); }
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

        // упаковка состава
        juce::Random rng (7);
        for (int t = 0; t < 300; ++t)
        {
            RackLayout l;
            std::array<int, kNumModules> order { 0, 1, 2, 3, 4, 5, 6, 7 };
            for (int i = kNumModules - 1; i > 0; --i) std::swap (order[(size_t) i], order[(size_t) rng.nextInt (i + 1)]);
            const int n = rng.nextInt (kMaxSlots + 1);
            for (int i = 0; i < n; ++i) { l.add (order[(size_t) i]); l.parallel[(size_t) i] = i > 0 && rng.nextBool(); }
            CHECK (RackLayout::unpack (l.pack()) == l, "pack / unpack roundtrip");
        }
        auto g = makeLayout ({ 1, 4, 6, 7 }, { 3 });
        g.remove (2);
        CHECK (g.count == 3 && g.module[2] == 7 && ! g.parallel[2], "remove group start");
        auto h = makeLayout ({ 1, 4, 6, 7 }, { 1, 2, 3 });
        h.remove (2);
        CHECK (h.count == 3 && h.parallel[1] && h.parallel[2], "remove inside a group keeps the group");
    }

    // ================================================================ нотный слой MPE
    std::cout << "notes\n";
    {
        const auto t12 = defaultTuning();
        NoteLayer nl;
        nl.prepare (gRate, 4096);
        NoteSettings s;
        // MCM: нижняя зона на 15 каналов, бенд мастера ±2, участников ±48
        nlRun (nl, s, t12.get(), 0.01, mb ({ juce::MidiMessage::controllerEvent (1, 101, 0), juce::MidiMessage::controllerEvent (1, 100, 6),
                                             juce::MidiMessage::controllerEvent (1, 6, 15) }));
        CHECK (nl.isMpe() && nl.memberChannels() == 15, "MCM: 15 member channels");
        nlRun (nl, s, t12.get(), 0.05, mb ({ juce::MidiMessage::noteOn (2, 69, (juce::uint8) 100), juce::MidiMessage::pitchWheel (2, bendFor (2.0)) }));
        NEAR (nl.voice (nl.lastVoice()).pitch, 7100.0, 0.5, "member bend ±48: +2 semitones");
        CHECK (nl.voice (nl.lastVoice()).strike > 0.78f, "strike");
        nlRun (nl, s, t12.get(), 0.05, mb ({ juce::MidiMessage::pitchWheel (1, 16383) }));
        NEAR (nl.voice (nl.lastVoice()).pitch, 7300.0, 0.5, "master bend ±2 adds to every note");
        nlRun (nl, s, t12.get(), 0.02, mb ({ juce::MidiMessage::channelPressureChange (2, 127), juce::MidiMessage::controllerEvent (2, 74, 64) }));
        NEAR (nl.voice (nl.lastVoice()).pressure, 1.0, 1e-6, "pressure");
        NEAR (nl.voice (nl.lastVoice()).slide, 64.0 / 127.0, 1e-6, "slide CC74");
        nlRun (nl, s, t12.get(), 0.02, mb ({ juce::MidiMessage::controllerEvent (2, 101, 0), juce::MidiMessage::controllerEvent (2, 100, 0),
                                             juce::MidiMessage::controllerEvent (2, 6, 12), juce::MidiMessage::pitchWheel (1, 8192) }));
        // тот же бенд на канале 2, но диапазон теперь ±12: 1200 ¢ × (значение колеса)
        NEAR (nl.voice (nl.lastVoice()).pitch, 6900.0 + (double) (bendFor (2.0) - 8192) / 8192.0 * 1200.0, 0.5, "RPN 0: bend range 12 on channel 2");
        juce::MidiMessage off = juce::MidiMessage::noteOff (2, 69, (juce::uint8) 64);
        nlRun (nl, s, t12.get(), 0.02, mb ({ off }));
        CHECK (! nl.voice (0).held && std::abs (nl.voice (0).lift - 64.0f / 127.0f) < 1e-3f, "lift velocity");

        // Glide: 200 мс, линейно в центах
        NoteLayer g;
        g.prepare (gRate, 4096);
        NoteSettings gs;
        gs.glideMs = 200.0;
        nlRun (g, gs, t12.get(), 0.3, mb ({ juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }));
        NEAR (g.voice (0).pitch, 6000.0, 0.5, "first note has nothing to glide from");
        nlRun (g, gs, t12.get(), 0.1, mb ({ juce::MidiMessage::noteOff (1, 60), juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100) }));
        NEAR (g.voice (g.lastVoice()).pitch, 6600.0, 40.0, "glide halfway after 100 ms");
        nlRun (g, gs, t12.get(), 0.2);
        NEAR (g.voice (g.lastVoice()).pitch, 7200.0, 0.5, "glide arrives");
        // в герцах — середина выше
        gs.glideDomain = 1;
        nlRun (g, gs, t12.get(), 0.1, mb ({ juce::MidiMessage::noteOff (1, 72), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }));
        nlRun (g, gs, t12.get(), 0.3);
        nlRun (g, gs, t12.get(), 0.1, mb ({ juce::MidiMessage::noteOff (1, 60), juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100) }));
        CHECK (g.voice (g.lastVoice()).pitch > 6650.0, "glide in hertz: the middle is higher");
        // скорость в центах в секунду
        gs.glideDomain = 0;
        gs.glideMode = 1;
        gs.glideRate = 1200.0;
        nlRun (g, gs, t12.get(), 0.3);
        nlRun (g, gs, t12.get(), 0.5, mb ({ juce::MidiMessage::noteOff (1, 72), juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }));
        NEAR (g.voice (g.lastVoice()).pitch, 6600.0, 40.0, "rate 1200 c/s: halfway after 0.5 s");

        // Snap: стоящий бенд на +40 ¢ притягивается к ступени, без Snap — нет
        for (double snap : { 0.0, 1.0 })
        {
            NoteLayer sn;
            sn.prepare (gRate, 4096);
            NoteSettings ss;
            ss.snap = snap;
            ss.snapMs = 50.0;
            nlRun (sn, ss, t12.get(), 1.0, mb ({ juce::MidiMessage::noteOn (2, 69, (juce::uint8) 100), juce::MidiMessage::pitchWheel (2, bendFor (0.4)) }));
            NEAR (sn.voice (0).pitch, snap > 0.5 ? 6900.0 : 6940.0, 2.0, "snap " + juce::String (snap));
        }
        // характер 100 %: скольжение идёт по ступеням
        {
            NoteLayer ch;
            ch.prepare (gRate, 4096);
            NoteSettings cs;
            cs.glideMs = 500.0;
            cs.character = 1.0;
            nlRun (ch, cs, t12.get(), 0.2, mb ({ juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }));
            std::vector<double> trace;
            nlRun (ch, cs, t12.get(), 0.5, mb ({ juce::MidiMessage::noteOff (1, 60), juce::MidiMessage::noteOn (1, 72, (juce::uint8) 100) }), &trace);
            int onStep = 0;
            for (double pc : trace) onStep += std::abs (pc - t12->nearest (pc)) < 6.0 ? 1 : 0;
            CHECK (onStep >= (int) trace.size() * 8 / 10, "character: glissando on steps " + juce::String (onStep) + "/" + juce::String ((int) trace.size()));
        }
        // посадка: перелёт за ступень
        {
            NoteLayer ld;
            ld.prepare (gRate, 4096);
            NoteSettings ls;
            ls.glideMs = 150.0;
            ls.overshoot = 20.0;
            nlRun (ld, ls, t12.get(), 0.2, mb ({ juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100) }));
            std::vector<double> trace;
            nlRun (ld, ls, t12.get(), 0.6, mb ({ juce::MidiMessage::noteOff (1, 60), juce::MidiMessage::noteOn (1, 64, (juce::uint8) 100) }), &trace);
            const double mx = *std::max_element (trace.begin(), trace.end());
            CHECK (mx > 6408.0 && mx < 6425.0, "landing overshoot " + juce::String (mx));
            NEAR (trace.back(), 6400.0, 1.0, "landing settles");
        }
        // 16 голосов и кража
        {
            NoteLayer many;
            many.prepare (gRate, 4096);
            juce::MidiBuffer b;
            for (int i = 0; i < 20; ++i) b.addEvent (juce::MidiMessage::noteOn (1, 40 + i, (juce::uint8) 90), 0);
            nlRun (many, s, t12.get(), 0.05, b);
            CHECK (many.numHeld() == kMaxVoices, "16 voices, oldest stolen");
        }
    }

    // ================================================================ модули
    std::cout << "modules\n";
    {
        CommaProcessor p;
        TestPlayHead ph;
        p.setPlayHead (&ph);
        p.prepareToPlay (gRate, kBlock);
        const auto s440 = sine (2.0, 440.0, 0.4f);

        // Doubler: левая выше, правая ниже
        only (p, 0);
        setP (p, P::DblCents, 9.0f);
        setP (p, P::DblFocus, 20.0f);
        auto out = runProc (p, s440);
        NEAR (peakHz (out, 0, 24000, 65536), 440.0 * centsToRatio (9.0), 0.4, "Doubler: left +9 c");
        NEAR (peakHz (out, 1, 24000, 65536), 440.0 * centsToRatio (-9.0), 0.4, "Doubler: right -9 c");
        setP (p, P::DblUnit, 2.0f);
        setP (p, P::TunSource, 1.0f);
        setP (p, P::TunEdo, 24.0f);
        p.refreshTuning();
        out = runProc (p, s440);
        NEAR (yinHz (out, 0, 24000, 48000), 440.0 * centsToRatio (50.0), 1.0, "Doubler: one step of 24-EDO");

        // Staircase: каждое эхо на ступень 12-EDO выше
        only (p, 2);
        setP (p, P::StaSync, 0.0f);
        setP (p, P::StaTime, 400.0f);
        setP (p, P::StaFeedback, 70.0f);
        setP (p, P::StaTone, 20000.0f);
        auto burst = sine (1.6, 440.0, 0.4f);
        for (int ch = 0; ch < 2; ++ch) burst.clear (ch, (int) (0.15 * gRate), burst.getNumSamples() - (int) (0.15 * gRate));
        runProc (p, juce::AudioBuffer<float> (2, (int) gRate));   // очистить хвосты
        p.rack.reset();
        out = runProc (p, burst);
        NEAR (peakHz (out, 0, (int) (0.42 * gRate), (int) (0.1 * gRate)), 466.164, 2.0, "Staircase: echo 1 one step up");
        NEAR (peakHz (out, 0, (int) (0.82 * gRate), (int) (0.1 * gRate)), 493.883, 2.5, "Staircase: echo 2 two steps up");

        // Shifter: +100 Гц, зеркальная частота подавлена
        only (p, 5);
        setP (p, P::ShiShift, 100.0f);
        const auto s1k = sine (1.5, 1000.0, 0.4f);
        out = runProc (p, s1k);
        NEAR (peakHz (out, 0, 24000, 48000), 1100.0, 0.5, "Shifter: 1000 + 100 Hz");
        {
            const auto sp = spectrum (out, 0, 24000, 48000);
            CHECK (bandDb (sp, 1100.0) - bandDb (sp, 900.0) > 30.0, "Shifter: image below -30 dB");
        }
        setP (p, P::ShiShift, -100.0f);
        out = runProc (p, s1k);
        NEAR (peakHz (out, 0, 24000, 48000), 900.0, 0.5, "Shifter: 1000 - 100 Hz");
        setP (p, P::ShiSync, 1.0f);
        setP (p, P::ShiDiv, 4.0f);   // 1/4: раз в долю
        ph.bpm = 120.0;
        runProc (p, sine (0.1, 1000.0, 0.1f));
        auto* shm = dynamic_cast<ShifterModule*> (&p.rack.getModule ((int) ModuleId::Shifter));
        NEAR (shm->getShiftHz(), 2.0, 1e-6, "Shifter sync: BPM 120, 1/4 = 2 Hz");

        // Harmonizer: голос на 3/2 в обоих режимах
        for (int mode : { 1, 0 })
        {
            only (p, 1);
            setP (p, P::HarMode, (float) mode);
            setP (p, P::Har1Unit, 1.0f);
            setP (p, P::Har1Ratio, (float) ratioIndex (3, 2));
            setP (p, P::Har2Level, 0.0f);
            p.updateLatencies();
            out = runProc (p, sine (2.0, 220.0, 0.4f));
            NEAR (peakHz (out, 0, 48000, 48000), 330.0, 0.6, juce::String ("Harmonizer 3/2, ") + (mode ? "Quality" : "Low latency"));
            CHECK (p.getLatencySamples() == (mode ? 3 * 800 + 4 : 0), "Harmonizer latency " + juce::String (p.getLatencySamples()));
        }

        // Autotune: 450 Гц → 440 (ближайшая ступень), в обоих режимах; к держащейся ноте
        for (int mode : { 1, 0 })
        {
            only (p, 3);
            setP (p, P::AutSpeed, 0.0f);
            setP (p, P::AutMode, (float) mode);
            p.updateLatencies();
            out = runProc (p, sine (2.0, 450.0, 0.4f));
            NEAR (peakHz (out, 0, 48000, 48000), 440.0, 1.0, juce::String ("Autotune to the scale, ") + (mode ? "Quality" : "Low latency"));
        }
        only (p, 3);
        setP (p, P::AutSpeed, 0.0f);
        setP (p, P::AutTarget, 1.0f);
        p.updateLatencies();
        out = runProc (p, sine (2.0, 300.0, 0.4f), kBlock, { { 0, juce::MidiMessage::noteOn (1, 62, (juce::uint8) 100) } });
        NEAR (peakHz (out, 0, 48000, 48000), 293.665, 1.0, "Autotune to the held note D3");
        runProc (p, sine (0.05, 300.0, 0.1f), kBlock, { { 0, juce::MidiMessage::noteOff (1, 62) } });

        // Resonator: шум + нота → пик на высоте ноты; бенд двигает его; без нот — тишина
        only (p, 4);
        setP (p, P::ResDecay, 2000.0f);
        auto nz = noise (2.0, 0.3f);
        out = runProc (p, nz, kBlock, { { 0, juce::MidiMessage::noteOn (2, 69, (juce::uint8) 100) } });
        NEAR (peakHz (out, 0, 24000, 48000, 100.0, 2000.0), 440.0, 2.0, "Resonator rings at the note");
        CHECK (rms (out, 0, 24000, 48000) > 0.01f, "Resonator is audible: " + juce::String (rms (out, 0, 24000, 48000)));
        out = runProc (p, nz, kBlock, { { 0, juce::MidiMessage::pitchWheel (2, bendFor (2.0)) } });
        NEAR (peakHz (out, 0, 48000, 48000, 100.0, 2000.0), 493.883, 3.0, "Resonator follows the bend");
        out = runProc (p, noise (3.0, 0.3f, 3), kBlock, { { 0, juce::MidiMessage::noteOff (2, 69) }, { 0, juce::MidiMessage::pitchWheel (2, 8192) } });
        CHECK (rms (out, 0, (int) (2.5 * gRate), 20000) < 1e-3f, "Resonator: silence after release");
        // Snap и мод-матрица на голос: давление → Brightness
        setP (p, P::modSrc (0), (float) SrcPressure);
        {
            const auto& d = modDestinations();
            int idx = 0;
            for (int i = 1; i < (int) d.size(); ++i) if (d[(size_t) i] == P::ShiShift) idx = i;
            setP (p, P::modDst (0), (float) idx);
        }
        setP (p, P::modAmt (0), 50.0f);
        p.setLayout (makeLayout ({ 5 }));
        setP (p, P::ShiShift, 5.0f);
        setP (p, P::ShiSync, 0.0f);
        runProc (p, sine (0.2, 440.0, 0.1f), kBlock, { { 0, juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100) }, { 0, juce::MidiMessage::channelPressureChange (2, 127) } });
        CHECK (shm->getShiftHz() > 100.0f, "matrix: pressure moves Shifter shift, now " + juce::String (shm->getShiftHz()));
        runProc (p, sine (0.2, 440.0, 0.1f), kBlock, { { 0, juce::MidiMessage::channelPressureChange (2, 0) }, { 0, juce::MidiMessage::noteOff (2, 60) } });
        NEAR (shm->getShiftHz(), 5.0, 0.01, "matrix: without pressure the knob value");
        setP (p, P::modSrc (0), 0.0f);

        // Varispeed: +100 ¢, тормоз и разгон
        only (p, 6);
        setP (p, P::VarWow, 0.0f);
        setP (p, P::VarFlutter, 0.0f);
        setP (p, P::VarSpeed, 100.0f);
        out = runProc (p, s440);
        NEAR (peakHz (out, 0, 48000, 48000), 466.164, 1.0, "Varispeed +100 c");
        setP (p, P::VarSpeed, 0.0f);
        setP (p, P::VarBrakeTime, 300.0f);
        setP (p, P::VarBrake, 1.0f);
        out = runProc (p, sine (0.6, 440.0, 0.4f));
        CHECK (rms (out, 0, (int) (0.45 * gRate), 4800) < 0.01f, "Varispeed brake stops the tape");
        setP (p, P::VarBrake, 0.0f);
        out = runProc (p, sine (1.0, 440.0, 0.4f));
        CHECK (rms (out, 0, (int) (0.8 * gRate), 4800) > 0.2f, "Varispeed spins back up");
        setP (p, P::VarRamp, -1200.0f);
        setP (p, P::VarRampLen, 2.0f);
        ph.playing = true;
        ph.ppq = 2.0;   // середина такта: рампа на −600 ¢
        out = runProc (p, sine (0.3, 440.0, 0.4f), kBlock, {}, nullptr, &ph);
        CHECK (allFinite (out), "Varispeed ramp: finite");
        ph.playing = false;
        CHECK (allFinite (out), "Varispeed: finite");

        // Spectral: Snap 450 → 440, без сдвига — точная копия с задержкой окна, Stretch под тритаву
        only (p, 7);
        out = runProc (p, sine (2.0, 450.0, 0.4f));
        NEAR (peakHz (out, 0, 48000, 48000), 440.0, 1.5, "Spectral snap 450 Hz to A");
        CHECK (p.getLatencySamples() == 2048, "Spectral latency 2048");
        setP (p, P::SpeAmount, 0.0f);
        const auto mix = sine (1.0, 330.0, 0.3f);
        out = runProc (p, mix);
        CHECK (tailError (out, mix, 1.0f, 2048, 12000) < 2e-3f, "Spectral amount 0: delayed copy, error " + juce::String (tailError (out, mix, 1.0f, 2048, 12000)));
        setP (p, P::SpeQuality, 0.0f);
        p.updateLatencies();
        CHECK (p.getLatencySamples() == 1024, "Spectral low latency 1024");
        setP (p, P::SpeQuality, 1.0f);
        setP (p, P::SpeAmount, 100.0f);
        setP (p, P::SpeMode, 1.0f);
        juce::String re;
        p.setRatios ("27/25 25/21 9/7 7/5 75/49 5/3 9/5 49/25 15/7 7/3 63/25 25/9 3/1", re);
        p.updateLatencies();
        auto two = sine (2.0, 200.0, 0.3f);
        {
            const auto s400 = sine (2.0, 400.0, 0.3f);
            for (int ch = 0; ch < 2; ++ch) two.addFrom (ch, 0, s400, ch, 0, two.getNumSamples());
        }
        out = runProc (p, two);
        {
            const auto sp = spectrum (out, 0, 48000, 48000);
            CHECK (bandDb (sp, 600.0) - bandDb (sp, 400.0) > 10.0, "Spectral stretch: 2:1 becomes 3:1");
        }

        // задержка rack'а: последовательно — сумма, параллельно — максимум
        p.presets.loadFactory (0);
        p.setLayout (makeLayout ({ 1, 7 }));
        p.updateLatencies();
        CHECK (p.getLatencySamples() == 2404 + 2048, "serial latency " + juce::String (p.getLatencySamples()));
        p.setLayout (makeLayout ({ 1, 7 }, { 1 }));
        CHECK (p.getLatencySamples() == 2404, "parallel latency " + juce::String (p.getLatencySamples()));

        // всё выключено: выход = вход, задержанный на сообщённую задержку
        fullRack (p);
        for (int m = 0; m < kNumModules; ++m) setP (p, P::moduleOn (m), 0.0f);
        p.updateLatencies();
        const int L = p.getLatencySamples();
        CHECK (L == 2404 * 2 + 2048, "full rack latency " + juce::String (L));
        const auto in = sine (1.0, 220.0, 0.4f);
        runProc (p, in);
        out = runProc (p, in);
        CHECK (tailError (out, in, 1.0f, L, L + 4800) < 1e-4f, "all bypassed: delayed copy");
    }

    // ================================================================ процессор
    std::cout << "processor\n";
    {
        CommaProcessor p;
        p.prepareToPlay (gRate, kBlock);
        CHECK (p.getLayout().count == 0 && p.presets.getCurrentName() == "Init", "starts with an empty rack");
        const auto in = sine (0.5, 220.0, 0.5f);
        CHECK (tailError (runProc (p, in), in, 1.0f, 0, 0) < 1e-6f, "empty rack passes audio unchanged");
        fullRack (p);
        p.setParallel (3, true);
        const auto hot = sine (0.5, 220.0, 2.0f);
        CHECK (allFinite (runProc (p, hot)), "output ≤ 0 dBFS");
        auto nan = sine (0.2, 220.0, 0.5f);
        nan.setSample (0, 100, std::numeric_limits<float>::quiet_NaN());
        nan.setSample (1, 2000, std::numeric_limits<float>::infinity());
        CHECK (allFinite (runProc (p, nan)), "NaN and inf do not reach the output");
        p.setLayout ({});
        setP (p, P::Output, -6.0f);
        runProc (p, in);
        CHECK (tailError (runProc (p, in), in, juce::Decibels::decibelsToGain (-6.0f), 0, 0) < 1e-4f, "output volume");
        setP (p, P::Output, 0.0f);
        runProc (p, sine (0.05, 220.0, 0.1f), kBlock, { { 0, juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100) }, { 10, juce::MidiMessage::noteOn (3, 60, (juce::uint8) 90) } });
        CHECK (p.midiNotes.load() == 2, "MPE: two notes on two channels");
        runProc (p, sine (0.05, 220.0, 0.1f), kBlock, { { 0, juce::MidiMessage::noteOff (2, 60) }, { 5, juce::MidiMessage::noteOff (3, 60) } });
        CHECK (p.midiNotes.load() == 0, "MPE: notes released");

        CommaProcessor mono;
        juce::AudioProcessor::BusesLayout bl;
        bl.inputBuses.add (juce::AudioChannelSet::mono());
        bl.outputBuses.add (juce::AudioChannelSet::mono());
        CHECK (mono.setBusesLayout (bl), "mono layout");
        mono.prepareToPlay (gRate, kBlock);
        fullRack (mono);
        CHECK (allFinite (runProc (mono, sine (0.5, 220.0, 0.5f, 1), kBlock, { { 0, juce::MidiMessage::noteOn (1, 57, (juce::uint8) 100) } })), "mono: finite");
    }

    // ================================================================ состояние, undo, пресеты
    std::cout << "state\n";
    {
        CommaProcessor a;
        a.setLayout (makeLayout ({ 2, 0, 5 }, { 1 }));
        setP (a, P::DblMix, 33.0f);
        setP (a, P::ShiOn, 0.0f);
        setP (a, P::Output, -3.0f);
        setP (a, P::TunSource, 1.0f);
        setP (a, P::TunEdo, 31.0f);
        juce::MemoryBlock mbk;
        a.getStateInformation (mbk);
        CommaProcessor b;
        b.setStateInformation (mbk.getData(), (int) mbk.getSize());
        CHECK (b.getLayout() == a.getLayout(), "state: rack");
        NEAR (getP (b, P::DblMix), 33.0, 0.05, "state: mix");
        CHECK (getP (b, P::ShiOn) < 0.5f, "state: bypass");
        NEAR (getP (b, P::Output), -3.0, 0.05, "state: output");
        CHECK (b.getTuning()->degrees == 31, "state: tuning");
        CHECK (! b.undo.canUndo(), "state: undo history cleared");
        const char junk[] = "not a state at all";
        b.setStateInformation (junk, (int) sizeof (junk));
        CHECK (b.getLayout() == a.getLayout(), "junk state is ignored");
        juce::ValueTree t ("Comma");
        juce::MemoryBlock old;
        if (auto xml = t.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, old);
        b.setStateInformation (old.getData(), (int) old.getSize());
        CHECK (b.getLayout().count == 0, "state without a rack: empty rack");
        // состояние v0.1: только on, mix и output
        juce::ValueTree v01 ("Comma");
        juce::ValueTree rack ("Rack"), slot ("Slot");
        slot.setProperty ("module", "doubler", nullptr);
        rack.appendChild (slot, nullptr);
        v01.appendChild (rack, nullptr);
        juce::ValueTree prm ("PARAM");
        prm.setProperty ("id", "doubler_mix", nullptr);
        prm.setProperty ("value", 42.0, nullptr);
        v01.appendChild (prm, nullptr);
        juce::MemoryBlock m01;
        if (auto xml = v01.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, m01);
        b.setStateInformation (m01.getData(), (int) m01.getSize());
        CHECK (b.getLayout().count == 1 && std::abs (getP (b, P::DblMix) - 42.0f) < 0.05f, "v0.1 project opens");

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
        setP (u, P::ResMix, 20.0f);
        u.apvts.copyState();   // APVTS пишет значения в дерево по таймеру; copyState() сбрасывает их сразу
        u.undo.beginNewTransaction();
        u.undo.undo();
        pumpMessages (100);
        NEAR (getP (u, P::ResMix), 100.0, 0.05, "undo a knob");

        CommaProcessor pr;
        setP (pr, P::Output, -9.0f);
        auto tree = a.apvts.copyState();
        tree.setProperty ("presetName", "Mine", nullptr);
        CHECK (pr.presets.loadUserTree (tree), "user preset");
        CHECK (pr.getLayout() == a.getLayout(), "user preset: rack");
        NEAR (getP (pr, P::Output), -9.0, 0.05, "user preset keeps Output");
        CHECK (pr.presets.getCurrentName() == "Mine", "user preset name");
        CHECK (pr.getTuning()->degrees == 31, "user preset: tuning");
        pr.presets.loadFactory (0);
        CHECK (pr.getLayout().count == 0 && std::abs (getP (pr, P::Output) + 9.0f) < 0.05f, "Init: empty rack, Output kept");
        CHECK (pr.getTuning()->degrees == 12, "Init: 12-EDO");
    }

    // ================================================================ все пресеты
    std::cout << "presets\n";
    {
        CommaProcessor p;
        TestPlayHead ph;
        ph.playing = true;
        p.setPlayHead (&ph);
        p.prepareToPlay (gRate, kBlock);
        auto in = sine (1.2, 220.0, 0.3f);
        {
            const auto nz = noise (1.2, 0.05f, 5);
            for (int ch = 0; ch < 2; ++ch) in.addFrom (ch, 0, nz, ch, 0, in.getNumSamples());
        }
        setP (p, P::Output, -1.0f);
        for (int i = 0; i < p.presets.getNumFactory(); ++i)
        {
            p.presets.loadFactory (i);
            p.rack.reset();
            const auto out = runProc (p, in, kBlock, { { 0, juce::MidiMessage::noteOn (2, 57, (juce::uint8) 100) }, { 0, juce::MidiMessage::noteOn (3, 64, (juce::uint8) 90) },
                                                       { 40000, juce::MidiMessage::noteOff (2, 57) }, { 40000, juce::MidiMessage::noteOff (3, 64) } }, nullptr, &ph);
            const auto& name = factoryPresets()[(size_t) i].name;
            CHECK (allFinite (out), "preset " + name + ": finite, ≤ 0 dBFS");
            CHECK (rms (out, 0, 12000, 40000) > 0.005f, "preset " + name + ": not silent, rms " + juce::String (rms (out, 0, 12000, 40000)));
            NEAR (getP (p, P::Output), -1.0, 0.05, "preset " + name + " keeps Output");
        }
    }

    // ================================================================ кубик
    std::cout << "dice\n";
    {
        CommaProcessor p;
        p.prepareToPlay (gRate, kBlock);
        fullRack (p);
        setP (p, P::Output, -2.0f);
        setP (p, P::VarOn, 0.0f);
        setP (p, P::TunSource, 1.0f);
        setP (p, P::Glide, 100.0f);
        juce::Random rng (2026);
        RandomizerOptions all;
        all.enabled.fill (true);
        auto in = sine (0.25, 330.0, 0.4f);
        for (int roll = 0; roll < 40; ++roll)
        {
            randomizePatch (p, rng, all);
            p.updateLatencies();
            CHECK (p.getLayout().count == kNumModules, "dice keeps the set of modules");
            for (int m = 0; m < kNumModules; ++m)
            {
                CHECK (getP (p, P::moduleMix (m)) >= 40.0f - 0.05f, "dice: mix ≥ 40%");
                CHECK ((getP (p, P::moduleOn (m)) > 0.5f) == (m != (int) ModuleId::Varispeed), "dice never touches bypass");
            }
            CHECK (getP (p, P::StaFeedback) <= 75.05f && getP (p, P::ShiFeedback) <= 75.05f, "dice: feedback ≤ 75%");
            NEAR (getP (p, P::Output), -2.0, 0.05, "dice never touches Output");
            CHECK (juce::roundToInt (getP (p, P::TunSource)) == 1 && std::abs (getP (p, P::Glide) - 100.0f) < 0.05f, "dice never touches tuning and notes");
            const auto out = runProc (p, in, kBlock, { { 0, juce::MidiMessage::noteOn (2, 60, (juce::uint8) 100) }, { 6000, juce::MidiMessage::noteOff (2, 60) } });
            CHECK (allFinite (out), "dice: finite, ≤ 0 dBFS");
            CHECK (rms (out, 0, 4000, 8000) > 0.003f, "dice: not silent");
        }
        RandomizerOptions none;
        none.enabled.fill (false);
        const auto before = p.getLayout();
        const float cents = getP (p, P::DblCents);
        randomizePatch (p, rng, none);
        CHECK (p.getLayout() == before && std::abs (getP (p, P::DblCents) - cents) < 1e-3f, "forbidden groups stay");
        randomizePatch (p, rng, RandomizerOptions());
        CHECK (p.getLayout() == before, "order is off by default");
    }

    // ================================================================ частоты и блоки
    std::cout << "rates\n";
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 1, 64, 333, 4096 })
        {
            gRate = rate;
            CommaProcessor p;
            p.prepareToPlay (rate, block);
            fullRack (p);
            p.setParallel (2, true);
            setP (p, P::Snap, 50.0f);
            const auto in = sine (block == 1 ? 0.15 : 0.5, 440.0, 0.5f);
            const auto out = runProc (p, in, block, { { 0, juce::MidiMessage::noteOn (2, 69, (juce::uint8) 100) } });
            CHECK (allFinite (out), "rate " + juce::String (rate) + " block " + juce::String (block));
        }
    gRate = 48000.0;

    // ================================================================ скорость
    std::cout << "speed\n";
    {
        CommaProcessor p;
        p.prepareToPlay (gRate, kBlock);
        fullRack (p);
        const auto in = sine (10.0, 440.0, 0.5f);
        Midi midi;
        for (int i = 0; i < 4; ++i) midi.push_back ({ 0, juce::MidiMessage::noteOn (2 + i, 57 + 4 * i, (juce::uint8) 100) });
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        runProc (p, in, kBlock, midi);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::cout << "  full rack, 4 notes: 10 s of audio in " << juce::String (ms, 1) << " ms\n";
        CHECK (ms < 5000.0, "speed");
    }

    // ================================================================ интерфейс
    std::cout << "ui\n";
    {
        CommaProcessor p;
        TestPlayHead ph;
        ph.playing = true;
        p.setPlayHead (&ph);
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
        auto voice = sine (0.8, 223.0, 0.35f);
        const Midi chord = { { 0, juce::MidiMessage::noteOn (2, 57, (juce::uint8) 100) }, { 0, juce::MidiMessage::noteOn (3, 64, (juce::uint8) 90) },
                             { 0, juce::MidiMessage::noteOn (4, 69, (juce::uint8) 80) }, { 0, juce::MidiMessage::channelPressureChange (3, 90) },
                             { 0, juce::MidiMessage::pitchWheel (4, bendFor (0.3)) } };
        const Midi chordOff = { { 0, juce::MidiMessage::noteOff (2, 57) }, { 0, juce::MidiMessage::noteOff (3, 64) }, { 0, juce::MidiMessage::noteOff (4, 69) } };
        // шаблон для каждого модуля
        const std::vector<std::pair<int, const char*>> perModule = { { 0, "Micro Double" }, { 1, "Just Choir" }, { 2, "19-EDO Stairs" }, { 3, "Glide Tune 31" },
                                                                     { 4, "Scale Bells" }, { 5, "Tempo Beats" }, { 6, "Tape Wobble" }, { 7, "Sethares Snap" } };
        auto loadByName = [&] (const juce::String& n)
        {
            for (int i = 0; i < p.presets.getNumFactory(); ++i) if (factoryPresets()[(size_t) i].name == n) p.presets.loadFactory (i);
        };

        for (auto theme : { ThemeId::Oxygen, ThemeId::Rubber })
        {
            const juce::String tn = theme == ThemeId::Oxygen ? "oxygen" : "rubber";
            ed->setThemeForTest (theme);
            ed->setViewTabForTest (0);
            p.presets.loadFactory (0);
            runProc (p, voice, kBlock, {}, ed);
            shoot ("ui_" + tn + "_empty");

            // большой rack с параллелью, выключенным слотом, MPE-голосами
            loadByName ("MPE Glide Rack");
            p.setLayout (makeLayout ({ 0, 2, 5, 4, 6 }, { 3 }));
            setP (p, P::ShiOn, 0.0f);
            setP (p, P::TunSource, 1.0f);
            setP (p, P::TunEdo, 19.0f);
            p.refreshTuning();
            ed->getRackForTest().selectModule (4);
            runProc (p, voice, kBlock, chord, ed, &ph);
            shoot ("ui_" + tn + "_main");
            ed->setViewTabForTest (1);
            shoot ("ui_" + tn + "_notes");
            ed->setViewTabForTest (2);
            shoot ("ui_" + tn + "_matrix");
            ed->setViewTabForTest (0);
            runProc (p, voice, kBlock, chordOff, ed, &ph);

            // перетаскивание
            ed->getRackForTest().dragForTest (4, 0);
            CHECK (p.getLayout().module[0] == 6 && p.getLayout().count == 5, "drag a card to the front");

            if (theme == ThemeId::Oxygen)
                for (auto& [m, name] : perModule)
                {
                    loadByName (name);
                    ed->getRackForTest().selectModule (m);
                    runProc (p, voice, kBlock, m == 4 ? chord : Midi {}, ed, &ph);
                    shoot ("ui_oxygen_" + juce::String (moduleInfo (m).key));
                    if (m == 4) runProc (p, voice, kBlock, chordOff, ed, &ph);
                }

            ed->rollDiceForTest();
            runProc (p, voice, kBlock, {}, ed);

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
