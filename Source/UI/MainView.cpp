#include "MainView.h"

namespace comma
{
using LnF = CommaLookAndFeel;

namespace
{
void attachKnob (ParamKnob& k, CommaProcessor& p, int prm)
{
    k.attach (p.apvts, spec (prm).id);
    k.slider.setTitle (spec (prm).name);
}
}

// ===========================================================================
NotesPanel::NotesPanel (CommaProcessor& p)
    : proc (p), glideMode (p.apvts, spec (P::GlideMode).id, "Glide by"), curve (p.apvts, spec (P::GlideCurve).id, "Curve"),
      domain (p.apvts, spec (P::GlideDomain).id, "Glide in"), agg (p.apvts, spec (P::MpeAgg).id, "MPE for effects")
{
    attachKnob (glide, p, P::Glide); attachKnob (rate, p, P::GlideRate); attachKnob (snap, p, P::Snap); attachKnob (snapSpeed, p, P::SnapSpeed);
    attachKnob (character, p, P::Character); attachKnob (overshoot, p, P::Overshoot); attachKnob (vibrato, p, P::Vibrato);
    attachKnob (landing, p, P::Landing); attachKnob (bend, p, P::BendRange);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &glideMode, &curve, &domain, &agg, &glide, &snap, &snapSpeed, &character,
                                                                        &overshoot, &vibrato, &landing, &bend })
        addAndMakeVisible (c);
    addChildComponent (rate);
    refresh();
}

void NotesPanel::refresh()
{
    const bool byRate = juce::roundToInt (proc.getParam (P::GlideMode)) == 1;
    if (rate.isVisible() != byRate) { rate.setVisible (byRate); glide.setVisible (! byRate); }
}

void NotesPanel::resized()
{
    auto r = getLocalBounds();
    auto chips = r.removeFromTop (26);
    const int w = (chips.getWidth() - 3 * 8) / 4;
    for (auto* c : { (juce::Component*) &glideMode, (juce::Component*) &curve, (juce::Component*) &domain, (juce::Component*) &agg })
    {
        c->setBounds (chips.removeFromLeft (w));
        chips.removeFromLeft (8);
    }
    r.removeFromTop (6);
    layoutRow (r, { &glide, &snap, &snapSpeed, &character, &overshoot, &vibrato, &landing, &bend });
    rate.setBounds (glide.getBounds());
}

// ===========================================================================
MatrixPanel::DestPill::DestPill (CommaProcessor& p, int r) : MenuPill (juce::String::fromUTF8 ("\xe2\x86\x92")), proc (p), row (r)
{
    setTitle ("Mod " + juce::String (r + 1) + " destination");
    onOpen = [this] { open(); };
}

void MatrixPanel::DestPill::open()
{
    // назначения сгруппированы по модулям
    const auto& d = modDestinations();
    const int cur = juce::roundToInt (proc.getParam (P::modDst (row)));
    juce::PopupMenu m;
    m.addItem (1, "None", true, cur == 0);
    std::array<juce::PopupMenu, kNumModules> sub;
    for (int i = 1; i < (int) d.size(); ++i)
        sub[(size_t) spec (d[(size_t) i]).module].addItem (1 + i, spec (d[(size_t) i]).label, true, cur == i);
    for (int mo = 0; mo < kNumModules; ++mo) m.addSubMenu (moduleInfo (mo).name, sub[(size_t) mo]);
    juce::Component::SafePointer<DestPill> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
    {
        if (safe != nullptr && r > 0) safe->proc.setParam (P::modDst (safe->row), (float) (r - 1));
    });
}

MatrixPanel::Row::Row (CommaProcessor& p, int i)
    : src (p.apvts, spec (P::modSrc (i)).id, juce::String (i + 1)), dst (p, i)
{
    amount.setSliderStyle (juce::Slider::LinearHorizontal);
    amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    amount.setDoubleClickReturnValue (true, 0.0);
    amount.setTitle ("Mod " + juce::String (i + 1) + " amount");
    att = std::make_unique<APVTS::SliderAttachment> (p.apvts, spec (P::modAmt (i)).id, amount);
    value.setFont (LnF::mono (LnF::Type::footnote));
    value.getProperties().set ("dim", true);
    value.setJustificationType (juce::Justification::centredRight);
}

MatrixPanel::MatrixPanel (CommaProcessor& p) : proc (p)
{
    for (int i = 0; i < kNumModRows; ++i)
    {
        rows.push_back (std::make_unique<Row> (p, i));
        auto& r = *rows.back();
        for (juce::Component* c : std::initializer_list<juce::Component*> { &r.src, &r.dst, &r.amount, &r.value })
            addAndMakeVisible (c);
    }
    refresh();
}

void MatrixPanel::refresh()
{
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto& r = *rows[(size_t) i];
        const auto t = r.amount.getTextFromValue (r.amount.getValue());
        if (r.value.getText() != t) r.value.setText (t, juce::dontSendNotification);
        r.dst.setText (modDestName (juce::roundToInt (proc.getParam (P::modDst (i)))));
    }
}

