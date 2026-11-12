#include "Tuning.h"
#include "Tunings.h"

namespace comma
{
// ===========================================================================
int TuningSnapshot::nearestIndex (double c) const noexcept
{
    if (numSteps <= 1) return 0;
    const float x = (float) c;
    int lo = 0, hi = numSteps - 1;
    if (x <= steps[0]) return 0;
    if (x >= steps[(size_t) hi]) return hi;
    while (hi - lo > 1)
    {
        const int mid = (lo + hi) / 2;
        if (steps[(size_t) mid] <= x) lo = mid; else hi = mid;
    }
    return (x - steps[(size_t) lo] <= steps[(size_t) hi] - x) ? lo : hi;
}

double TuningSnapshot::shiftSteps (double c, int k) const noexcept
{
    if (numSteps <= 1) return c + k * meanStep;
    const int i = nearestIndex (c);
    const double off = c - (double) steps[(size_t) i];
    const int j = i + k;
    if (j >= 0 && j < numSteps) return (double) steps[(size_t) j] + off;
    // за краями таблицы — средним шагом
    return (double) steps[(size_t) juce::jlimit (0, numSteps - 1, j)] + off + (j < 0 ? j : j - (numSteps - 1)) * meanStep;
}

int TuningSnapshot::degreeOf (double c) const noexcept
{
    if (degrees <= 0 || periodCents <= 0.0) return 0;
    const double n = nearest (c);
    double rel = std::fmod (n - rootCents, periodCents);
    if (rel < 0.0) rel += periodCents;
    // ближайшая ступень внутри периода
    int best = 0;
    double bestD = 1.0e9;
    for (int d = 0; d < (int) degreeCents.size(); ++d)
    {
        const double dd = std::min (std::abs (rel - degreeCents[(size_t) d]), std::abs (rel - degreeCents[(size_t) d] - periodCents));
        if (dd < bestD) { bestD = dd; best = d; }
    }
    return best;
}

// ===========================================================================
TuningHolder::TuningHolder() { publish (defaultTuning()); }

void TuningHolder::publish (TuningPtr t)
{
    if (t == nullptr) t = defaultTuning();
    ring[(size_t) next] = t;   // старый указатель на этом месте опубликован 32 сборки назад
    next = (next + 1) % 32;
    current.store (t.get());
}

// ===========================================================================
namespace
{
const char* kNames12[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void fillSteps (TuningSnapshot& s)
{
    s.numSteps = 0;
    if (s.degreeCents.empty() || s.periodCents <= 1.0) return;
    const int first = (int) std::floor ((0.0 - s.rootCents) / s.periodCents) - 1;
    const int last = (int) std::ceil ((TuningSnapshot::kTop - s.rootCents) / s.periodCents) + 1;
    for (int o = first; o <= last && s.numSteps < TuningSnapshot::kMaxSteps; ++o)
        for (double d : s.degreeCents)
        {
            const double c = s.rootCents + o * s.periodCents + d;
            if (c < 0.0 || c > TuningSnapshot::kTop) continue;
            if (s.numSteps > 0 && c - (double) s.steps[(size_t) s.numSteps - 1] < 0.01) continue;
            if (s.numSteps >= TuningSnapshot::kMaxSteps) break;
            s.steps[(size_t) s.numSteps++] = (float) c;
        }
    s.degrees = (int) s.degreeCents.size();
    s.meanStep = s.periodCents / juce::jmax (1, s.degrees);
}

juce::String centsLabel (double c) { return juce::String (c, 1); }

std::shared_ptr<TuningSnapshot> fromTunings (const Tunings::Scale& scale, const Tunings::Tuning& tuning, double rootCents)
{
    auto s = std::make_shared<TuningSnapshot>();
    for (int n = 0; n < 128; ++n) s->noteCents[(size_t) n] = hzToCents (tuning.frequencyForMidiNote (n));
    s->periodCents = scale.tones.empty() ? 1200.0 : scale.tones.back().cents;
    s->degreeCents.push_back (0.0);
    s->degreeNames.push_back ("1/1");
    for (int i = 0; i + 1 < (int) scale.tones.size(); ++i)
    {
        s->degreeCents.push_back (scale.tones[(size_t) i].cents);
        const auto& t = scale.tones[(size_t) i];
        s->degreeNames.push_back (t.type == Tunings::Tone::kToneRatio ? juce::String (t.ratio_n) + "/" + juce::String (t.ratio_d) : centsLabel (t.cents));
    }
    // ступени по возрастанию (в .scl они могут идти не по порядку)
    std::vector<std::pair<double, juce::String>> d;
    for (size_t i = 0; i < s->degreeCents.size(); ++i)
        if (s->degreeCents[i] >= 0.0 && s->degreeCents[i] < s->periodCents) d.push_back ({ s->degreeCents[i], s->degreeNames[i] });
    std::sort (d.begin(), d.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    s->degreeCents.clear();
    s->degreeNames.clear();
    for (auto& p : d) { s->degreeCents.push_back (p.first); s->degreeNames.push_back (p.second); }
    s->rootCents = rootCents;
    fillSteps (*s);
    return s;
}

double rootCentsFor (const TuningSpec& spec)
{
    // корень: нота C3 + root в 12-EDO от A4 = refHz
    return hzToCents (spec.refHz * std::exp2 ((60 + spec.root - 69) / 12.0));
}
} // namespace

juce::String noteName12 (int note)
{
    note = juce::jlimit (0, 127, note);
    return juce::String (kNames12[note % 12]) + juce::String (note / 12 - 2);
}

juce::String formatCents (double c, int decimals)
{
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
    auto s = juce::String (std::abs (c), decimals);
    return (c < 0.0 && s != juce::String (0.0, decimals) ? minus : juce::String ("+")) + s + juce::String::fromUTF8 (" \xc2\xa2");
}

juce::String defaultRatios() { return "1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8 2/1"; }

juce::String ratiosToScl (const juce::String& text, juce::String& error)
{
    juce::StringArray tok;
    tok.addTokens (text.replaceCharacters (",;\n\t", "    "), " ", "");
    tok.removeEmptyStrings();
    std::vector<std::pair<double, juce::String>> tones;
    for (auto t : tok)
    {
        t = t.trim();
        double c = 0.0;
        juce::String rep;
        if (t.containsChar ('/'))
        {
            const auto n = t.upToFirstOccurrenceOf ("/", false, false).getLargeIntValue();
            const auto d = t.fromFirstOccurrenceOf ("/", false, false).getLargeIntValue();
            if (n <= 0 || d <= 0) { error = "Can't read \"" + t + "\": use ratios like 3/2 or cents like 701.96."; return {}; }
            c = ratioToCents ((double) n / (double) d);
            rep = juce::String (n) + "/" + juce::String (d);
        }
        else if (t.containsOnly ("0123456789.-"))
        {
            if (t.containsChar ('.')) { c = t.getDoubleValue(); rep = juce::String (c, 5); }
            else
            {
                const auto n = t.getLargeIntValue();
                if (n <= 0) { error = "Can't read \"" + t + "\"."; return {}; }
                c = ratioToCents ((double) n);
                rep = juce::String (n) + "/1";
            }
        }
        else { error = "Can't read \"" + t + "\": use ratios like 3/2 or cents like 701.96."; return {}; }
        if (c <= 0.001) continue;   // 1/1 — ступень 0, её в .scl не пишут
        tones.push_back ({ c, rep });
    }
    std::sort (tones.begin(), tones.end(), [] (auto& a, auto& b) { return a.first < b.first; });
    tones.erase (std::unique (tones.begin(), tones.end(), [] (auto& a, auto& b) { return std::abs (a.first - b.first) < 0.001; }), tones.end());
    if (tones.empty()) { error = "Enter at least one interval, for example 3/2 2/1."; return {}; }
    juce::String scl = "! ratios\nRatios\n " + juce::String ((int) tones.size()) + "\n!\n";
    for (auto& t : tones) scl << " " << t.second << "\n";
    return scl;
}

TuningPtr defaultTuning()
{
    static TuningPtr t = []
    {
        TuningSpec spec;
        juce::String err;
        return buildTuning (spec, err);
    }();
    return t;
}

TuningPtr buildTuning (const TuningSpec& spec, juce::String& error)
{
    try
    {
        const double rootC = rootCentsFor (spec);
        const double rootHz = centsToHz (rootC);
        Tunings::Scale scale;
        Tunings::KeyboardMapping kbm = Tunings::startScaleOnAndTuneNoteTo (60 + spec.root, 60 + spec.root, rootHz);
        juce::String name, detail;
        switch (spec.source)
        {
            case TuningSource::EDO12:
            case TuningSource::MTS:   // без мастера — 12-EDO
                scale = Tunings::evenTemperament12NoteScale();
                kbm = Tunings::tuneNoteTo (69, spec.refHz);
                name = "12-EDO";
                break;
            case TuningSource::EDO:
            {
                const int n = juce::jlimit (1, 96, spec.edo);
                scale = Tunings::evenDivisionOfSpanByM (2, n);
                name = juce::String (n) + "-EDO";
                break;
            }
            case TuningSource::Ratios:
            {
                const auto scl = ratiosToScl (spec.ratios.isNotEmpty() ? spec.ratios : defaultRatios(), error);
                if (scl.isEmpty()) throw Tunings::TuningError (error.toStdString());
                scale = Tunings::parseSCLData (scl.toStdString());
                name = "Ratios";
                break;
            }
            case TuningSource::Scala:
            {
                if (spec.scl.trim().isEmpty()) { error = "No .scl file loaded."; throw Tunings::TuningError ("no scl"); }
                scale = Tunings::parseSCLData (spec.scl.toStdString());
                if (spec.kbm.trim().isNotEmpty()) kbm = Tunings::parseKBMData (spec.kbm.toStdString());
                name = spec.sclName.isNotEmpty() ? spec.sclName : juce::String (scale.name);
                if (name.isEmpty()) name = "Scala";
                detail = spec.kbmName;
                break;
            }
        }
        if (scale.count <= 0) throw Tunings::TuningError ("empty scale");
        Tunings::Tuning tuning (scale, kbm, true);
        tuning = tuning.withSkippedNotesInterpolated();
        // корень для ступеней: нота начала шкалы (в .kbm — middleNote)
        const bool twelve = spec.source == TuningSource::EDO12 || spec.source == TuningSource::MTS;
        double rc = rootC;
        if (twelve) rc = hzToCents (spec.refHz * std::exp2 ((60 - 69) / 12.0));                                  // C3
        else if (spec.source == TuningSource::Scala && spec.kbm.trim().isNotEmpty()) rc = hzToCents (tuning.frequencyForMidiNote (kbm.middleNote));
        auto s = fromTunings (scale, tuning, rc);
        if (twelve)
            for (int i = 0; i < 12 && i < (int) s->degreeNames.size(); ++i) s->degreeNames[(size_t) i] = kNames12[i];
        else if (spec.source == TuningSource::EDO)
            for (size_t i = 0; i < s->degreeNames.size(); ++i) s->degreeNames[i] = juce::String ((int) i);
        s->name = name;
        s->detail = detail.isNotEmpty() ? detail : juce::String (s->degrees) + " steps" + juce::String::fromUTF8 (" \xc2\xb7 ")
                                                    + (std::abs (s->periodCents - 1200.0) < 0.01 ? juce::String ("octave") : juce::String (s->periodCents, 1) + juce::String::fromUTF8 (" \xc2\xa2 period"));
        return s;
    }
    catch (const std::exception& e)
    {
        if (error.isEmpty()) error = juce::String ("Can't read the tuning: ") + e.what();
        if (spec.source == TuningSource::EDO12) return nullptr;
        TuningSpec fallback;
        fallback.refHz = spec.refHz;
        juce::String ignore;
        return buildTuning (fallback, ignore);
    }
}

TuningPtr buildTuningFromNoteHz (const std::array<double, 128>& hz, const juce::String& name, double periodCents)
{
    // строй от мастера MTS-ESP: ступени — сами 128 нот (их и тянет Snap), период — от мастера
    auto s = std::make_shared<TuningSnapshot>();
    std::vector<double> c;
    for (int n = 0; n < 128; ++n)
    {
        s->noteCents[(size_t) n] = hzToCents (hz[(size_t) n]);
        c.push_back (s->noteCents[(size_t) n]);
    }
    std::sort (c.begin(), c.end());
    for (double x : c)
        if (x >= 0.0 && x <= TuningSnapshot::kTop && (s->numSteps == 0 || x - (double) s->steps[(size_t) s->numSteps - 1] > 0.01))
            s->steps[(size_t) s->numSteps++] = (float) x;
    s->periodCents = periodCents > 1.0 ? periodCents : 1200.0;
    s->rootCents = s->noteCents[60];
    for (double x : c)
    {
        const double rel = x - s->rootCents;
        if (rel >= -0.001 && rel < s->periodCents - 0.01) { s->degreeCents.push_back (juce::jmax (0.0, rel)); s->degreeNames.push_back (centsLabel (rel)); }
    }
    if (s->degreeCents.empty()) { s->degreeCents.push_back (0.0); s->degreeNames.push_back ("0"); }
    s->degrees = (int) s->degreeCents.size();
    s->meanStep = s->periodCents / juce::jmax (1, s->degrees);
    s->name = name.isNotEmpty() ? name : juce::String ("MTS-ESP");
    s->detail = "from MTS-ESP" + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (s->degrees) + " steps";
    return s;
}

// ===========================================================================
namespace
{
struct RatioList
{
    juce::StringArray names;
    std::vector<double> values;
    int unison = 0;
    RatioList()
    {
        const int up[][2] = { { 16, 15 }, { 10, 9 }, { 9, 8 }, { 8, 7 }, { 7, 6 }, { 6, 5 }, { 5, 4 }, { 9, 7 }, { 4, 3 }, { 11, 8 }, { 7, 5 },
                              { 10, 7 }, { 3, 2 }, { 14, 9 }, { 8, 5 }, { 13, 8 }, { 5, 3 }, { 7, 4 }, { 16, 9 }, { 9, 5 }, { 11, 6 }, { 15, 8 }, { 2, 1 } };
        const int n = (int) (sizeof (up) / sizeof (up[0]));
        for (int i = n - 1; i >= 0; --i) { names.add (juce::String (up[i][1]) + "/" + juce::String (up[i][0])); values.push_back ((double) up[i][1] / up[i][0]); }
        unison = names.size();
        names.add ("1/1");
        values.push_back (1.0);
        for (int i = 0; i < n; ++i) { names.add (juce::String (up[i][0]) + "/" + juce::String (up[i][1])); values.push_back ((double) up[i][0] / up[i][1]); }
    }
};
const RatioList& ratios() { static RatioList r; return r; }
}

const juce::StringArray& ratioNames() { return ratios().names; }
double ratioValue (int i) { return ratios().values[(size_t) juce::jlimit (0, (int) ratios().values.size() - 1, i)]; }
int unisonRatioIndex() { return ratios().unison; }
int ratioIndex (int num, int den) { return ratioNames().indexOf (juce::String (num) + "/" + juce::String (den)); }

double intervalCents (IntervalUnit unit, double cents, int ratioIdx, int steps, const TuningSnapshot* t, double from) noexcept
{
    switch (unit)
    {
        case IntervalUnit::Cents: return cents;
        case IntervalUnit::Ratio: return ratioToCents (ratioValue (ratioIdx));
        case IntervalUnit::Steps:
            if (t == nullptr) return steps * 100.0;
            if (std::isfinite (from)) return t->shiftSteps (from, steps) - from;
            return steps * t->meanStep;
    }
    return 0.0;
}
} // namespace comma
