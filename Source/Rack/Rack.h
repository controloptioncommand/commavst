#pragma once
#include "Module.h"
#include "Params.h"
#include <array>
#include <vector>

namespace comma
{
// Состав rack'а: модули по порядку и флаг «‖ с предыдущим» (слот входит в параллельную группу предыдущего).
// Упаковывается в 64 бита, поэтому в аудиопоток передаётся одним атомарным числом — без блокировок.
struct RackLayout
{
    int count = 0;
    std::array<int, kMaxSlots> module {};
    std::array<bool, kMaxSlots> parallel {};

    uint64_t pack() const noexcept;
    static RackLayout unpack (uint64_t bits) noexcept;
    bool operator== (const RackLayout& o) const noexcept { return pack() == o.pack(); }
    bool operator!= (const RackLayout& o) const noexcept { return pack() != o.pack(); }

    bool contains (int m) const noexcept { return indexOf (m) >= 0; }
    int indexOf (int m) const noexcept;
    bool isParallel (int slot) const noexcept { return slot > 0 && slot < count && parallel[(size_t) slot]; }
    // группа слота: [first, last]
    int groupStart (int slot) const noexcept;
    int groupEnd (int slot) const noexcept;
    void sanitise() noexcept;   // без повторов и чужих номеров; у первого слота нет «‖»

    // правки (поток сообщений)
    bool add (int m);
    void remove (int slot);
    void move (int from, int to);
};

// Целочисленная задержка на 2 канала, буфер — заранее
class IntDelay
{
public:
    static constexpr int kMax = 16384;
    void prepare (int maxBlock);
    void clear();
    // in → out (можно на месте), задержка delay ≤ kMax
    void process (const float* const* in, float* const* out, int numChannels, int n, int delay) noexcept;

private:
    std::array<std::vector<float>, 2> buf;
    int size = 1, write = 0;
};

// Rack: последовательные группы, внутри группы — параллельные слоты.
// Слот: z = dry + mix · (wet − dry), где dry — вход, задержанный на задержку модуля.
// Группа: y = (Σ wₖ·zₖ + d·x) / (Σ wₖ + d), wₖ — включён ли слот (сглаживается), d = max (0, 1 − Σ wₖ).
// Слоты группы выравниваются по самому медленному; плагин сообщает хосту сумму задержек групп.
class Rack
{
public:
    Rack();
    void prepare (double sampleRate, int maxBlock);
    void reset();

    void setLayout (const RackLayout& l) noexcept { wanted.store (l.pack()); }   // любой поток
    RackLayout getLayout() const noexcept { return RackLayout::unpack (wanted.load()); }
    int latencyFor (const RackLayout& l) const noexcept;
    int getLatency() const noexcept { return latencyFor (getLayout()); }

    struct Controls { std::array<float, kNumModules> mix {}; std::array<bool, kNumModules> on {}; };
    void process (juce::AudioBuffer<float>& io, int numSamples, const Controls& controls, const ProcessContext& ctx) noexcept;

    Module& getModule (int m) { return *modules[(size_t) m]; }
    void setModuleForTest (int m, std::unique_ptr<Module> mod) { modules[(size_t) m] = std::move (mod); }

private:
    void switchLayout() noexcept;
    void processGroup (int first, int last, int numCh, int n, const Controls& c, const ProcessContext& ctx) noexcept;

    std::array<std::unique_ptr<Module>, kNumModules> modules;
    std::atomic<uint64_t> wanted { 0 };
    RackLayout active;
    uint64_t activeBits = 0;

    struct SlotState
    {
        juce::SmoothedValue<float> mix, on;
        bool running = false;   // модуль обрабатывает (выключенный и затихший — пропускается)
        IntDelay dry, pad;
    };
    std::array<SlotState, kNumModules> slots;          // по номеру модуля
    std::array<IntDelay, kMaxSlots> groupDry;           // вход группы, задержанный до выхода группы

    juce::AudioBuffer<float> cur, wet, zbuf, acc, xd;
    std::vector<float> wsum;
    double sr = 48000.0;
    int maxBlock = 512;
    int fadeLen = 240, fadePos = 0;
    enum class Fade { None, Out, In } fade = Fade::None;
};
} // namespace comma
