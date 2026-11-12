#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include "Params.h"
#include "Tuning/Tuning.h"
#include "Notes/NoteLayer.h"

namespace comma
{
// Мод-матрица на блок: строки «источник → назначение, сила» и нормированные значения ручек.
// Эффекты без голосов получают уже промодулированные значения (MPE сведено: последняя нота, самая высокая или среднее),
// модули с голосами (Resonator) берут значение на каждый голос через voiceValue().
struct ModMatrix
{
    struct Row { int src = 0, dst = -1; float amt = 0.0f; };
    std::array<Row, kNumModRows> rows {};
    std::array<float, P::Count> norm {};
    int bendRange = 48;

    float voiceValue (const NoteLayer& notes, int p, int voice) const noexcept
    {
        float n = norm[(size_t) p];
        for (const auto& r : rows)
            if (r.dst == p && r.src != SrcNone) n += r.amt * notes.sourceValue (voice, r.src, bendRange);
        return spec (p).range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, n));
    }
};

// Что модуль знает о хосте и о нотах в этом блоке
struct ProcessContext
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
    int blockOffset = 0;                      // где этот кусок начинается в блоке хоста (для высот нотного слоя)
    const TuningSnapshot* tuning = nullptr;
    const float* values = nullptr;            // P::Count значений с модуляцией
    const NoteLayer* notes = nullptr;
    const ModMatrix* matrix = nullptr;

    float v (int p) const noexcept { return values != nullptr ? values[p] : spec (p).def; }
    int i (int p) const noexcept { return juce::roundToInt (v (p)); }
    bool b (int p) const noexcept { return v (p) > 0.5f; }
};

// Модуль rack'а. process() — в аудиопотоке: без выделений памяти, блокировок и ввода-вывода.
// Модуль отдаёт «мокрый» сигнал (как при Mix 100 %); смешивание с сухим, bypass и выравнивание задержки делает Rack.
class Module
{
public:
    virtual ~Module() = default;
    virtual void prepare (double sampleRate, int maxBlock) { juce::ignoreUnused (sampleRate, maxBlock); }
    virtual void reset() {}
    virtual void process (float* const* channels, int numChannels, int numSamples, const ProcessContext& ctx)
    {
        juce::ignoreUnused (channels, numChannels, numSamples, ctx);
    }
    // задержка при этих значениях ручек (режим «низкая задержка» / «качество»); зовёт поток сообщений
    virtual int computeLatency (const float* values, double sampleRate) const { juce::ignoreUnused (values, sampleRate); return 0; }
    // задержка «мокрого» сигнала в семплах; ставит процессор, модуль по ней понимает свой режим
    int getLatency() const noexcept { return latency.load(); }
    void setLatency (int l) noexcept { latency.store (l); }

protected:
    std::atomic<int> latency { 0 };
};

std::unique_ptr<Module> createModule (int moduleId);
} // namespace comma
