#include "Modules.h"

namespace comma
{
namespace
{
int psolaLatency (double sr) { return 3 * (int) std::ceil (sr / 60.0) + 4; }   // как в dsp::Psola::prepare (sr, 60)
}

// ===========================================================================
// Harmonizer
void HarmonizerModule::prepare (double s, int)
{
    sr = s;
    for (auto& v : shift) for (auto& x : v) x.prepare (sr, 70.0f, 0.0f);
    for (auto& p : psola) p.prepare (sr, 60.0f);
    yin.prepare (sr);
    for (auto& r : ratio) r.setTime (25.0, sr);
    reset();
}

void HarmonizerModule::reset()
{
    for (auto& v : shift) for (auto& x : v) x.reset();
    for (auto& p : psola) p.reset();
    yin.reset();
    for (auto& r : ratio) r.y = 1.0f;
    lastLatency = getLatency();
}

int HarmonizerModule::computeLatency (const float* v, double s) const
{
    return v != nullptr && juce::roundToInt (v[P::HarMode]) == 1 ? psolaLatency (s) : 0;
}

void HarmonizerModule::process (float* const* ch, int nc, int n, const ProcessContext& c)
{
    if (getLatency() != lastLatency) reset();
    const bool quality = getLatency() > 0;
    // шаги строя — от высоты соло-голоса (в режиме Quality), иначе средним шагом
    const double from = yin.isVoiced() ? hzToCents (yin.getHz()) : std::numeric_limits<double>::quiet_NaN();
    const float r1 = (float) centsToRatio (intervalOf (c, P::Har1Unit, P::Har1Cents, P::Har1Ratio, P::Har1Steps, from));
    const float r2 = (float) centsToRatio (intervalOf (c, P::Har2Unit, P::Har2Cents, P::Har2Ratio, P::Har2Steps, from));
    const float l1 = c.v (P::Har1Level) * 0.01f, l2 = c.v (P::Har2Level) * 0.01f;
    float* L = ch[0];
    float* R = nc > 1 ? ch[1] : nullptr;
    for (int i = 0; i < n; ++i)
    {
        const float q1 = ratio[0].next (r1), q2 = ratio[1].next (r2);
        const float m = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
        yin.push (m);   // высота нужна в обоих режимах: Quality — для PSOLA, Low latency — окно в целое число периодов
        const float per = yin.getPeriod();
        if (quality)
        {
            const float y = l1 * psola[0].process (m, per, q1) + l2 * psola[1].process (m, per, q2);
            L[i] = y;
            if (R != nullptr) R[i] = y;
        }
        else
        {
            for (int k = 0; k < (R != nullptr ? 2 : 1); ++k)
            {
                float& x = ch[k][i];
                x = l1 * shift[0][(size_t) k].process (x, q1, 40.0f, 0.0f, per) + l2 * shift[1][(size_t) k].process (x, q2, 40.0f, 0.0f, per);
            }
        }
    }
}

// ===========================================================================
// Autotune
void AutotuneModule::prepare (double s, int)
{
    sr = s;
    yin.prepare (sr);
    psola.prepare (sr, 60.0f);
    for (auto& x : shift) x.prepare (sr, 40.0f, 0.0f);
    reset();
}

void AutotuneModule::reset()
{
    yin.reset();
    psola.reset();
    for (auto& x : shift) x.reset();
    corr = 0.0;
    lastLatency = getLatency();
}

int AutotuneModule::computeLatency (const float* v, double s) const
{
    return v != nullptr && juce::roundToInt (v[P::AutMode]) == 1 ? psolaLatency (s) : 0;
}

void AutotuneModule::process (float* const* ch, int nc, int n, const ProcessContext& c)
{
    if (getLatency() != lastLatency) reset();
    const bool quality = getLatency() > 0;
    const double amount = c.v (P::AutAmount) * 0.01;
    const double speedMs = c.v (P::AutSpeed);
    const double k = speedMs <= 0.5 ? 1.0 : 1.0 - std::exp (-1.0 / (0.001 * speedMs * sr));
    const bool notes = c.i (P::AutTarget) == 1;
    float* L = ch[0];
    float* R = nc > 1 ? ch[1] : nullptr;
    double goal = corr;
    for (int i = 0; i < n; ++i)
    {
        const float m = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
        yin.push (m);
        if (yin.justAnalysed())
        {
            if (yin.isVoiced())
            {
                const double det = hzToCents (yin.getHz());
                double target = det;
                if (! notes) target = c.tuning != nullptr ? c.tuning->nearest (det) : std::round (det / 100.0) * 100.0;
                else if (c.notes != nullptr)
                {
                    // ближайшая держащаяся нота, в любом периоде строя
                    const double period = c.tuning != nullptr ? c.tuning->periodCents : 1200.0;
                    double best = 1.0e9;
                    for (int v = 0; v < kMaxVoices; ++v)
                    {
                        const auto& vo = c.notes->voice (v);
                        if (! vo.held) continue;
                        const double rel = std::remainder (vo.pitch - det, period);
                        if (std::abs (rel) < std::abs (best)) best = rel;
                    }
                    if (best < 1.0e8) target = det + best;
                }
                goal = (target - det) * amount;
                detected.store (yin.getHz());
            }
            else { goal = 0.0; detected.store (0.0f); }
        }
        corr += (goal - corr) * k;
        const float r = (float) centsToRatio (corr);
        if (quality)
        {
            const float y = psola.process (m, yin.getPeriod(), r);
            L[i] = y;
            if (R != nullptr) R[i] = y;
        }
        else
            for (int ci = 0; ci < (R != nullptr ? 2 : 1); ++ci) ch[ci][i] = shift[(size_t) ci].process (ch[ci][i], r, 25.0f, 0.0f, yin.getPeriod());
    }
    correction.store ((float) corr);
}

// ===========================================================================
// Resonator
void ResonatorModule::prepare (double s, int)
{
    sr = s;
    reset();
}

void ResonatorModule::reset()
{
    for (int v = 0; v < kMaxVoices; ++v)
    {
        voices[(size_t) v] = V();
        voices[(size_t) v].pan = (float) (((v * 7) % 9) - 4) / 16.0f;
    }
}

void ResonatorModule::process (float* const* ch, int nc, int n, const ProcessContext& c)
{
    float* L = ch[0];
    float* R = nc > 1 ? ch[1] : nullptr;
    if (c.notes == nullptr || c.matrix == nullptr)
    {
        for (int k = 0; k < nc; ++k) juce::FloatVectorOperations::clear (ch[k], n);
        return;
    }
    const auto& notes = *c.notes;
    const auto& mx = *c.matrix;
    const bool onScale = c.b (P::ResScale) && c.tuning != nullptr;
    const int partials = juce::jlimit (1, kPartials, c.i (P::ResPartials));
    int shown = 0;

    // вход — моно, выход пишется поверх
    for (int i = 0; i < n; ++i)
    {
        const float x = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
        float outL = 0.0f, outR = 0.0f;
        const int sub = (c.blockOffset + i) / kControlBlock;
        const bool newSub = i == 0 || (c.blockOffset + i) % kControlBlock == 0;
        for (int v = 0; v < kMaxVoices; ++v)
        {
            auto& vo = voices[(size_t) v];
            const auto& nv = notes.voice (v);
            if (! nv.active && vo.env < 1.0e-5f)
            {
                if (vo.count > 0) { for (auto& rs : vo.r) rs.y1 = rs.y2 = 0.0f; vo.count = 0; vo.env = 0.0f; }
                continue;
            }
            if (newSub)
            {
                // коэффициенты — раз в 32 семпла, по высоте нотного слоя и модуляции этого голоса
                const double pitch = notes.pitchAt (v, sub);
                const bool gate = notes.gateAt (v, sub) && nv.active;
                const double decay = juce::jmax (0.02, (double) mx.voiceValue (notes, P::ResDecay, v) * 0.001);
                const float bright = mx.voiceValue (notes, P::ResBright, v) * 0.01f;
                const double stretch = mx.voiceValue (notes, P::ResStretch, v) * 0.01;
                const float gain = juce::Decibels::decibelsToGain (mx.voiceValue (notes, P::ResDrive, v));
                const float vel = mx.voiceValue (notes, P::ResVelocity, v) * 0.01f;
                const double release = juce::jmax (0.02, (double) mx.voiceValue (notes, P::ResRelease, v) * 0.001);
                const double r = std::exp (-6.907755 / (decay * sr));
                const float makeup = (float) juce::jmin (16.0, 0.25 / std::sqrt (1.0 - r));
                const double f0 = centsToHz (pitch), e = 1.0 + 0.25 * stretch;
                vo.count = 0;
                for (int k = 1; k <= partials; ++k)
                {
                    double fk = f0 * std::pow ((double) k, e);
                    if (onScale && k > 1) fk = centsToHz (c.tuning->nearest (hzToCents (fk)));
                    if (fk > 0.45 * sr) break;
                    const double w = dsp::kTwoPi * fk / sr;
                    auto& rs = vo.r[(size_t) vo.count++];
                    rs.a1 = (float) (2.0 * r * std::cos (w));
                    rs.a2 = (float) (-r * r);
                    const float amp = std::pow ((float) k, -(1.0f - bright) * 1.5f);
                    rs.g = (float) ((1.0 - r) * std::sqrt (1.0 - 2.0 * r * std::cos (2.0 * w) + r * r)) * amp * makeup * gain;
                }
                vo.level = gate ? (1.0f - vel) + vel * nv.strike : 0.0f;
                const double tc = gate ? 0.002 : release / 6.9;
                vo.envCoef = (float) std::exp (-1.0 / (tc * sr));
            }
            vo.env = vo.level + vo.envCoef * (vo.env - vo.level);
            float y = 0.0f;
            for (int k = 0; k < vo.count; ++k)
            {
                auto& rs = vo.r[(size_t) k];
                const float out = rs.g * x + rs.a1 * rs.y1 + rs.a2 * rs.y2;
                rs.y2 = rs.y1;
                rs.y1 = out;
                y += out;
            }
            y *= vo.env;
            outL += y * (0.5f - vo.pan);
            outR += y * (0.5f + vo.pan);
            if (i == 0) ++shown;
        }
        L[i] = R != nullptr ? 2.0f * outL : outL + outR;
        if (R != nullptr) R[i] = 2.0f * outR;
    }
    shownVoices.store (shown);
}
} // namespace comma
