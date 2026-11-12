#include "Modules.h"

namespace comma
{
std::unique_ptr<Module> createModule (int m)
{
    switch ((ModuleId) m)
    {
        case ModuleId::Doubler:    return std::make_unique<DoublerModule>();
        case ModuleId::Harmonizer: return std::make_unique<HarmonizerModule>();
        case ModuleId::Staircase:  return std::make_unique<StaircaseModule>();
        case ModuleId::Autotune:   return std::make_unique<AutotuneModule>();
        case ModuleId::Resonator:  return std::make_unique<ResonatorModule>();
        case ModuleId::Shifter:    return std::make_unique<ShifterModule>();
        case ModuleId::Varispeed:  return std::make_unique<VarispeedModule>();
        case ModuleId::Spectral:   return std::make_unique<SpectralModule>();
    }
    return std::make_unique<Module>();
}

namespace
{
inline float softClip (float x) noexcept { return std::abs (x) < 1.0f ? x : std::tanh (x); }
}

// ===========================================================================
// Doubler
void DoublerModule::prepare (double s, int)
{
    sr = s;
    for (auto& x : shift) x.prepare (sr, 70.0f, 35.0f);
    reset();
}

void DoublerModule::reset()
{
    for (auto& x : shift) x.reset();
    for (auto& f : focus) f.reset();
}

void DoublerModule::process (float* const* ch, int nc, int n, const ProcessContext& c)
{
    // шаги без высоты входа — средний шаг строя
    const double cents = intervalOf (c, P::DblUnit, P::DblCents, P::DblRatio, P::DblSteps);
    const float up = (float) centsToRatio (cents), down = 1.0f / up;
    const float base = c.v (P::DblDelay) * 0.001f * (float) sr;
    const float win = c.v (P::DblWindow);
    const float w = c.v (P::DblWidth) * 0.01f, a = 0.5f + 0.5f * w;
    for (auto& f : focus) f.set (dsp::Biquad::HighPass, sr, c.v (P::DblFocus), 0.707);
    float* L = ch[0];
    float* R = nc > 1 ? ch[1] : ch[0];
    for (int i = 0; i < n; ++i)
    {
        const float l = focus[0].process (shift[0].process (L[i], up, win, base));
        const float r = focus[1].process (shift[1].process (R[i], down, win, base));
        L[i] = a * l + (1.0f - a) * r;
        if (nc > 1) R[i] = a * r + (1.0f - a) * l;
        else L[i] = 0.5f * (l + r);
    }
}

// ===========================================================================
// Staircase
void StaircaseModule::prepare (double s, int)
{
    sr = s;
    for (auto& l : line) l.prepare ((int) (2.6 * sr));
    for (auto& x : shift) x.prepare (sr, 70.0f, 0.0f);
    time.setTime (120.0, sr);
    reset();
}

void StaircaseModule::reset()
{
    for (auto& l : line) l.clear();
    for (auto& x : shift) x.reset();
    for (auto& t : tone) t.reset();
    time.y = 0.0f;
}

void StaircaseModule::process (float* const* ch, int nc, int n, const ProcessContext& c)
{
    const double seconds = c.b (P::StaSync) ? 60.0 / juce::jmax (20.0, c.bpm) * noteDivBeats (c.i (P::StaDiv)) : c.v (P::StaTime) * 0.001;
    const float target = (float) (juce::jlimit (0.02, 2.5, seconds) * sr);
    if (time.y <= 0.0f) time.y = target;
    const float r = (float) centsToRatio (intervalOf (c, P::StaUnit, P::StaCents, P::StaRatio, P::StaSteps));
    const float fb = c.v (P::StaFeedback) * 0.01f;
    const float win = c.v (P::StaWindow), comp = 0.5f * win * 0.001f * (float) sr;   // шифтер в среднем запаздывает на пол-окна
    for (auto& t : tone) t.set (dsp::Biquad::LowPass, sr, c.v (P::StaTone), 0.707);
    for (int i = 0; i < n; ++i)
    {
        const float d = juce::jmax (4.0f, time.next (target) - comp);
        for (int k = 0; k < juce::jmin (2, nc); ++k)
        {
            const float y = line[(size_t) k].read (d);
            const float s = tone[(size_t) k].process (shift[(size_t) k].process (y, r, win));
            line[(size_t) k].push (ch[k][i] + softClip (fb * s));
            ch[k][i] = s;
        }
    }
}

// ===========================================================================
// Shifter
void ShifterModule::prepare (double s, int)
{
    sr = s;
    hz.setTime (30.0, sr);
    reset();
}

void ShifterModule::reset()
{
    for (auto& h : hil) h.reset();
    phase = { 0.0, 0.0 };
    fb = { 0.0f, 0.0f };
}

void ShifterModule::process (float* const* ch, int nc, int n, const ProcessContext& c)
{
    // Δf = BPM / 60 / (долей в цикле): например, цикл 1/4 — биения раз в долю
    const double f = c.b (P::ShiSync) ? (c.i (P::ShiDir) == 0 ? 1.0 : -1.0) * juce::jmax (20.0, c.bpm) / 60.0 / cycleDivBeats (c.i (P::ShiDiv))
                                      : (double) c.v (P::ShiShift);
    shownHz.store ((float) f);
    const float g = c.v (P::ShiFeedback) * 0.01f;
    const float spread = c.v (P::ShiSpread) * 0.01f;
    for (int i = 0; i < n; ++i)
    {
        const double hzL = hz.next ((float) f);
        const double hzs[2] = { hzL, hzL * (1.0 - 2.0 * spread) };
        for (int k = 0; k < juce::jmin (2, nc); ++k)
        {
            float re, im;
            hil[(size_t) k].process (ch[k][i] + softClip (g * fb[(size_t) k]), re, im);
            phase[(size_t) k] += dsp::kTwoPi * hzs[k] / sr;
            if (phase[(size_t) k] > dsp::kTwoPi) phase[(size_t) k] -= dsp::kTwoPi;
            if (phase[(size_t) k] < 0.0) phase[(size_t) k] += dsp::kTwoPi;
            const float y = re * (float) std::cos (phase[(size_t) k]) + im * (float) std::sin (phase[(size_t) k]);   // знак — сдвиг вверх при f > 0
            fb[(size_t) k] = y;
            ch[k][i] = y;
        }
    }
}

// ===========================================================================
// Varispeed
void VarispeedModule::prepare (double s, int)
{
    sr = s;
    for (auto& t : tape) t.prepare ((int) (8.5 * sr));
    for (auto& w : wobble) w.prepare ((int) (0.12 * sr));
    for (auto& x : shift) x.prepare (sr, 90.0f, 0.0f);
    depthW.setTime (200.0, sr);
    depthF.setTime (200.0, sr);
    cents.setTime (20.0, sr);
    shiftMix.setTime (20.0, sr);
    reset();
}

void VarispeedModule::reset()
{
    for (auto& t : tape) t.clear();
    for (auto& w : wobble) w.clear();
    for (auto& x : shift) x.reset();
    rate = 1.0;
    tapeD = 0.0;
    xfade = -1.0;
    depthW.y = depthF.y = cents.y = shiftMix.y = 0.0f;
}

void VarispeedModule::process (float* const* ch, int nc, int n, const ProcessContext& c)
{
    const bool brake = c.b (P::VarBrake);
    const double brakeSec = juce::jmax (0.05, (double) c.v (P::VarBrakeTime) * 0.001);
    // wow 0.7 Гц и flutter ~9 Гц: амплитуда задержки A = (2^(c/1200) − 1) / ω
    const float wowA = (float) ((std::exp2 (c.v (P::VarWow) / 1200.0) - 1.0) / (dsp::kTwoPi * 0.7) * sr);
    const float flA = (float) ((std::exp2 (c.v (P::VarFlutter) / 1200.0) - 1.0) / (dsp::kTwoPi * 9.0) * sr);
    const double beats = cycleDivBeats (c.i (P::VarRampLen));
    const float ramp = c.v (P::VarRamp), speed = c.v (P::VarSpeed);
    for (int i = 0; i < n; ++i)
    {
        // тормоз ленты: скорость падает до нуля, на отпускании — разгон и сшивка с живым сигналом
        if (brake) { rate = juce::jmax (0.0, rate - 1.0 / (brakeSec * sr)); xfade = -1.0; }
        else if (rate < 1.0) rate = juce::jmin (1.0, rate + 2.0 / (brakeSec * sr));
        tapeD = juce::jmin ((double) tape[0].capacity() - 8.0, tapeD + (1.0 - rate));
        if (! brake && rate >= 1.0 && tapeD > 0.0 && xfade < 0.0) xfade = 0.0;
        const float gate = (float) juce::jlimit (0.0, 1.0, rate / 0.03);

        // рампа: пила по долям при играющем транспорте, иначе — свободная
        double u;
        if (c.playing) { const double pos = c.ppq + (double) i / sr * c.bpm / 60.0; u = pos / beats - std::floor (pos / beats); }
        else { freeRamp.next (c.bpm / 60.0 / beats, sr); u = freeRamp.phase; }
        const float pitchCents = cents.next (speed + ramp * (float) u);
        const float r = (float) centsToRatio (pitchCents);
        shiftMix.next (std::abs (pitchCents) > 0.5f ? 1.0f : 0.0f);

        const float dw = depthW.next (wowA), df = depthF.next (flA);
        const float mod = dw * (1.0f + wow.next (0.7, sr)) + df * (1.0f + 0.6f * flutter.next (9.0, sr) + 0.4f * flutter2.next (13.3, sr) * 0.5f);
        for (int k = 0; k < juce::jmin (2, nc); ++k)
        {
            tape[(size_t) k].push (ch[k][i]);
            float x = tapeD > 0.0 ? tape[(size_t) k].read ((float) tapeD + 3.0f) : ch[k][i];
            if (xfade >= 0.0) x = (float) ((1.0 - xfade) * x + xfade * ch[k][i]);
            x *= gate;
            wobble[(size_t) k].push (x);
            x = wobble[(size_t) k].read (3.0f + mod);
            // без сдвига шифтер не нужен (две головки на одной задержке дают гребёнку): плавный переход
            const float shifted = shift[(size_t) k].process (x, r, 60.0f);
            ch[k][i] = x + shiftMix.y * (shifted - x);
        }
        if (xfade >= 0.0 && (xfade += 1.0 / (0.03 * sr)) >= 1.0) { xfade = -1.0; tapeD = 0.0; }
    }
}
} // namespace comma
