#include "Rack.h"

namespace comma
{
// ===========================================================================
// RackLayout: биты 0–3 — число слотов, 4 + 4·i — номер модуля слота i, 36 + i — «‖ с предыдущим»
uint64_t RackLayout::pack() const noexcept
{
    const int n = juce::jlimit (0, kMaxSlots, count);
    uint64_t b = (uint64_t) n;
    for (int i = 0; i < n; ++i)
    {
        b |= (uint64_t) (module[(size_t) i] & 0xF) << (4 + 4 * i);
        if (i > 0 && parallel[(size_t) i]) b |= (uint64_t) 1 << (36 + i);
    }
    return b;
}

RackLayout RackLayout::unpack (uint64_t b) noexcept
{
    RackLayout l;
    l.count = juce::jlimit (0, kMaxSlots, (int) (b & 0xF));
    for (int i = 0; i < l.count; ++i)
    {
        l.module[(size_t) i] = (int) ((b >> (4 + 4 * i)) & 0xF);
        l.parallel[(size_t) i] = i > 0 && ((b >> (36 + i)) & 1) != 0;
    }
    l.sanitise();
    return l;
}

int RackLayout::indexOf (int m) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (module[(size_t) i] == m) return i;
    return -1;
}

int RackLayout::groupStart (int slot) const noexcept
{
    while (slot > 0 && parallel[(size_t) slot]) --slot;
    return slot;
}

int RackLayout::groupEnd (int slot) const noexcept
{
    while (slot + 1 < count && parallel[(size_t) slot + 1]) ++slot;
    return slot;
}

void RackLayout::sanitise() noexcept
{
    RackLayout out;
    std::array<bool, kNumModules> seen {};
    for (int i = 0; i < juce::jlimit (0, kMaxSlots, count); ++i)
    {
        const int m = module[(size_t) i];
        if (m < 0 || m >= kNumModules || seen[(size_t) m]) continue;
        seen[(size_t) m] = true;
        out.module[(size_t) out.count] = m;
        out.parallel[(size_t) out.count] = out.count > 0 && parallel[(size_t) i];
        ++out.count;
    }
    *this = out;
}

bool RackLayout::add (int m)
{
    if (m < 0 || m >= kNumModules || contains (m) || count >= kMaxSlots) return false;
    module[(size_t) count] = m;
    parallel[(size_t) count] = false;
    ++count;
    return true;
}

void RackLayout::remove (int slot)
{
    if (slot < 0 || slot >= count) return;
    // если удалили начало группы, следующий слот становится её началом: остальные слоты группы остаются вместе
    const bool removedWasParallel = parallel[(size_t) slot];
    for (int i = slot; i + 1 < count; ++i)
    {
        module[(size_t) i] = module[(size_t) i + 1];
        parallel[(size_t) i] = parallel[(size_t) i + 1];
    }
    --count;
    if (slot < count) parallel[(size_t) slot] = removedWasParallel && parallel[(size_t) slot];
    sanitise();
}

void RackLayout::move (int from, int to)
{
    if (from < 0 || from >= count) return;
    to = juce::jlimit (0, count - 1, to);
    if (from == to) return;
    const int m = module[(size_t) from];
    const bool par = parallel[(size_t) from];
    if (from < to)
        for (int i = from; i < to; ++i) { module[(size_t) i] = module[(size_t) i + 1]; parallel[(size_t) i] = parallel[(size_t) i + 1]; }
    else
        for (int i = from; i > to; --i) { module[(size_t) i] = module[(size_t) i - 1]; parallel[(size_t) i] = parallel[(size_t) i - 1]; }
    module[(size_t) to] = m;
    parallel[(size_t) to] = par;
    sanitise();
}

// ===========================================================================
void IntDelay::prepare (int maxBlock)
{
    size = kMax + juce::jmax (1, maxBlock) + 1;
    for (auto& b : buf) b.assign ((size_t) size, 0.0f);
    write = 0;
}

void IntDelay::clear()
{
    for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
    write = 0;
}

