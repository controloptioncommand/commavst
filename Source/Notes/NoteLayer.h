#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Tuning/Tuning.h"
#include "Params.h"

namespace comma
{
inline constexpr int kMaxVoices = 16;
inline constexpr int kControlBlock = 32;   // нотный слой считает высоты раз в 32 семпла (~0.7 мс)

// Голос: одна нажатая нота со своими MPE-измерениями и итоговой высотой
struct Voice
{
    bool active = false;   // держится или звучит после отпускания (до 10 с)
    bool held = false;
    int note = 60, channel = 1;
    juce::uint32 stamp = 0;               // порядок нажатия
    float strike = 0.8f, lift = 0.5f, pressure = 0.0f, slide = 0.0f, bend = 0.0f;   // bend — в полутонах
    double pitch = 6000.0;                // итоговая высота, абсолютные центы
    double releasedFor = 0.0;             // секунд после отпускания

    // внутреннее состояние скольжения
    double glideOffset = 0.0, glideT = 1.0, glideDur = 0.0;
    double prevRaw = 0.0, speed = 0.0, stepped = 0.0, snapOff = 0.0;
    bool moving = false;
    double landT = 1.0e9, landDir = 0.0;
    bool started = false;
};

struct NoteSettings
{
    double glideMs = 0.0, glideRate = 1200.0;
    int glideMode = 0, glideCurve = 0, glideDomain = 0;
    double snap = 0.0, snapMs = 120.0, character = 0.0, overshoot = 0.0, vibrato = 0.0, landingMs = 600.0;
    int bendRange = 48;
};

// Нотный слой: разбор MIDI / MPE (MCM, member channels, RPN 0 — диапазон бенда на канал), голоса,
// Glide, Snap, характер скольжения (портаменто → глиссандо по ступеням), посадка (перелёт и затухающее вибрато).
class NoteLayer
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset();
    // обрабатывает блок: события MIDI применяются на границах подблоков по 32 семпла
    void process (const juce::MidiBuffer& midi, int numSamples, const NoteSettings& s, const TuningSnapshot* t) noexcept;

    const Voice& voice (int i) const noexcept { return voices[(size_t) i]; }
    int getNumSubBlocks() const noexcept { return numSub; }
    float pitchAt (int voice, int sub) const noexcept { return track[(size_t) (voice * maxSub + juce::jlimit (0, juce::jmax (0, numSub - 1), sub))]; }
    bool gateAt (int voice, int sub) const noexcept { return gates[(size_t) (voice * maxSub + juce::jlimit (0, juce::jmax (0, numSub - 1), sub))] != 0; }
    int numHeld() const noexcept;
    bool isMpe() const noexcept { return lowerMembers > 0 || upperMembers > 0; }
    int memberChannels() const noexcept { return lowerMembers > 0 ? lowerMembers : upperMembers; }
    float sourceValue (int voice, int src, int bendRange) const noexcept;   // MpeSource для голоса
    // для эффектов без голосов: последний голос, самый высокий или среднее по держащимся
    float aggregate (int src, int mode, int bendRange) const noexcept;
    int lastVoice() const noexcept;

private:
    void handle (const juce::MidiMessage& m, const NoteSettings& s, const TuningSnapshot* t) noexcept;
    void noteOn (int ch, int note, float vel, const NoteSettings& s, const TuningSnapshot* t) noexcept;
    void noteOff (int ch, int note, float vel) noexcept;
    void tick (const NoteSettings& s, const TuningSnapshot* t, double dt) noexcept;
    float channelBend (int ch, int bendRange) const noexcept;
    bool isMaster (int ch) const noexcept;

    std::array<Voice, kMaxVoices> voices;
    std::array<float, 17> bendRaw {}, pressureCh {}, slideCh {};   // по каналам 1…16 (−1…1 и 0…1)
    std::array<float, 17> bendRangeCh {};                          // RPN 0; 0 — по настройке
    std::array<int, 17> rpnMsb {}, rpnLsb {};
    int lowerMembers = 0, upperMembers = 0;
    juce::uint32 stampCounter = 0;
    double lastPitch = -1.0, lastPitchAge = 1.0e9;
    std::vector<float> track;
    std::vector<juce::uint8> gates;
    int maxSub = 1, numSub = 0;
    double sr = 48000.0;
};
} // namespace comma
