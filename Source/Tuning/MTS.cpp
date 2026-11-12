#include "MTS.h"
#include "libMTSClient.h"
#include "libMTSMaster.h"

namespace comma
{
MTSLink::MTSLink() { client = MTS_RegisterClient(); }

MTSLink::~MTSLink()
{
    setMaster (false);
    if (client != nullptr) MTS_DeregisterClient ((MTSClient*) client);
}

bool MTSLink::hasMaster() const { return ! master && client != nullptr && MTS_HasMaster ((MTSClient*) client); }

bool MTSLink::readTuning (std::array<double, 128>& hz, juce::String& name, double& periodCents)
{
    if (! hasMaster()) return false;
    auto* c = (MTSClient*) client;
    bool changed = false;
    for (int n = 0; n < 128; ++n)
    {
        hz[(size_t) n] = MTS_NoteToFrequency (c, (char) n, -1);
        if (std::abs (hz[(size_t) n] - last[(size_t) n]) > 1.0e-9) changed = true;
    }
    last = hz;
    if (const char* s = MTS_GetScaleName (c)) name = juce::String::fromUTF8 (s);
    periodCents = 100.0 * MTS_GetPeriodSemitones (c);
    return changed;
}

bool MTSLink::canRegisterMaster() const { return MTS_CanRegisterMaster(); }

bool MTSLink::setMaster (bool on)
{
    if (on == master) return true;
    if (on)
    {
        if (! MTS_CanRegisterMaster()) return false;
        MTS_RegisterMaster();
        master = true;
        return true;
    }
    MTS_DeregisterMaster();
    master = false;
    return true;
}

void MTSLink::pushTuning (const TuningSnapshot& t)
{
    if (! master) return;
    std::array<double, 128> hz {};
    for (int n = 0; n < 128; ++n) hz[(size_t) n] = t.noteHz (n);
    MTS_SetNoteTunings (hz.data());
    MTS_SetScaleName (t.name.toRawUTF8());
    MTS_SetPeriodRatio (centsToRatio (t.periodCents));
}
} // namespace comma
