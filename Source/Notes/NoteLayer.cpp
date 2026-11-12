#include "NoteLayer.h"

namespace comma
{
void NoteLayer::prepare (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    maxSub = (juce::jmax (1, maxBlock) + kControlBlock - 1) / kControlBlock + 1;
    track.assign ((size_t) (kMaxVoices * maxSub), 6000.0f);
    gates.assign ((size_t) (kMaxVoices * maxSub), 0);
    reset();
}

void NoteLayer::reset()
{
    for (auto& v : voices) v = Voice();
    bendRaw.fill (0.0f); pressureCh.fill (0.0f); slideCh.fill (0.0f); bendRangeCh.fill (0.0f);
    rpnMsb.fill (127); rpnLsb.fill (127);
    lastPitch = -1.0;
    lastPitchAge = 1.0e9;
    numSub = 0;
}

bool NoteLayer::isMaster (int ch) const noexcept
{
    return (lowerMembers > 0 && ch == 1) || (upperMembers > 0 && ch == 16);
}

float NoteLayer::channelBend (int ch, int bendRange) const noexcept
{
    const float range = bendRangeCh[(size_t) ch] > 0.0f ? bendRangeCh[(size_t) ch] : (isMaster (ch) ? 2.0f : (float) bendRange);
    return bendRaw[(size_t) ch] * range;
}

int NoteLayer::numHeld() const noexcept
{
    int n = 0;
    for (auto& v : voices) n += v.held ? 1 : 0;
    return n;
}

int NoteLayer::lastVoice() const noexcept
{
    int best = -1;
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices[(size_t) i].held && (best < 0 || voices[(size_t) i].stamp > voices[(size_t) best].stamp)) best = i;
    return best;
}

void NoteLayer::noteOn (int ch, int note, float vel, const NoteSettings& s, const TuningSnapshot* t) noexcept
{
    int slot = -1;
    for (int i = 0; i < kMaxVoices && slot < 0; ++i) if (voices[(size_t) i].active && voices[(size_t) i].channel == ch && voices[(size_t) i].note == note) slot = i;
    for (int i = 0; i < kMaxVoices && slot < 0; ++i) if (! voices[(size_t) i].active) slot = i;
    if (slot < 0)   // кража: самый старый отпущенный, иначе самый старый
    {
        for (int i = 0; i < kMaxVoices; ++i)
            if (! voices[(size_t) i].held && (slot < 0 || voices[(size_t) i].stamp < voices[(size_t) slot].stamp)) slot = i;
        if (slot < 0)
            for (int i = 0; i < kMaxVoices; ++i)
                if (slot < 0 || voices[(size_t) i].stamp < voices[(size_t) slot].stamp) slot = i;
    }
    auto& v = voices[(size_t) slot];
    v = Voice();
    v.active = v.held = true;
    v.note = note;
    v.channel = ch;
    v.stamp = ++stampCounter;
    v.strike = vel;
    v.pressure = pressureCh[(size_t) ch];
    v.slide = slideCh[(size_t) ch];
    v.bend = channelBend (ch, s.bendRange) + (isMaster (ch) ? 0.0f : (lowerMembers > 0 ? channelBend (1, s.bendRange) : (upperMembers > 0 ? channelBend (16, s.bendRange) : 0.0f)));
    const double target = (t != nullptr ? t->noteCents[(size_t) note] : note * 100.0) + 100.0 * v.bend;
    const bool glide = (s.glideMode == 0 ? s.glideMs > 0.5 : true) && lastPitch >= 0.0 && lastPitchAge < 3.0;
    if (glide)
    {
        v.glideOffset = lastPitch - target;
        v.glideT = 0.0;
        v.glideDur = s.glideMode == 0 ? s.glideMs * 0.001 : std::abs (v.glideOffset) / juce::jmax (1.0, s.glideRate);
        if (v.glideDur <= 1.0e-4) v.glideT = 1.0;
    }
    v.pitch = target + (glide ? v.glideOffset : 0.0);
}

void NoteLayer::noteOff (int ch, int note, float vel) noexcept
{
    for (auto& v : voices)
        if (v.held && v.channel == ch && v.note == note) { v.held = false; v.lift = vel; v.releasedFor = 0.0; }
}