void MatrixPanel::resized()
{
    auto r = getLocalBounds();
    const int colW = (r.getWidth() - 24) / 2;
    const int h = r.getHeight() / 4;
    for (int i = 0; i < (int) rows.size(); ++i)
    {
        auto& row = *rows[(size_t) i];
        auto line = juce::Rectangle<int> (r.getX() + (i / 4) * (colW + 24), r.getY() + (i % 4) * h, colW, h).withSizeKeepingCentre (colW, juce::jmin (26, h - 6));
        row.src.setBounds (line.removeFromLeft (104));
        line.removeFromLeft (6);
        row.dst.setBounds (line.removeFromLeft (156));
        line.removeFromLeft (8);
        row.value.setBounds (line.removeFromRight (44));
        row.amount.setBounds (line);
    }
}

// ===========================================================================
MainView::MainView (CommaProcessor& p) : proc (p), notes (p), matrix (p)
{
    setTitle ("Tuning view");
    tabs.setTitle ("View");
    tabs.setItems ({ { "Pitch", 0 }, { "Notes", 1 }, { "Matrix", 2 } });
    tabs.onChange = [this] (int t) { setTab (t); };
    addAndMakeVisible (tabs);
    addChildComponent (notes);
    addChildComponent (matrix);
    setTab (0);
}

void MainView::setTab (int t)
{
    tab = juce::jlimit (0, 2, t);
    tabs.setSelectedValue (tab);
    notes.setVisible (tab == 1);
    matrix.setVisible (tab == 2);
    repaint();
}

void MainView::tick()
{
    if (tab == 1) notes.refresh();
    if (tab == 2) matrix.refresh();
    repaint (circleArea);
    if (tab == 0) repaint (contentArea);
}

void MainView::resized()
{
    auto r = getLocalBounds().reduced (12);
    circleArea = r.removeFromLeft (250);
    r.removeFromLeft (16);
    rightArea = r;
    tabs.setBounds (r.removeFromTop (28).removeFromRight (270));
    r.removeFromTop (8);
    contentArea = r;
    notes.setBounds (r);
    matrix.setBounds (r);
}

void MainView::paintCircle (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto t = proc.getTuning();
    if (t == nullptr) return;
    auto circ = area;
    const float rad = juce::jmin (circ.getWidth(), circ.getHeight()) * 0.5f - 18.0f;
    const auto c = circ.getCentre();
    g.setColour (Colours::track);
    g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 1.0f);
    auto at = [&] (double cents, float rr)
    {
        double rel = std::fmod (cents - t->rootCents, t->periodCents);
        if (rel < 0.0) rel += t->periodCents;
        const float a = (float) (6.283185307179586 * rel / t->periodCents);
        return juce::Point<float> (c.x + rr * std::sin (a), c.y - rr * std::cos (a));
    };
    const int nd = (int) t->degreeCents.size();
    const bool labels = nd <= 24;
    g.setFont (LnF::mono (LnF::Type::footnote));
    for (int d = 0; d < nd; ++d)
    {
        const auto p = at (t->rootCents + t->degreeCents[(size_t) d], rad);
        g.setColour (Colours::ink2);
        const float dot = nd > 48 ? 3.0f : 5.0f;
        g.fillEllipse (juce::Rectangle<float> (dot, dot).withCentre (p));
        if (labels && d < (int) t->degreeNames.size())
            g.drawText (t->degreeNames[(size_t) d], juce::Rectangle<float> (40.0f, 14.0f).withCentre (at (t->rootCents + t->degreeCents[(size_t) d], rad + 15.0f)),
                        juce::Justification::centred);
    }
    // голоса MPE — точки цвета несущей внутри круга
    for (auto& v : proc.voiceView)
        if (v.held.load() != 0)
        {
            const auto p = at (v.pitch.load(), rad * 0.78f);
            g.setColour (Colours::carrier);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (p));
        }
    // высота входа — сигнальная стрелка от центра
    const float hz = proc.getInputHz();
    if (hz > 0.0f)
    {
        const auto p = at (hzToCents (hz), rad);
        g.setColour (Colours::signal);
        g.drawLine (juce::Line<float> (c, p), 2.0f);
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (p));
    }
    g.setColour (Colours::ink2);
    g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (c));
}

