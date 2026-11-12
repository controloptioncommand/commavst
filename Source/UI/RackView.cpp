#include "RackView.h"

namespace comma
{
using LnF = CommaLookAndFeel;

namespace
{
constexpr int kLinkW = 24, kCardH = 112;

bool isOn (CommaProcessor& p, int m) { return p.apvts.getRawParameterValue (moduleParam (m, "on"))->load() > 0.5f; }
}

// ===========================================================================
SlotCard::SlotCard (CommaProcessor& p, int m) : proc (p), module (m)
{
    const juce::String name = moduleInfo (m).name;
    setTitle (name + " slot");
    setDescription ("Drag to reorder, or press Alt and an arrow key.");
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    mix.attach (p.apvts, moduleParam (m, "mix"));
    mix.slider.setTitle (name + " mix");
    on.attach (p.apvts, moduleParam (m, "on"));
    on.setTitle (name + " on");
    close.setTitle ("Remove " + name);
    close.onClick = [this] { if (onRemove) onRemove (module); };
    for (juce::Component* c : std::initializer_list<juce::Component*> { &mix, &on, &close })
    {
        addAndMakeVisible (c);
        c->setMouseCursor (juce::MouseCursor::NormalCursor);
    }
}

void SlotCard::CloseButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto c = getLocalBounds().toFloat().getCentre();
    const float s = 3.5f;
    g.setColour (down || over ? Colours::ink : Colours::ink2);
    g.drawLine (c.x - s, c.y - s, c.x + s, c.y + s, 1.4f);
    g.drawLine (c.x - s, c.y + s, c.x + s, c.y - s, 1.4f);
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, getLocalBounds().toFloat().reduced (2.0f), 6.0f);
}

void SlotCard::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (selected ? Colours::ink : Colours::outline);
    g.drawRoundedRectangle (r, 10.0f, 1.0f);

    // ручка перетаскивания: 2 × 3 точки
    g.setColour (Colours::ink2.withAlpha (0.6f));
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 3; ++j)
            g.fillEllipse (10.0f + (float) i * 4.0f, 11.0f + (float) j * 4.0f, 2.0f, 2.0f);

    const bool active = isOn (proc, module);
    g.setColour (active ? Colours::ink : Colours::ink2);
    g.setFont (LnF::font (LnF::Type::body, true));
    g.drawText (moduleInfo (module).name, juce::Rectangle<int> (22, 6, getWidth() - 22 - 26, 22), juce::Justification::centredLeft, true);
}

void SlotCard::paintOverChildren (juce::Graphics& g)
{
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, getLocalBounds().toFloat(), 10.0f);
}

void SlotCard::resized()
{
    auto r = getLocalBounds();
    close.setBounds (r.getRight() - 28, 3, 26, 28);
    auto foot = r.removeFromBottom (30).reduced (10, 4);
    on.setBounds (foot.removeFromLeft (70));
    r.removeFromTop (28);
    // ручка Mix слева, подпись и значение справа (в карточке мало высоты)
    mix.setBounds (r.reduced (8, 0));
    auto m = mix.getLocalBounds();
    const int k = juce::jmin (44, m.getHeight());
    mix.slider.setBounds (m.removeFromLeft (k).withSizeKeepingCentre (k, k));
    m.removeFromLeft (8);
    auto text = m.withSizeKeepingCentre (m.getWidth(), 30);
    mix.label.setBounds (text.removeFromTop (16));
    mix.value.setBounds (text);
    mix.label.setJustificationType (juce::Justification::centredLeft);
    mix.value.setJustificationType (juce::Justification::centredLeft);
}

void SlotCard::mouseDown (const juce::MouseEvent& e)
{
    dragging = false;
    dragStart = e.getEventRelativeTo (getParentComponent()).getPosition();
    dragOffset = dragStart.x - getX();
    if (onSelect) onSelect (module);
}

void SlotCard::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.getEventRelativeTo (getParentComponent()).getPosition();
    if (! dragging && p.getDistanceFrom (dragStart) < 5) return;
    dragging = true;
    toFront (false);
    setTopLeftPosition (p.x - dragOffset, getY());
    if (onDragMove) onDragMove (module, getBounds().getCentreX());
}