void NoteLayer::handle (const juce::MidiMessage& m, const NoteSettings& s, const TuningSnapshot* t) noexcept
{
    const int ch = juce::jlimit (1, 16, m.getChannel());
    auto updateChannel = [this, ch, &s]
    {
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const bool master = isMaster (ch);
            if (v.channel == ch || master)
            {
                v.bend = channelBend (v.channel, s.bendRange)
                       + (isMaster (v.channel) ? 0.0f : (lowerMembers > 0 && v.channel <= 1 + lowerMembers ? channelBend (1, s.bendRange)
                                                         : (upperMembers > 0 && v.channel >= 16 - upperMembers ? channelBend (16, s.bendRange) : 0.0f)));
                if (v.channel == ch) { v.pressure = pressureCh[(size_t) ch]; v.slide = slideCh[(size_t) ch]; }
            }
        }
    };
    if (m.isNoteOn()) noteOn (ch, m.getNoteNumber(), m.getFloatVelocity(), s, t);
    else if (m.isNoteOff()) noteOff (ch, m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isPitchWheel()) { bendRaw[(size_t) ch] = juce::jlimit (-1.0f, 1.0f, (float) (m.getPitchWheelValue() - 8192) / 8192.0f); updateChannel(); }
    else if (m.isChannelPressure()) { pressureCh[(size_t) ch] = (float) m.getChannelPressureValue() / 127.0f; updateChannel(); }
    else if (m.isAftertouch())
    {
        for (auto& v : voices) if (v.active && v.channel == ch && v.note == m.getNoteNumber()) v.pressure = (float) m.getAfterTouchValue() / 127.0f;
    }
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(), val = m.getControllerValue();
        if (cc == 74) { slideCh[(size_t) ch] = (float) val / 127.0f; updateChannel(); }
        else if (cc == 101) rpnMsb[(size_t) ch] = val;
        else if (cc == 100) rpnLsb[(size_t) ch] = val;
        else if (cc == 6 && rpnMsb[(size_t) ch] == 0)
        {
            if (rpnLsb[(size_t) ch] == 0) { bendRangeCh[(size_t) ch] = (float) juce::jmax (1, val); updateChannel(); }
            else if (rpnLsb[(size_t) ch] == 6 && (ch == 1 || ch == 16))
            {
                // MPE Configuration Message: число member channels зоны; бенд — ±48 у участников, ±2 у мастера
                const int members = juce::jlimit (0, 15, val);
                if (ch == 1) lowerMembers = members; else upperMembers = members;
                if (members > 0)
                {
                    bendRangeCh[(size_t) ch] = 2.0f;
                    for (int c = 0; c < members; ++c) bendRangeCh[(size_t) (ch == 1 ? 2 + c : 15 - c)] = 0.0f;   // по настройке Bend (±48)
                }
                else bendRangeCh[(size_t) ch] = 0.0f;
            }
        }
        else if (cc == 120 || cc == 123) { for (auto& v : voices) if (v.held) { v.held = false; v.releasedFor = 0.0; } }
        else if (cc == 121) { bendRaw[(size_t) ch] = 0.0f; pressureCh[(size_t) ch] = 0.0f; slideCh[(size_t) ch] = 0.0f; updateChannel(); }
    }
}

