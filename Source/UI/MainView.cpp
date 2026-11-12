#include "MainView.h"

namespace comma
{
using LnF = CommaLookAndFeel;

void MainView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (currentTheme() == ThemeId::Oxygen ? Colours::surface : Colours::well);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);

    // круг строя: 12 ступеней, C сверху, по часовой
    const float rad = juce::jmin (r.getHeight() * 0.5f - 34.0f, 92.0f);
    const juce::Point<float> c (r.getX() + 40.0f + rad + 20.0f, r.getCentreY() + 6.0f);
    g.setColour (Colours::track);
    g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 1.0f);
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    g.setFont (LnF::mono (LnF::Type::footnote));
    for (int i = 0; i < 12; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 12.0f;
        const juce::Point<float> p (c.x + rad * std::sin (a), c.y - rad * std::cos (a));
        g.setColour (Colours::ink2);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (p));
        const juce::Point<float> t (c.x + (rad + 16.0f) * std::sin (a), c.y - (rad + 16.0f) * std::cos (a));
        g.drawText (names[i], juce::Rectangle<float> (28.0f, 14.0f).withCentre (t), juce::Justification::centred);
    }

    // сведения слева сверху (плотная плашка)
    const juce::String info = "12-EDO" + midDot() + "A4 = 440 Hz";
    g.setFont (LnF::mono (LnF::Type::footnote));
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), info) + 20.0f;
    const auto plate = juce::Rectangle<float> (r.getX() + 12.0f, r.getY() + 12.0f, w, 22.0f);
    LnF::drawPlate (g, plate, 8.0f, currentTheme() == ThemeId::Oxygen ? Colours::well : Colours::surface);
    g.setColour (Colours::ink2);
    g.drawText (info, plate, juce::Justification::centred);

    // место анализатора
    auto right = r.withTrimmedLeft (c.x + rad + 70.0f - r.getX()).reduced (24.0f, 40.0f);
    g.setColour (Colours::outline);
    g.drawHorizontalLine ((int) right.getCentreY(), right.getX(), right.getRight());
    g.setColour (Colours::ink2);
    g.setFont (LnF::font (LnF::Type::body));
    g.drawText ("The tuning circle and the cents analyzer come alive in stage 1, MPE voices in stage 3.",
                right.withTrimmedTop (right.getHeight() * 0.5f + 8.0f).withHeight (20.0f), juce::Justification::centred, true);
    juce::ignoreUnused (proc);
}
} // namespace comma
