#include "Randomizer.h"

namespace comma
{
juce::String RandomizerOptions::name (Group g)
{
    switch (g)
    {
        case Mix:       return "Mix";
        case Order:     return "Slot order and parallel groups";
        case Modules:   return "Module settings";
        case NumGroups: break;
    }
    return {};
}

juce::String RandomizerOptions::prefKey (Group g)
{
    static const char* keys[] = { "mix", "order", "modules" };
    return juce::String ("dice_") + keys[(int) g];
}

void randomizePatch (CommaProcessor& p, juce::Random& rng, const RandomizerOptions& opt)
{
    using G = RandomizerOptions;
    auto uni = [&rng] (float a, float b) { return a + (b - a) * rng.nextFloat(); };
    auto logUni = [&rng] (float a, float b) { return a * std::pow (b / a, rng.nextFloat()); };
    // Случайные числа тянем всегда, даже для запрещённых групп и модулей не в rack'е:
    // последовательность не зависит от настроек
    const auto l = p.getLayout();
    auto set = [&p, &opt, &l] (G::Group g, int prm, float v)
    {
        if (! opt.allows (g)) return;
        const int m = spec (prm).module;
        if (m >= 0 && ! l.contains (m)) return;
        p.setParam (prm, v);
    };

    p.undo.beginNewTransaction();
    for (int m = 0; m < kNumModules; ++m)
        set (G::Mix, P::moduleMix (m), uni (40.0f, 100.0f));

    // Doubler
    set (G::Modules, P::DblCents, logUni (3.0f, 35.0f));
    set (G::Modules, P::DblDelay, uni (0.0f, 22.0f));
    set (G::Modules, P::DblFocus, logUni (40.0f, 600.0f));
    set (G::Modules, P::DblWidth, uni (50.0f, 100.0f));
    set (G::Modules, P::DblWindow, uni (22.0f, 55.0f));
    // Harmonizer: отношения из консонансов
    {
        static const int ratios[][2] = { { 5, 4 }, { 6, 5 }, { 4, 3 }, { 3, 2 }, { 5, 3 }, { 7, 4 }, { 8, 5 }, { 2, 1 }, { 1, 2 }, { 2, 3 }, { 3, 4 } };
        const auto& a = ratios[rng.nextInt (11)];
        const auto& b = ratios[rng.nextInt (11)];
        set (G::Modules, P::Har1Ratio, (float) ratioIndex (a[0], a[1]));
        set (G::Modules, P::Har2Ratio, (float) ratioIndex (b[0], b[1]));
        set (G::Modules, P::Har1Level, uni (50.0f, 100.0f));
        set (G::Modules, P::Har2Level, uni (30.0f, 90.0f));
    }
    // Staircase: шаг ±1–3 ступени
    {
        int st = 1 + rng.nextInt (3);
        if (rng.nextFloat() < 0.3f) st = -st;
        set (G::Modules, P::StaSteps, (float) st);
        set (G::Modules, P::StaDiv, (float) (4 + rng.nextInt (6)));
        set (G::Modules, P::StaTime, logUni (120.0f, 700.0f));
        set (G::Modules, P::StaFeedback, uni (35.0f, 75.0f));
        set (G::Modules, P::StaTone, logUni (2500.0f, 14000.0f));
        set (G::Modules, P::StaWindow, uni (25.0f, 55.0f));
    }
    // Autotune
    set (G::Modules, P::AutSpeed, rng.nextFloat() < 0.3f ? 0.0f : logUni (10.0f, 200.0f));
    set (G::Modules, P::AutAmount, uni (60.0f, 100.0f));
    // Resonator
    set (G::Modules, P::ResDecay, logUni (300.0f, 4000.0f));
    set (G::Modules, P::ResBright, uni (20.0f, 90.0f));
    set (G::Modules, P::ResPartials, (float) (3 + rng.nextInt (6)));
    set (G::Modules, P::ResStretch, uni (-20.0f, 30.0f));
    set (G::Modules, P::ResRelease, logUni (200.0f, 3000.0f));
    // Shifter: медленные сдвиги — чаще
    {
        float hz = rng.nextFloat() < 0.6f ? logUni (0.2f, 12.0f) : logUni (12.0f, 400.0f);
        if (rng.nextBool()) hz = -hz;
        set (G::Modules, P::ShiShift, hz);
        set (G::Modules, P::ShiFeedback, uni (0.0f, 70.0f));
        set (G::Modules, P::ShiSpread, uni (0.0f, 100.0f));
    }
    // Varispeed
    set (G::Modules, P::VarSpeed, rng.nextFloat() < 0.6f ? 0.0f : uni (-100.0f, 100.0f));
    set (G::Modules, P::VarRamp, rng.nextFloat() < 0.7f ? 0.0f : uni (-300.0f, 300.0f));
    set (G::Modules, P::VarWow, uni (0.0f, 40.0f));
    set (G::Modules, P::VarFlutter, uni (0.0f, 8.0f));
    // Spectral
    set (G::Modules, P::SpeAmount, uni (40.0f, 100.0f));

    // порядок: перемешать (Фишер — Йейтс) и раздать «‖» с вероятностью 1/4
    auto shuffled = l;
    for (int i = shuffled.count - 1; i > 0; --i)
        std::swap (shuffled.module[(size_t) i], shuffled.module[(size_t) rng.nextInt (i + 1)]);
    for (int i = 0; i < shuffled.count; ++i) shuffled.parallel[(size_t) i] = i > 0 && rng.nextFloat() < 0.25f;
    if (opt.allows (G::Order) && shuffled != l) p.setLayout (shuffled);
}
} // namespace comma