void MainView::paintPitch (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto t = proc.getTuning();
    auto left = area.removeFromLeft (area.getWidth() * 3 / 5);
    area.removeFromLeft (20);

    // высота входа и отклонение от ближайшей ступени
    const float hz = proc.getInputHz();
    juce::String big = juce::String::fromUTF8 ("\xe2\x80\x94"), small = "No pitch on the input";
    double dev = 0.0;
    if (hz > 0.0f && t != nullptr)
    {
        const double c = hzToCents (hz);
        const double n = t->nearest (c);
        dev = c - n;
        const int deg = t->degreeOf (c);
        const auto name = deg < (int) t->degreeNames.size() ? t->degreeNames[(size_t) deg] : juce::String (deg);
        big = name + "  " + formatCents (dev, 1);
        small = juce::String (hz, 1) + " Hz" + midDot() + "step " + juce::String (deg) + " of " + juce::String (t->degrees);
    }
    g.setColour (Colours::ink);
    g.setFont (LnF::mono (LnF::Type::title2, true));
    g.drawText (big, left.removeFromTop (24), juce::Justification::centredLeft);
    g.setColour (Colours::ink2);
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.drawText (small, left.removeFromTop (16), juce::Justification::centredLeft);
    left.removeFromTop (8);

    // шкала ±50 ¢ и история
    auto meter = left.removeFromTop (14).toFloat();
    g.setColour (Colours::track);
    g.fillRoundedRectangle (meter.withSizeKeepingCentre (meter.getWidth(), 4.0f), 2.0f);
    g.setColour (Colours::ink2);
    g.fillRect (meter.getCentreX() - 0.5f, meter.getY(), 1.0f, meter.getHeight());
    if (hz > 0.0f)
    {
        const float x = meter.getCentreX() + (float) juce::jlimit (-50.0, 50.0, dev) / 50.0f * meter.getWidth() * 0.5f;
        g.setColour (Colours::signal);
        g.fillRoundedRectangle (juce::jmin (x, meter.getCentreX()), meter.getCentreY() - 2.0f, std::abs (x - meter.getCentreX()), 4.0f, 2.0f);
        g.fillEllipse (x - 5.0f, meter.getCentreY() - 5.0f, 10.0f, 10.0f);
    }
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.setColour (Colours::ink2);
    auto scale = left.removeFromTop (14);
    g.drawText (minus() + "50", scale, juce::Justification::centredLeft);
    g.drawText ("0", scale, juce::Justification::centred);
    g.drawText ("+50", scale, juce::Justification::centredRight);
    left.removeFromTop (6);

    auto hist = left.toFloat();
    g.setColour (Colours::outline);
    g.drawHorizontalLine ((int) hist.getCentreY(), hist.getX(), hist.getRight());
    juce::Path path;
    bool started = false;
    for (int i = 0; i < CommaProcessor::kHistory; ++i)
    {
        const float d = proc.centsHistory[(size_t) ((proc.historyPos + i) % CommaProcessor::kHistory)];
        if (! std::isfinite (d)) { started = false; continue; }
        const float x = hist.getX() + hist.getWidth() * (float) i / (float) (CommaProcessor::kHistory - 1);
        const float y = hist.getCentreY() - juce::jlimit (-50.0f, 50.0f, d) / 50.0f * hist.getHeight() * 0.5f;
        if (! started) { path.startNewSubPath (x, y); started = true; }
        else path.lineTo (x, y);
    }
    g.setColour (Colours::signal);
    g.strokePath (path, juce::PathStrokeType (1.5f));

    // голоса MPE
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::headline, true));
    const int members = proc.mpeMembers.load();
    g.drawText (members > 0 ? "MPE" + midDot() + juce::String (members) + " channels" : juce::String ("Notes"), area.removeFromTop (20), juce::Justification::centredLeft);
    g.setFont (LnF::mono (LnF::Type::footnote));
    int shown = 0;
    for (auto& v : proc.voiceView)
    {
        if (v.held.load() == 0 || shown >= 7) continue;
        auto row = area.removeFromTop (17);
        ++shown;
        const double pc = v.pitch.load();
        const double off = t != nullptr ? pc - t->nearest (pc) : 0.0;
        g.setColour (Colours::ink);
        g.drawText (noteName12 (v.note.load()), row.removeFromLeft (40), juce::Justification::centredLeft);
        g.setColour (Colours::ink2);
        g.drawText (formatCents (off, 1), row.removeFromLeft (66), juce::Justification::centredLeft);
        auto bar = row.removeFromLeft (juce::jmax (20, row.getWidth() - 4)).toFloat().withSizeKeepingCentre ((float) juce::jmax (20, row.getWidth() - 4), 4.0f);
        g.setColour (Colours::track);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (Colours::carrier);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, v.pressure.load())), 2.0f);
    }
    if (shown == 0)
    {
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout));
        g.drawFittedText ("Play MIDI or MPE notes. Dots on the circle are your notes, the orange line is the input.", area.removeFromTop (48),
                          juce::Justification::topLeft, 3);
    }
}

void MainView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (currentTheme() == ThemeId::Oxygen ? Colours::surface : Colours::well);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    g.drawVerticalLine (rightArea.getX() - 8, (float) rightArea.getY(), (float) rightArea.getBottom());
    paintCircle (g, circleArea.toFloat());

    // заголовок вкладки слева от сегментов
    auto head = rightArea.withHeight (28);
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::title3, true));
    static const char* titles[] = { "Pitch", "Notes", "Matrix" };
    static const char* hints[] = { "Input pitch against the nearest step of the tuning",
                                   "Glide, Snap and landing shape every note",
                                   "MPE per note to any module setting" };
    const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), titles[tab]);
    g.drawText (titles[tab], head.removeFromLeft (tw + 12), juce::Justification::centredLeft);
    g.setColour (Colours::ink2);
    g.setFont (LnF::font (LnF::Type::callout));
    g.drawText (hints[tab], head.withTrimmedRight (280), juce::Justification::centredLeft, true);
    if (tab == 0) paintPitch (g, contentArea);
}
} // namespace comma