void IntDelay::process (const float* const* in, float* const* out, int numChannels, int n, int delay) noexcept
{
    delay = juce::jlimit (0, kMax, delay);
    if (delay == 0 || size <= 1)
    {
        for (int c = 0; c < numChannels; ++c)
            if (out[c] != in[c]) std::copy (in[c], in[c] + n, out[c]);
        return;
    }
    for (int i = 0; i < n; ++i)
    {
        int r = write - delay;
        if (r < 0) r += size;
        for (int c = 0; c < numChannels; ++c)
        {
            auto& b = buf[(size_t) c];
            const float x = in[c][i];
            out[c][i] = b[(size_t) r];
            b[(size_t) write] = x;
        }
        if (++write >= size) write = 0;
    }
}

// ===========================================================================
Rack::Rack()
{
    for (int m = 0; m < kNumModules; ++m) modules[(size_t) m] = createModule (m);
}

void Rack::prepare (double sampleRate, int block)
{
    sr = sampleRate;
    maxBlock = juce::jmax (1, block);
    fadeLen = juce::jmax (16, (int) (0.005 * sampleRate));   // 5 мс: смена состава rack'а без щелчка
    for (auto* b : { &cur, &wet, &zbuf, &acc, &xd }) b->setSize (2, maxBlock, false, true, false);
    wsum.assign ((size_t) maxBlock, 0.0f);
    for (int m = 0; m < kNumModules; ++m)
    {
        modules[(size_t) m]->prepare (sampleRate, maxBlock);
        auto& s = slots[(size_t) m];
        s.mix.reset (sampleRate, 0.03);
        s.on.reset (sampleRate, 0.01);
        s.dry.prepare (maxBlock);
        s.pad.prepare (maxBlock);
    }
    for (auto& g : groupDry) g.prepare (maxBlock);
    activeBits = wanted.load();
    active = RackLayout::unpack (activeBits);
    fade = Fade::None;
    reset();
}

void Rack::reset()
{
    for (int m = 0; m < kNumModules; ++m)
    {
        modules[(size_t) m]->reset();
        auto& s = slots[(size_t) m];
        s.running = false;
        s.on.setCurrentAndTargetValue (0.0f);
        s.dry.clear();
        s.pad.clear();
    }
    for (auto& g : groupDry) g.clear();
}

int Rack::latencyFor (const RackLayout& l) const noexcept
{
    int total = 0;
    for (int i = 0; i < l.count; i = l.groupEnd (i) + 1)
    {
        int mx = 0;
        for (int s = i; s <= l.groupEnd (i); ++s) mx = juce::jmax (mx, modules[(size_t) l.module[(size_t) s]]->getLatency());
        total += mx;
    }
    return total;
}

void Rack::switchLayout() noexcept
{
    const auto next = RackLayout::unpack (activeBits = wanted.load());
    for (int m = 0; m < kNumModules; ++m)
    {
        auto& s = slots[(size_t) m];
        if (next.contains (m) && ! active.contains (m))
        {
            modules[(size_t) m]->reset();
            s.running = false;
            s.on.setCurrentAndTargetValue (0.0f);
        }
        s.dry.clear();
        s.pad.clear();
    }
    for (auto& g : groupDry) g.clear();
    active = next;
}

