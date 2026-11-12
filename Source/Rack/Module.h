#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>

namespace comma
{
// Что модуль знает о хосте в этом блоке
struct ProcessContext
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    double ppq = 0.0;
    bool playing = false;
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
    // задержка «мокрого» сигнала в семплах; меняется только при смене режима «низкая задержка» / «качество»
    int getLatency() const noexcept { return latency.load(); }

protected:
    std::atomic<int> latency { 0 };
};

std::unique_ptr<Module> createModule (int moduleId);
} // namespace comma
