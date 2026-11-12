#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace comma
{
// Высоты в Comma — «абсолютные центы»: центы над частотой MIDI-ноты 0 в 12-EDO при A4 = 440 Гц (8.1758 Гц).
// Тогда нота n в 12-EDO = 100·n центов, а сдвиг на интервал — простое сложение.
inline constexpr double kMidi0Hz = 8.175798915643707;
inline double centsToHz (double c) noexcept { return kMidi0Hz * std::exp2 (c / 1200.0); }
inline double hzToCents (double hz) noexcept { return 1200.0 * std::log2 (juce::jmax (1.0e-6, hz) / kMidi0Hz); }
inline double ratioToCents (double r) noexcept { return 1200.0 * std::log2 (juce::jmax (1.0e-9, r)); }
inline double centsToRatio (double c) noexcept { return std::exp2 (c / 1200.0); }

// Неизменяемый снимок строя. Собирается в потоке сообщений, в аудиопоток передаётся готовым (TuningHolder).
// Все методы — без выделений памяти, их можно звать из аудиопотока.
struct TuningSnapshot
{
    static constexpr int kMaxSteps = 4096;
    static constexpr double kTop = 15000.0;   // ~20 кГц

    std::array<double, 128> noteCents {};      // высота каждой MIDI-ноты (по .kbm)
    std::array<float, kMaxSteps> steps {};     // все ступени строя по возрастанию, 0 … kTop
    int numSteps = 0;
    double periodCents = 1200.0;               // период (октава или, например, тритава Болена — Пирса)
    int degrees = 12;                          // ступеней в периоде
    double meanStep = 100.0;                   // средний шаг = период / ступени
    double rootCents = 6000.0;                 // где стоит ступень 0 (для круга строя)

    // для интерфейса (только поток сообщений)
    std::vector<double> degreeCents;           // ступени внутри периода, от 0
    std::vector<juce::String> degreeNames;
    juce::String name, detail;

    int nearestIndex (double c) const noexcept;
    double nearest (double c) const noexcept { return numSteps > 0 ? (double) steps[(size_t) nearestIndex (c)] : c; }
    double step (int i) const noexcept { return numSteps > 0 ? (double) steps[(size_t) juce::jlimit (0, numSteps - 1, i)] : 0.0; }
    // сдвиг высоты на k ступеней строя с сохранением отклонения от ближайшей ступени
    double shiftSteps (double c, int k) const noexcept;
    double noteHz (int note) const noexcept { return centsToHz (noteCents[(size_t) juce::jlimit (0, 127, note)]); }
    // ступень внутри периода (0 … degrees−1) для высоты c
    int degreeOf (double c) const noexcept;
};

using TuningPtr = std::shared_ptr<const TuningSnapshot>;

// Источник строя
enum class TuningSource { EDO12 = 0, EDO, Ratios, Scala, MTS };

struct TuningSpec
{
    TuningSource source = TuningSource::EDO12;
    int edo = 12;
    int root = 0;              // 0 = C … 11 = B: где стоит ступень 0 (для EDO, отношений и .scl без .kbm)
    double refHz = 440.0;      // A4 в 12-EDO, от него считается частота корня
    juce::String ratios;       // «1/1 9/8 5/4 … 2/1»
    juce::String scl, kbm;     // тексты файлов Scala
    juce::String sclName, kbmName;
};

// Сборка снимка. Ошибки разбора — в error, снимок тогда 12-EDO.
TuningPtr buildTuning (const TuningSpec& spec, juce::String& error);
TuningPtr buildTuningFromNoteHz (const std::array<double, 128>& hz, const juce::String& name, double periodCents);   // MTS-ESP
TuningPtr defaultTuning();

// Отношения «3/2 7/4 11/8» или центы «701.96» → текст .scl. Ошибка — пустой результат и error.
juce::String ratiosToScl (const juce::String& text, juce::String& error);
juce::String defaultRatios();   // 5-limit just intonation, 12 ступеней

// Передача снимка в аудиопоток без блокировок: кольцо из 32 указателей держит память,
// аудиопоток читает сырой указатель в начале блока и не освобождает его.
class TuningHolder
{
public:
    TuningHolder();
    void publish (TuningPtr t);                                            // поток сообщений
    const TuningSnapshot* get() const noexcept { return current.load(); }  // любой поток
    TuningPtr latest() const { return ring[(size_t) ((next + 31) % 32)]; }

private:
    std::array<TuningPtr, 32> ring;
    int next = 0;
    std::atomic<const TuningSnapshot*> current { nullptr };
};

// ---------------------------------------------------------------------------
// Интервал в одной из трёх единиц
enum class IntervalUnit { Cents = 0, Ratio, Steps };
const juce::StringArray& ratioNames();      // «1/2» … «1/1» … «2/1»
double ratioValue (int index);
int ratioIndex (int num, int den);          // −1, если нет в списке
int unisonRatioIndex();

// Сдвиг в центах. Шаги: если известна высота from — настоящие ступени строя вокруг неё,
// иначе (модуль не знает высоту входа) — шаги × средний шаг строя.
double intervalCents (IntervalUnit unit, double cents, int ratioIdx, int steps, const TuningSnapshot* t,
                      double from = std::numeric_limits<double>::quiet_NaN()) noexcept;

juce::String noteName12 (int note);         // C3 = 60, как в Ableton
juce::String formatCents (double c, int decimals = 1);   // «+12.3 ¢»
} // namespace comma
