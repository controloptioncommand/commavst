#include "Randomizer.h"

namespace comma
{
juce::String RandomizerOptions::name (Group g)
{
    switch (g)
    {
        case Mix:       return "Mix";
        case Order:     return "Slot order and parallel groups";
        case NumGroups: break;
    }
    return {};
}

juce::String RandomizerOptions::prefKey (Group g)
{
    static const char* keys[] = { "mix", "order" };
    return juce::String ("dice_") + keys[(int) g];
}

void randomizePatch (CommaProcessor& p, juce::Random& rng, const RandomizerOptions& opt)
{
    using G = RandomizerOptions;
    // Случайные числа тянем всегда, даже для запрещённых групп: последовательность не зависит от настроек
    auto set = [&p, &opt] (G::Group g, const juce::String& id, float v)
    {
        if (! opt.allows (g)) return;
        if (auto* prm = p.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };

    p.undo.beginNewTransaction();
    auto l = p.getLayout();
    for (int i = 0; i < l.count; ++i)
        set (G::Mix, moduleParam (l.module[(size_t) i], "mix"), 40.0f + 60.0f * rng.nextFloat());

    // порядок: перемешать (Фишер — Йейтс) и раздать «‖» с вероятностью 1/4
    auto shuffled = l;
    for (int i = shuffled.count - 1; i > 0; --i)
        std::swap (shuffled.module[(size_t) i], shuffled.module[(size_t) rng.nextInt (i + 1)]);
    for (int i = 0; i < shuffled.count; ++i) shuffled.parallel[(size_t) i] = i > 0 && rng.nextFloat() < 0.25f;
    if (opt.allows (G::Order) && shuffled != l) p.setLayout (shuffled);
}
} // namespace comma