void Rack::processGroup (int first, int last, int numCh, int n, const Controls& c, const ProcessContext& ctx) noexcept
{
    int lmax = 0;
    for (int s = first; s <= last; ++s) lmax = juce::jmax (lmax, modules[(size_t) active.module[(size_t) s]]->getLatency());

    groupDry[(size_t) first].process (cur.getArrayOfReadPointers(), xd.getArrayOfWritePointers(), numCh, n, lmax);
    for (int ch = 0; ch < numCh; ++ch) acc.clear (ch, 0, n);
    std::fill (wsum.begin(), wsum.begin() + n, 0.0f);

    for (int s = first; s <= last; ++s)
    {
        const int m = active.module[(size_t) s];
        auto& st = slots[(size_t) m];
        auto& mod = *modules[(size_t) m];
        const int lat = juce::jlimit (0, IntDelay::kMax, mod.getLatency());
        st.mix.setTargetValue (juce::jlimit (0.0f, 1.0f, c.mix[(size_t) m]));
        st.on.setTargetValue (c.on[(size_t) m] ? 1.0f : 0.0f);
        if (! st.running && c.on[(size_t) m]) { mod.reset(); st.running = true; }

        // мокрый: модуль обрабатывает копию входа; выключенный и затихший — пропускается
        for (int ch = 0; ch < numCh; ++ch) wet.copyFrom (ch, 0, cur, ch, 0, n);
        if (st.running) mod.process (wet.getArrayOfWritePointers(), numCh, n, ctx);

        // сухой — вход, задержанный на задержку модуля
        st.dry.process (cur.getArrayOfReadPointers(), zbuf.getArrayOfWritePointers(), numCh, n, lat);
        auto* const* z = zbuf.getArrayOfWritePointers();
        auto* const* w = wet.getArrayOfReadPointers();
        if (st.running)
            for (int i = 0; i < n; ++i)
            {
                const float mx = st.mix.getNextValue();
                for (int ch = 0; ch < numCh; ++ch) z[ch][i] += mx * (w[ch][i] - z[ch][i]);
            }
        else
            st.mix.skip (n);

        // выравнивание по самому медленному слоту группы
        st.pad.process (zbuf.getArrayOfReadPointers(), zbuf.getArrayOfWritePointers(), numCh, n, lmax - lat);

        auto* const* a = acc.getArrayOfWritePointers();
        for (int i = 0; i < n; ++i)
        {
            const float g = st.on.getNextValue();
            wsum[(size_t) i] += g;
            if (g > 0.0f)
                for (int ch = 0; ch < numCh; ++ch) a[ch][i] += g * z[ch][i];
        }
        if (st.running && ! c.on[(size_t) m] && ! st.on.isSmoothing()) st.running = false;
    }

    auto* const* out = cur.getArrayOfWritePointers();
    auto* const* a = acc.getArrayOfReadPointers();
    auto* const* x = xd.getArrayOfReadPointers();
    for (int i = 0; i < n; ++i)
    {
        const float ws = wsum[(size_t) i];
        const float d = juce::jmax (0.0f, 1.0f - ws);
        const float norm = 1.0f / (ws + d);
        for (int ch = 0; ch < numCh; ++ch) out[ch][i] = (a[ch][i] + d * x[ch][i]) * norm;
    }
}

void Rack::process (juce::AudioBuffer<float>& io, int numSamples, const Controls& c, const ProcessContext& ctx) noexcept
{
    const int numCh = juce::jmin (2, io.getNumChannels());
    if (numCh == 0) return;
    if (fade == Fade::None && wanted.load() != activeBits) { fade = Fade::Out; fadePos = 0; }

    int off = 0;
    while (off < numSamples)
    {
        int n = juce::jmin (maxBlock, numSamples - off);
        if (fade != Fade::None) n = juce::jmin (n, fadeLen - fadePos);

        for (int ch = 0; ch < numCh; ++ch) cur.copyFrom (ch, 0, io, ch, off, n);
        for (int i = 0; i < active.count; i = active.groupEnd (i) + 1)
            processGroup (i, active.groupEnd (i), numCh, n, c, ctx);

        if (fade != Fade::None)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = cur.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    const float t = (float) (fadePos + i) / (float) fadeLen;
                    d[i] *= fade == Fade::Out ? 1.0f - t : t;
                }
            }
            fadePos += n;
            if (fadePos >= fadeLen)
            {
                fadePos = 0;
                if (fade == Fade::Out) { switchLayout(); fade = Fade::In; }
                else fade = wanted.load() != activeBits ? Fade::Out : Fade::None;
            }
        }
        for (int ch = 0; ch < numCh; ++ch) io.copyFrom (ch, off, cur, ch, 0, n);
        off += n;
    }
    if (io.getNumChannels() > numCh)
        for (int ch = numCh; ch < io.getNumChannels(); ++ch) io.clear (ch, 0, numSamples);
}
} // namespace comma