void NoteLayer::tick (const NoteSettings& s, const TuningSnapshot* t, double dt) noexcept
{
    const double kSpeed = 1.0 - std::exp (-dt / 0.03);
    const double kStep = s.character > 0.0 ? 1.0 - std::exp (-dt / 0.003) : 1.0;
    const double kSnap = 1.0 - std::exp (-dt / juce::jmax (0.002, s.snapMs * 0.001));
    const double vibTau = juce::jmax (0.02, s.landingMs * 0.001 / 3.0);
    int newest = -1;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        if (! v.active) continue;
        if (! v.held && (v.releasedFor += dt) > 10.0) { v.active = false; continue; }

        const double target = (t != nullptr ? t->noteCents[(size_t) v.note] : v.note * 100.0) + 100.0 * v.bend;
        double off = 0.0;
        if (v.glideT < 1.0)
        {
            v.glideT = juce::jmin (1.0, v.glideT + dt / juce::jmax (1.0e-4, v.glideDur));
            const double u = v.glideT;
            const double k = s.glideCurve == 1 ? 1.0 - (1.0 - u) * (1.0 - u) : (s.glideCurve == 2 ? u * u : u);
            if (s.glideDomain == 1)
            {
                const double r0 = centsToRatio (v.glideOffset);
                off = ratioToCents (1.0 + (r0 - 1.0) * (1.0 - k));
            }
            else off = v.glideOffset * (1.0 - k);
        }
        const double raw = target + off;
        if (! v.started) { v.prevRaw = raw; v.stepped = raw; v.speed = 0.0; v.started = true; }

        const double inst = std::abs (raw - v.prevRaw) / dt;
        v.speed += (inst - v.speed) * kSpeed;

        // характер: от непрерывного портаменто к глиссандо по ступеням
        const double q = t != nullptr ? t->nearest (raw) : raw;
        const double stepTarget = raw + s.character * (q - raw);
        v.stepped += (stepTarget - v.stepped) * kStep;

        // магнит: пока высота движется — свободна, остановилась — тянется к ступени
        const double still = juce::jlimit (0.0, 1.0, 1.0 - v.speed / 40.0);
        const double snapTarget = s.snap * still * ((t != nullptr ? t->nearest (v.stepped) : v.stepped) - v.stepped);
        v.snapOff += (snapTarget - v.snapOff) * kSnap;

        // посадка: перелёт и затухающее вибрато, когда высота останавливается
        if (std::abs (raw - v.prevRaw) > 1.0e-9 && v.speed > 80.0) { v.moving = true; v.landDir = raw > v.prevRaw ? 1.0 : -1.0; }
        else if (v.moving && v.speed < 20.0) { v.moving = false; v.landT = 0.0; }
        double land = 0.0;
        if (v.landT < 8.0)
        {
            const double tau = 0.045, x = v.landT / tau;
            land = v.landDir * s.overshoot * x * std::exp (1.0 - x)
                 + s.vibrato * std::exp (-v.landT / vibTau) * std::sin (6.283185307179586 * 5.5 * v.landT);
            v.landT += dt;
        }
        v.pitch = v.stepped + v.snapOff + land;
        v.prevRaw = raw;
        if (v.held && (newest < 0 || v.stamp > voices[(size_t) newest].stamp)) newest = i;
    }
    lastPitchAge += dt;
    if (newest >= 0) { lastPitch = voices[(size_t) newest].pitch; lastPitchAge = 0.0; }
}

void NoteLayer::process (const juce::MidiBuffer& midi, int numSamples, const NoteSettings& s, const TuningSnapshot* t) noexcept
{
    numSub = juce::jmin (maxSub, (numSamples + kControlBlock - 1) / kControlBlock);
    auto it = midi.cbegin();
    for (int sub = 0; sub < numSub; ++sub)
    {
        const int start = sub * kControlBlock, end = juce::jmin (numSamples, start + kControlBlock);
        while (it != midi.cend() && (*it).samplePosition < end)
        {
            handle ((*it).getMessage(), s, t);
            ++it;
        }
        tick (s, t, (double) (end - start) / sr);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            track[(size_t) (v * maxSub + sub)] = (float) voices[(size_t) v].pitch;
            gates[(size_t) (v * maxSub + sub)] = voices[(size_t) v].held ? 1 : 0;
        }
    }
    for (; it != midi.cend(); ++it) handle ((*it).getMessage(), s, t);
}

float NoteLayer::sourceValue (int vi, int src, int bendRange) const noexcept
{
    const auto& v = voices[(size_t) juce::jlimit (0, kMaxVoices - 1, vi)];
    switch (src)
    {
        case SrcStrike:   return v.strike;
        case SrcLift:     return v.lift;
        case SrcPressure: return v.pressure;
        case SrcSlide:    return v.slide;
        case SrcBend:     return juce::jlimit (-1.0f, 1.0f, v.bend / (float) juce::jmax (1, bendRange));
        case SrcNote:     return (float) v.note / 127.0f;
        default:          return 0.0f;
    }
}

float NoteLayer::aggregate (int src, int mode, int bendRange) const noexcept
{
    if (src <= SrcNone) return 0.0f;
    int n = 0, best = -1;
    float sum = 0.0f;
    for (int i = 0; i < kMaxVoices; ++i)
    {
        const auto& v = voices[(size_t) i];
        if (! v.held) continue;
        ++n;
        sum += sourceValue (i, src, bendRange);
        if (best < 0) best = i;
        else if (mode == 0 && v.stamp > voices[(size_t) best].stamp) best = i;
        else if (mode == 1 && v.pitch > voices[(size_t) best].pitch) best = i;
    }
    if (n == 0)
    {
        // никто не держит: Lift — у последнего отпущенного, остальное — 0
        if (src != SrcLift) return 0.0f;
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices[(size_t) i].active && (best < 0 || voices[(size_t) i].stamp > voices[(size_t) best].stamp)) best = i;
        return best >= 0 ? voices[(size_t) best].lift : 0.0f;
    }
    return mode == 2 ? sum / (float) n : sourceValue (best, src, bendRange);
}
} // namespace comma