void SlotCard::mouseUp (const juce::MouseEvent&)
{
    if (dragging && onDragEnd) onDragEnd (module, getBounds().getCentreX());
    dragging = false;
}

bool SlotCard::keyPressed (const juce::KeyPress& k)
{
    const bool alt = k.getModifiers().isAltDown();
    if (alt && (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::rightKey)))
    {
        if (onNudge) onNudge (module, k.isKeyCode (juce::KeyPress::leftKey) ? -1 : 1);
        return true;
    }
    if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) { if (onSelect) onSelect (module); return true; }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { if (onRemove) onRemove (module); return true; }
    return false;
}

// ===========================================================================
void LinkButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat();
    const auto c = r.getCentre();
    g.setColour (over || down ? Colours::ink : Colours::ink2);
    if (parallel)
    {
        g.fillRoundedRectangle (c.x - 3.5f, c.y - 7.0f, 2.0f, 14.0f, 1.0f);
        g.fillRoundedRectangle (c.x + 1.5f, c.y - 7.0f, 2.0f, 14.0f, 1.0f);
    }
    else
    {
        juce::Path p;
        p.startNewSubPath (c.x - 2.5f, c.y - 5.0f);
        p.lineTo (c.x + 2.5f, c.y);
        p.lineTo (c.x - 2.5f, c.y + 5.0f);
        g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, r.reduced (1.0f), 6.0f);
}

void RackView::AddButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (Colours::well);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (Colours::outline);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    const auto col = ! isEnabled() ? Colours::ink2.withAlpha (0.5f) : (over || down ? Colours::signal : Colours::ink);
    g.setColour (col);
    const float cx = 16.0f, cy = r.getCentreY();
    g.drawLine (cx - 4.5f, cy, cx + 4.5f, cy, 1.5f);
    g.drawLine (cx, cy - 4.5f, cx, cy + 4.5f, 1.5f);
    g.setFont (LnF::font (LnF::Type::callout, true));
    g.drawText ("Add module", r.withTrimmedLeft (28.0f), juce::Justification::centredLeft);
    if (hasKeyboardFocus (false)) LnF::drawFocusRing (g, r, 8.0f);
}

// ===========================================================================
RackView::RackView (CommaProcessor& p) : proc (p)
{
    setTitle ("Rack");
    for (int m = 0; m < kNumModules; ++m)
    {
        auto c = std::make_unique<SlotCard> (p, m);
        c->onSelect = [this] (int mod) { selectModule (mod); };
        c->onRemove = [this] (int mod)
        {
            const int idx = proc.getLayout().indexOf (mod);
            if (idx >= 0) proc.removeSlot (idx);
            if (selected == mod) selected = -1;
            refresh();
        };
        c->onNudge = [this] (int mod, int dir)
        {
            const int idx = proc.getLayout().indexOf (mod);
            if (idx >= 0) proc.moveSlot (idx, idx + dir);
            refresh();
            if (auto& card = cards[(size_t) mod]) card->grabKeyboardFocus();
        };
        c->onDragMove = [this] (int mod, int x)
        {
            draggingModule = mod;
            dropIndex = slotIndexForX (x);
            repaint();
        };
        c->onDragEnd = [this] (int mod, int x)
        {
            const int from = proc.getLayout().indexOf (mod);
            const int to = slotIndexForX (x);
            draggingModule = dropIndex = -1;
            if (from >= 0 && to != from) proc.moveSlot (from, to);
            hasShown = false;   // вернуть карточку на место, даже если порядок не сменился
            refresh();
        };
        addChildComponent (*c);
        cards[(size_t) m] = std::move (c);
    }
    for (int i = 0; i < (int) links.size(); ++i)
    {
        addChildComponent (links[(size_t) i]);
        links[(size_t) i].onClick = [this, i]
        {
            const auto l = proc.getLayout();
            proc.setParallel (i + 1, ! l.isParallel (i + 1));
            refresh();
        };
    }
    for (int m = 0; m < kNumModules; ++m)
    {
        panels[(size_t) m] = std::make_unique<ModulePanel> (p, m);
        addChildComponent (*panels[(size_t) m]);
    }
    addAndMakeVisible (add);
    add.onClick = [this] { openAddMenu(); };
    refresh();
}

