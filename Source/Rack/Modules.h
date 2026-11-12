#pragma once
#include "Module.h"
#include "DSP/DSP.h"
#include <juce_dsp/juce_dsp.h>

namespace comma
{
// Интервал из тройки ручек «единица / центы / отношение / шаги»
inline double intervalOf (const ProcessContext& c, int unitP, int centsP, int ratioP, int stepsP,
                          double from = std::numeric_limits<double>::quiet_NaN()) noexcept
{
    return intervalCents ((IntervalUnit) c.i (unitP), c.v (centsP), c.i (ratioP), c.i (stepsP), c.tuning, from);
}

// Micro-doubler: копии на ±интервал (левая выше, правая ниже), задержка 0–30 мс, ВЧ-фильтр Focus на обработанном
class DoublerModule : public Module
{
public:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* const* ch, int nc, int n, const ProcessContext& ctx) override;

private:
    std::array<dsp::DelayShifter, 2> shift;
    std::array<dsp::Biquad, 2> focus;
    double sr = 48000.0;
};

// Staircase delay: питч-шифтер в петле обратной связи, каждое эхо на интервал выше
class StaircaseModule : public Module
{
public:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* const* ch, int nc, int n, const ProcessContext& ctx) override;

private:
    std::array<dsp::DelayLine, 2> line;
    std::array<dsp::DelayShifter, 2> shift;
    std::array<dsp::Biquad, 2> tone;
    dsp::Smooth time;
    double sr = 48000.0;
};

// Frequency shifter: однополосная модуляция через фильтр Гильберта, обратная связь, биения в темп
class ShifterModule : public Module
{
public:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* const* ch, int nc, int n, const ProcessContext& ctx) override;
    float getShiftHz() const noexcept { return shownHz.load(); }

private:
    std::array<dsp::Hilbert, 2> hil;
    std::array<double, 2> phase {};
    std::array<float, 2> fb {};
    dsp::Smooth hz;
    std::atomic<float> shownHz { 0.0f };
    double sr = 48000.0;
};

// Varispeed: тормоз ленты, wow и flutter, высота и рампы в центах по темпу
class VarispeedModule : public Module
{
public:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* const* ch, int nc, int n, const ProcessContext& ctx) override;

private:
    std::array<dsp::DelayLine, 2> tape, wobble;
    std::array<dsp::DelayShifter, 2> shift;
    dsp::Phasor wow, flutter, flutter2, freeRamp;
    double rate = 1.0, tapeD = 0.0, xfade = -1.0, sr = 48000.0;
    dsp::Smooth depthW, depthF, cents, shiftMix;
};

// Scale harmonizer: два голоса; низкая задержка — линия задержки, качество — PSOLA по высоте соло-голоса
class HarmonizerModule : public Module
{
public:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* const* ch, int nc, int n, const ProcessContext& ctx) override;
    int computeLatency (const float* values, double sr) const override;

private:
    std::array<std::array<dsp::DelayShifter, 2>, 2> shift;   // [голос][канал]
    std::array<dsp::Psola, 2> psola;
    dsp::YinTracker yin;
    std::array<dsp::Smooth, 2> ratio;
    double sr = 48000.0;
    int lastLatency = -1;
};

// Микро-автотюн: YIN → ближайшая ступень строя (или держащаяся нота) → сдвиг высоты
class AutotuneModule : public Module
{
public:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* const* ch, int nc, int n, const ProcessContext& ctx) override;
    int computeLatency (const float* values, double sr) const override;
    float getDetectedHz() const noexcept { return detected.load(); }
    float getCorrection() const noexcept { return correction.load(); }

private:
    dsp::YinTracker yin;
    dsp::Psola psola;
    std::array<dsp::DelayShifter, 2> shift;
    double corr = 0.0, sr = 48000.0;
    int lastLatency = -1;
    std::atomic<float> detected { 0.0f }, correction { 0.0f };
};

// MPE-резонатор: каждая нота — банк двухполюсных резонаторов на своей высоте
class ResonatorModule : public Module
{
public:
    static constexpr int kPartials = 8;
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* const* ch, int nc, int n, const ProcessContext& ctx) override;
    int activeVoices() const noexcept { return shownVoices.load(); }

private:
    struct Res { float a1 = 0, a2 = 0, g = 0, y1 = 0, y2 = 0; };
    struct V { std::array<Res, kPartials> r; float env = 0.0f, level = 0.0f, envCoef = 0.0f, pan = 0.0f; int count = 0; };
    std::array<V, kMaxVoices> voices;
    std::atomic<int> shownVoices { 0 };
    double sr = 48000.0;
};

// Spectral mapper: STFT, поиск пиков, перенос партиалов на ступени строя или растяжение под период строя
class SpectralModule : public Module
{
public:
    void prepare (double sr, int maxBlock) override;
    void reset() override;
    void process (float* const* ch, int nc, int n, const ProcessContext& ctx) override;
    int computeLatency (const float* values, double sr) const override;

private:
    struct Chan
    {
        std::vector<float> in, out, frame, prevPhase, outPhase;
        std::vector<float> peakBin, peakPhase;   // выходные пики прошлого кадра
        int numPrevPeaks = 0, pos = 0, hop = 0;
    };
    void frame (Chan& c, const ProcessContext& ctx) noexcept;
    void configure (int order);
    std::unique_ptr<juce::dsp::FFT> fft10, fft11;
    juce::dsp::FFT* fft = nullptr;
    std::array<Chan, 2> chans;
    std::vector<float> window, mag, phs, spec, outSpec;
    std::vector<int> peaks;
    int N = 2048, H = 512, order = 11;
    float invScale[2] = { 1.0f, 1.0f };   // масштаб обратного БПФ: 0 — 1024, 1 — 2048
    double sr = 48000.0;
};
} // namespace comma
