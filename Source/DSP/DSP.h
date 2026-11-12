#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// DSP-примитивы Comma. Всё, что вызывается в аудиопотоке, — без выделений памяти: буферы — в prepare().
namespace comma::dsp
{
constexpr double kTwoPi = 6.283185307179586;

// Кубическая интерполяция Эрмита по четырём точкам, t ∈ [0, 1)
inline float hermite (float ym1, float y0, float y1, float y2, float t) noexcept
{
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * t + c2) * t + c1) * t + y0;
}

// Линия задержки с дробным чтением (Hermite). Задержка d — в семплах, не меньше 2.
class DelayLine
{
public:
    void prepare (int maxDelaySamples);
    void clear();
    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }
    float read (float d) const noexcept;
    int capacity() const noexcept { return mask - 4; }

private:
    std::vector<float> buf { 0.0f, 0.0f, 0.0f, 0.0f };
    int mask = 3, w = 0;
};

// Питч-шифтер на линии задержки: две головки, сдвинутые на пол-окна, перекрёстное затухание окном Ханна.
// Окно 20–60 мс. ratio > 1 — выше. base — дополнительная задержка (для дублера).
// Если известен период входа, окно подгоняется под чётное число периодов: на сшивках фаза не прыгает,
// и высота точна даже при больших сдвигах (иначе она гуляет в пределах ± половины частоты окна).
class DelayShifter
{
public:
    void prepare (double sampleRate, float maxWindowMs = 100.0f, float maxBaseMs = 40.0f);
    void reset();
    float process (float x, float ratio, float windowMs, float baseSamples = 0.0f, float periodSamples = 0.0f) noexcept;

private:
    DelayLine line;
    double sr = 48000.0;
    float phase = 0.0f, win = 0.0f;
};

// Биквад (RBJ): НЧ, ВЧ, полосовой
class Biquad
{
public:
    enum Type { LowPass, HighPass, BandPass };
    void set (Type t, double sampleRate, double hz, double q) noexcept;
    void reset() noexcept { z1 = z2 = 0.0f; }
    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

private:
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
};

// Аналитический сигнал: две цепочки по 4 allpass-фильтра второго порядка (коэффициенты О. Ниемитало), разность фаз 90°
class Hilbert
{
public:
    void reset() noexcept;
    void process (float x, float& re, float& im) noexcept;

private:
    struct Chain { std::array<float, 4> c {}; std::array<float, 4> x1 {}, x2 {}, y1 {}, y2 {}; float run (float x) noexcept; };
    static constexpr float sq (double v) { return (float) (v * v); }
    Chain a { { { sq (0.6923878), sq (0.9360654322959), sq (0.9882295226860), sq (0.9987488452737) } } };
    Chain b { { { sq (0.4021921162426), sq (0.8561710882420), sq (0.9722909545651), sq (0.9952884791278) } } };
    float aDelay = 0.0f;
};

// Определение высоты YIN (de Cheveigné, Kawahara, 2002): грубо — на прореженном сигнале (~12 кГц),
// уточнение — на полной частоте около найденного периода. Анализ — раз в hop семплов, без выделений памяти.
class YinTracker
{
public:
    void prepare (double sampleRate, float minHz = 60.0f, float maxHz = 1500.0f);
    void reset();
    void push (float x) noexcept;          // по семплу
    float getHz() const noexcept { return hz; }
    float getPeriod() const noexcept { return period; }      // в семплах полной частоты, 0 — нет высоты
    float getConfidence() const noexcept { return confidence; }
    bool isVoiced() const noexcept { return period > 0.0f; }
    bool justAnalysed() const noexcept { return fresh; }

private:
    void analyse() noexcept;
    double sr = 48000.0;
    int dec = 4, decCount = 0, hop = 64, hopCount = 0;
    float decAcc = 0.0f;
    std::vector<float> full, low, diff;
    int fullMask = 0, fullW = 0, lowMask = 0, lowW = 0;
    int win = 512, maxTau = 200, minTau = 8, fineWin = 1024;
    float hz = 0.0f, period = 0.0f, confidence = 0.0f;
    bool fresh = false;
};

// TD-PSOLA для монофонического голоса: зерна по два периода с окном Ханна, шаг синтеза — период / ratio.
// Постоянная задержка getLatency(). Без высоты — зерна фиксированной длины (как гранулярный шифтер).
class Psola
{
public:
    void prepare (double sampleRate, float minHz = 60.0f);
    void reset();
    float process (float x, float periodSamples, float ratio) noexcept;
    int getLatency() const noexcept { return latency; }

private:
    std::vector<float> in, out, wsum;
    int mask = 0, w = 0, latency = 0, maxPeriod = 800;
    double synth = 0.0, ana = 0.0;
    double sr = 48000.0;
};

// Синусоидальный LFO с плавной фазой
struct Phasor
{
    double phase = 0.0;
    float next (double hz, double sr) noexcept
    {
        phase += hz / sr;
        phase -= std::floor (phase);
        return (float) std::sin (kTwoPi * phase);
    }
};

// Сглаживание одним полюсом с постоянной времени в мс
struct Smooth
{
    float y = 0.0f, a = 0.0f;
    void setTime (double ms, double sr) noexcept { a = ms <= 0.0 ? 0.0f : (float) std::exp (-1.0 / (0.001 * ms * sr)); }
    float next (float x) noexcept { y = x + a * (y - x); return y; }
};
} // namespace comma::dsp