void RackView::openAddMenu()
{
    const auto l = proc.getLayout();
    juce::PopupMenu m;
    m.addSectionHeader ("Add to the end of the rack");
    for (int mod = 0; mod < kNumModules; ++mod)
        if (! l.contains (mod)) m.addItem (1 + mod, moduleInfo (mod).name);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (add), [this] (int r)
    {
        if (r <= 0) return;
        if (proc.addModule (r - 1)) selectModule (r - 1);
        refresh();
    });
}

void RackView::selectModule (int m)
{
    selected = m;
    for (auto& c : cards) c->setSelected (c->getModule() == m);
    for (int i = 0; i < kNumModules; ++i)
        if (panels[(size_t) i] != nullptr) panels[(size_t) i]->setVisible (i == m);
    repaint();
}

juce::Rectangle<int> RackView::cardBounds (int slot) const
{
    const int cw = (rowArea.getWidth() - (kMaxSlots - 1) * kLinkW) / kMaxSlots;
    return { rowArea.getX() + slot * (cw + kLinkW), rowArea.getY(), cw, kCardH };
}

int RackView::slotIndexForX (int x) const
{
    const auto l = proc.getLayout();
    if (l.count == 0) return 0;
    const int step = cardBounds (1).getX() - cardBounds (0).getX();
    return juce::jlimit (0, l.count - 1, (x - rowArea.getX()) / juce::jmax (1, step));
}

void RackView::layoutCards()
{
    const auto l = shown;
    for (auto& c : cards) c->setVisible (false);
    for (int i = 0; i < l.count; ++i)
    {
        auto& c = *cards[(size_t) l.module[(size_t) i]];
        c.setBounds (cardBounds (i));
        c.setVisible (true);
        c.setDescription (juce::String ("Slot ") + juce::String (i + 1) + (l.isParallel (i) ? ", parallel with the previous slot" : ""));
    }
    for (int i = 0; i < (int) links.size(); ++i)
    {
        auto& lk = links[(size_t) i];
        lk.setVisible (i + 1 < l.count);
        if (i + 1 >= l.count) continue;
        const auto a = cardBounds (i);
        lk.setBounds (a.getRight(), a.getCentreY() - 14, kLinkW, 28);
        lk.setParallel (l.isParallel (i + 1));
        const juce::String an = moduleInfo (l.module[(size_t) i]).name, bn = moduleInfo (l.module[(size_t) i + 1]).name;
        lk.setTitle (l.isParallel (i + 1) ? bn + " runs in parallel with " + an + ". Click to run it after " + an
                                          : bn + " runs after " + an + ". Click to run it in parallel");
    }
    add.setEnabled (l.count < kMaxSlots);
    repaint();
}

void RackView::refresh()
{
    const auto l = proc.getLayout();
    if (! hasShown || l != shown)
    {
        shown = l;
        hasShown = true;
        if (selected >= 0 && ! l.contains (selected)) selected = -1;
        if (selected < 0 && l.count > 0) selected = l.module[0];
        selectModule (selected);
        layoutCards();
    }
    if (selected >= 0 && panels[(size_t) selected] != nullptr) panels[(size_t) selected]->refresh();
    for (int i = 0; i < l.count; ++i)
    {
        auto& c = *cards[(size_t) l.module[(size_t) i]];
        const float a = isOn (proc, c.getModule()) ? 1.0f : 0.5f;
        for (auto* ch : c.getChildren())
            if (dynamic_cast<ParamKnob*> (ch) != nullptr && std::abs (ch->getAlpha() - a) > 0.01f) { ch->setAlpha (a); c.repaint(); }
    }
}

void RackView::dragForTest (int fromSlot, int toSlot)
{
    const auto l = proc.getLayout();
    if (fromSlot < 0 || fromSlot >= l.count) return;
    auto& c = *cards[(size_t) l.module[(size_t) fromSlot]];
    if (c.onDragEnd) c.onDragEnd (c.getModule(), cardBounds (toSlot).getCentreX());
}

