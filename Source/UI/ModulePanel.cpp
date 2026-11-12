#include "ModulePanel.h"
#include "Rack/Modules.h"

namespace comma
{
using LnF = CommaLookAndFeel;

namespace
{
std::unique_ptr<ParamKnob> makeKnob (CommaProcessor& p, int prm)
{
    auto k = std::make_unique<ParamKnob> (spec (prm).label);
    k->attach (p.apvts, spec (prm).id);
    k->slider.setTitle (spec (prm).name);
    return k;
}
}

ModulePanel::Column& ModulePanel::knob (int p)
{
    cols.push_back ({});
    cols.back().knobs.push_back (makeKnob (proc, p));
    return cols.back();
}

ModulePanel::Column& ModulePanel::interval (int unitP, int centsP, int ratioP, int stepsP)
{
    cols.push_back ({});
    auto& c = cols.back();
    auto seg = std::make_unique<ChoiceSegmented> (proc.apvts, spec (unitP).id, juce::StringArray { juce::String::fromUTF8 ("\xc2\xa2"), "Ratio", "Steps" });
    seg->setTitle (spec (unitP).name);
    c.top = std::move (seg);
    for (int p : { centsP, ratioP, stepsP }) c.knobs.push_back (makeKnob (proc, p));
    c.selector = unitP;
    c.width = 136;
    return c;
}

ModulePanel::Column& ModulePanel::sync (int syncP, int timeP, int divP)
{
    cols.push_back ({});
    auto& c = cols.back();
    auto t = std::make_unique<ParamToggle> ("Sync");
    t->attach (proc.apvts, spec (syncP).id);
    t->setTitle (spec (syncP).name);
    c.top = std::move (t);
    c.knobs.push_back (makeKnob (proc, timeP));
    c.knobs.push_back (makeKnob (proc, divP));
    c.selector = syncP;
    c.width = 100;
    return c;
}

ModulePanel::Column& ModulePanel::toggled (int toggleP, int knobP)
{
    cols.push_back ({});
    auto& c = cols.back();
    auto t = std::make_unique<ParamToggle> (spec (toggleP).label);
    t->attach (proc.apvts, spec (toggleP).id);
    t->setTitle (spec (toggleP).name);
    c.top = std::move (t);
    c.knobs.push_back (makeKnob (proc, knobP));
    c.width = 104;
    return c;
}

void ModulePanel::header (int choiceP, int width)
{
    auto seg = std::make_unique<ChoiceSegmented> (proc.apvts, spec (choiceP).id, spec (choiceP).choices);
    seg->setTitle (spec (choiceP).name);
    heads.push_back ({ std::move (seg), width });
}

ModulePanel::ModulePanel (CommaProcessor& p, int m) : proc (p), module (m)
{
    setTitle (juce::String (moduleInfo (m).name) + " settings");
    auto fmtCents = [] (float c) { return formatCents (c, 1); };
    switch ((ModuleId) m)
    {
        case ModuleId::Doubler:
            interval (P::DblUnit, P::DblCents, P::DblRatio, P::DblSteps);
            knob (P::DblDelay); knob (P::DblFocus); knob (P::DblWidth); knob (P::DblWindow);
            readout = [this]
            {
                if (juce::roundToInt (proc.getParam (P::DblUnit)) != 2) return juce::String();
                const auto t = proc.getTuning();
                return "1 step = " + juce::String (t != nullptr ? t->meanStep : 100.0, 1) + juce::String::fromUTF8 (" \xc2\xa2 (mean step of the tuning)");
            };
            break;
        case ModuleId::Harmonizer:
            header (P::HarMode, 220);
            interval (P::Har1Unit, P::Har1Cents, P::Har1Ratio, P::Har1Steps);
            knob (P::Har1Level);
            interval (P::Har2Unit, P::Har2Cents, P::Har2Ratio, P::Har2Steps);
            knob (P::Har2Level);
            readout = [this] { return juce::roundToInt (proc.getParam (P::HarMode)) == 1 ? juce::String ("Quality follows a solo voice: steps are real steps of the tuning")
                                                                                    : juce::String ("Low latency also takes chords: steps follow a solo voice, otherwise the mean step"); };
            break;
        case ModuleId::Staircase:
            interval (P::StaUnit, P::StaCents, P::StaRatio, P::StaSteps);
            sync (P::StaSync, P::StaTime, P::StaDiv);
            knob (P::StaFeedback); knob (P::StaTone); knob (P::StaWindow);
            readout = [this]
            {
                const auto t = proc.getTuning();
                if (t == nullptr || juce::roundToInt (proc.getParam (P::StaUnit)) != 2) return juce::String();
                const int st = juce::roundToInt (proc.getParam (P::StaSteps));
                if (st == 0) return juce::String();
                const double reps = t->periodCents / (std::abs (st) * t->meanStep);
                return juce::String (reps, std::abs (reps - std::round (reps)) < 1.0e-6 ? 0 : 1) + " echoes climb one period of " + t->name;
            };
            break;
        case ModuleId::Autotune:
            header (P::AutTarget, 220);
            header (P::AutMode, 220);
            knob (P::AutSpeed); knob (P::AutAmount);
            readout = [this, fmtCents]
            {
                auto* a = dynamic_cast<AutotuneModule*> (&proc.rack.getModule ((int) ModuleId::Autotune));
                if (a == nullptr || a->getDetectedHz() <= 0.0f) return juce::String ("No pitch on the input");
                return "Input " + juce::String (a->getDetectedHz(), 1) + " Hz" + midDot() + "correction " + fmtCents (a->getCorrection());
            };
            break;
        case ModuleId::Resonator:
            knob (P::ResDecay); knob (P::ResBright);
            toggled (P::ResScale, P::ResPartials);
            knob (P::ResStretch); knob (P::ResRelease); knob (P::ResDrive); knob (P::ResVelocity);
            readout = [this]
            {
                const int n = proc.midiNotes.load();
                return n == 0 ? juce::String ("Play MIDI or MPE notes: each note rings at its pitch") : juce::String (n) + (n == 1 ? " note ringing" : " notes ringing");
            };
            break;
        case ModuleId::Shifter:
            header (P::ShiDir, 160);
            {
                auto& c = sync (P::ShiSync, P::ShiShift, P::ShiDiv);
                c.width = 104;
            }
            knob (P::ShiFeedback); knob (P::ShiSpread);
            readout = [this]
            {
                auto* s = dynamic_cast<ShifterModule*> (&proc.rack.getModule ((int) ModuleId::Shifter));
                if (s == nullptr || proc.getParam (P::ShiSync) < 0.5f) return juce::String();
                return juce::String::fromUTF8 ("\xce\x94") + "f = BPM / 60 / beats = " + juce::String (s->getShiftHz(), 2) + " Hz";
            };
            break;
        case ModuleId::Varispeed:
            knob (P::VarSpeed); knob (P::VarRamp); knob (P::VarRampLen); knob (P::VarWow); knob (P::VarFlutter);
            toggled (P::VarBrake, P::VarBrakeTime);
            readout = [] { return juce::String ("Ramps run in time with the song when it plays. Brake stops the tape, off spins it back up"); };
            break;
        case ModuleId::Spectral:
            header (P::SpeMode, 160);
            header (P::SpeQuality, 220);
            knob (P::SpeAmount); knob (P::SpeThreshold);
            readout = [this]
            {
                const auto t = proc.getTuning();
                if (juce::roundToInt (proc.getParam (P::SpeMode)) == 1)
                    return t != nullptr && std::abs (t->periodCents - 1200.0) < 0.01 ? juce::String ("Stretch needs a tuning whose period is not an octave")
                                                                                   : "Stretch maps 2:1 onto the period, " + juce::String (t != nullptr ? t->periodCents : 1200.0, 1) + juce::String::fromUTF8 (" \xc2\xa2");
                return "Snap moves partials onto " + (t != nullptr ? t->name : juce::String ("12-EDO"));
            };
            break;
    }
    for (auto& c : cols)
    {
        if (c.top) addAndMakeVisible (*c.top);
        for (auto& k : c.knobs) addChildComponent (*k);
    }
    for (auto& h : heads) addAndMakeVisible (*h.first);
    refresh();
}

void ModulePanel::refresh()
{
    for (auto& c : cols)
    {
        const int sel = c.selector >= 0 ? juce::jlimit (0, (int) c.knobs.size() - 1, juce::roundToInt (proc.getParam (c.selector))) : 0;
        for (int i = 0; i < (int) c.knobs.size(); ++i)
            if (c.knobs[(size_t) i]->isVisible() != (i == sel)) c.knobs[(size_t) i]->setVisible (i == sel);
    }
    const auto r = readout ? readout() : juce::String();
    if (r != shownReadout) { shownReadout = r; repaint (readoutArea); }
}

void ModulePanel::paint (juce::Graphics& g)
{
    g.setColour (Colours::ink2);
    g.setFont (LnF::mono (LnF::Type::footnote));
    g.drawText (shownReadout, readoutArea, juce::Justification::centredLeft, true);
}

void ModulePanel::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (26);
    for (auto it = heads.rbegin(); it != heads.rend(); ++it)
    {
        it->first->setBounds (top.removeFromRight (it->second).withSizeKeepingCentre (it->second, 26));
        top.removeFromRight (10);
    }
    readoutArea = top;
    r.removeFromTop (6);
    int x = r.getX();
    for (auto& c : cols)
    {
        auto col = juce::Rectangle<int> (x, r.getY(), c.width, r.getHeight());
        auto t = col.removeFromTop (24);
        if (c.top) c.top->setBounds (t.reduced (4, 0));
        col.removeFromTop (4);
        for (auto& k : c.knobs) k->setBounds (col);
        x += c.width + 8;
    }
}
} // namespace comma