void RackView::paint (juce::Graphics& g)
{
    auto h = headArea;
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::title3, true));
    g.drawText ("Rack", h.removeFromTop (22), juce::Justification::centredLeft);
    g.setColour (Colours::ink2);
    g.setFont (LnF::font (LnF::Type::callout));
    g.drawText ("Audio flows left to right. The link between two slots runs them one after another or in parallel.",
                h.withTrimmedTop (2).withTrimmedRight (180), juce::Justification::topLeft, true);

    const auto l = shown;
    if (l.count == 0)
    {
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::body));
        g.drawText ("The rack is empty. Audio passes through unchanged. Add a module to start.",
                    rowArea.withHeight (kCardH), juce::Justification::centred);
    }

    // параллельные группы: скобка под карточками и подпись
    for (int i = 0; i < l.count; i = l.groupEnd (i) + 1)
    {
        const int e = l.groupEnd (i);
        if (e == i) continue;
        const auto a = cardBounds (i), b = cardBounds (e);
        const float y = (float) a.getBottom() + 7.0f, x0 = (float) a.getX() + 10.0f, x1 = (float) b.getRight() - 10.0f;
        g.setColour (Colours::ink2.withAlpha (0.7f));
        g.drawHorizontalLine ((int) y, x0, x1);
        g.drawVerticalLine ((int) x0, y - 4.0f, y + 1.0f);
        g.drawVerticalLine ((int) x1, y - 4.0f, y + 1.0f);
        g.setFont (LnF::mono (LnF::Type::footnote));
        g.setColour (Colours::ink2);
        g.drawText ("parallel", juce::Rectangle<float> (x0, y + 2.0f, x1 - x0, 14.0f), juce::Justification::centred);
    }

    // разделитель и сведения о выбранном модуле
    g.setColour (Colours::outline);
    g.drawHorizontalLine (detailArea.getY() - 8, (float) detailArea.getX(), (float) detailArea.getRight());
    auto d = detailArea;
    if (selected < 0)
    {
        g.setColour (Colours::ink2);
        g.setFont (LnF::font (LnF::Type::callout));
        g.drawText (l.count == 0 ? "Modules: Doubler, Harmonizer, Staircase, Autotune, Resonator, Shifter, Varispeed, Spectral."
                                 : "Choose a slot to see its module.", d.removeFromTop (20), juce::Justification::centredLeft);
        return;
    }
    const auto& info = moduleInfo (selected);
    const int idx = l.indexOf (selected);
    auto top = d.removeFromTop (22);
    g.setColour (Colours::ink);
    g.setFont (LnF::font (LnF::Type::title3, true));
    const auto nameW = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), info.name);
    g.drawText (info.name, top.removeFromLeft (nameW + 12), juce::Justification::centredLeft);
    g.setColour (Colours::ink2);
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.drawText ("Slot " + juce::String (idx + 1) + midDot() + (l.isParallel (idx) || (idx + 1 < l.count && l.isParallel (idx + 1)) ? "parallel" : "serial"),
                top, juce::Justification::centredLeft);
    d.removeFromTop (2);
    g.setFont (LnF::font (LnF::Type::callout));
    g.drawText (info.description, d.removeFromTop (18), juce::Justification::centredLeft, true);
}

void RackView::paintOverChildren (juce::Graphics& g)
{
    // место, куда встанет перетаскиваемая карточка
    if (draggingModule < 0 || dropIndex < 0) return;
    const int from = shown.indexOf (draggingModule);
    if (from < 0 || dropIndex == from) return;
    const auto c = cardBounds (dropIndex);
    const float x = dropIndex < from ? (float) c.getX() - kLinkW * 0.5f : (float) c.getRight() + kLinkW * 0.5f;
    g.setColour (Colours::signal);
    g.fillRoundedRectangle (x - 1.0f, (float) c.getY() + 6.0f, 2.0f, (float) c.getHeight() - 12.0f, 1.0f);
}

void RackView::resized()
{
    auto r = getLocalBounds();
    headArea = r.removeFromTop (40);
    add.setBounds (headArea.getRight() - 150, headArea.getY(), 150, 28);
    r.removeFromTop (8);
    rowArea = r.removeFromTop (kCardH + 20);
    r.removeFromTop (16);
    detailArea = r;
    for (auto& pn : panels) if (pn != nullptr) pn->setBounds (detailArea.withTrimmedTop (46));
    hasShown = false;
    refresh();
}
} // namespace comma
