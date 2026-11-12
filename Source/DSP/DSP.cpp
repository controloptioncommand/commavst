#include "DSP.h"

namespace comma::dsp
{
namespace
{
int pow2AtLeast (int n) { int p = 1; while (p < n) p <<= 1; return p; }
}

// ===========================================================================
void DelayLine::prepare (int maxDelay)
{
    const int size = pow2AtLeast (juce::jmax (8, maxDelay + 8));
    buf.assign ((size_t) size, 0.0f);
    mask = size - 1;
    w = 0;
}

void DelayLine::clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }

float DelayLine::read (float d) const noexcept
{
    d = juce::jlimit (3.0f, (float) (mask - 4), d);
    const float pos = (float) w - d;
    const float fl = std::floor (pos);
    const int i = (int) fl;
    const float t = pos - fl;
    return hermite (buf[(size_t) ((i - 1) & mask)], buf[(size_t) (i & mask)], buf[(size_t) ((i + 1) & mask)], buf[(size_t) ((i + 2) & mask)], t);
}

// ===========================================================================
void DelayShifter::prepare (double sampleRate, float maxWindowMs, float maxBaseMs)
{
    sr = sampleRate;
    line.prepare ((int) std::ceil ((maxWindowMs + maxBaseMs) * 0.001 * sr) + 16);
    reset();
}

void DelayShifter::reset()
{
    line.clear();
    phase = 0.0f;
    win = 0.0f;
}

float DelayShifter::process (float x, float ratio, float windowMs, float base, float period) noexcept
{
    float target = juce::jlimit (8.0f, (float) line.capacity() * 0.9f, windowMs * 0.001f * (float) sr);
    if (period > 8.0f)
    {
        const float n = juce::jmax (1.0f, std::round (target / (2.0f * period)));
        target = juce::jmin ((float) line.capacity() * 0.9f, n * 2.0f * period);
    }
    win = win <= 0.0f ? target : win + 0.0005f * (target - win);
    phase += (1.0f - ratio) / win;
    phase -= std::floor (phase);
    float p2 = phase + 0.5f;
    if (p2 >= 1.0f) p2 -= 1.0f;
    const float s = std::sin (juce::MathConstants<float>::pi * phase);
    const float g1 = s * s, g2 = 1.0f - g1;
    line.push (x);
    return g1 * line.read (base + phase * win + 3.0f) + g2 * line.read (base + p2 * win + 3.0f);
}

// ===========================================================================
void Biquad::set (Type t, double fs, double hz, double q) noexcept
{
    hz = juce::jlimit (10.0, 0.45 * fs, hz);
    const double w0 = kTwoPi * hz / fs, c = std::cos (w0), al = std::sin (w0) / (2.0 * juce::jmax (0.05, q));
    double nb0 = 1, nb1 = 0, nb2 = 0;
    switch (t)
    {
        case LowPass:  nb0 = (1.0 - c) * 0.5; nb1 = 1.0 - c; nb2 = nb0; break;
        case HighPass: nb0 = (1.0 + c) * 0.5; nb1 = -(1.0 + c); nb2 = nb0; break;
        case BandPass: nb0 = al; nb1 = 0.0; nb2 = -al; break;
    }
    const double a0 = 1.0 + al;
    b0 = (float) (nb0 / a0); b1 = (float) (nb1 / a0); b2 = (float) (nb2 / a0);
    a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
}

// ===========================================================================
float Hilbert::Chain::run (float x) noexcept
{
    for (size_t i = 0; i < 4; ++i)
    {
        const float y = c[i] * (x + y2[i]) - x2[i];
        x2[i] = x1[i]; x1[i] = x;
        y2[i] = y1[i]; y1[i] = y;
        x = y;
    }
    return x;
}

void Hilbert::reset() noexcept
{
    for (auto* ch : { &a, &b }) { ch->x1.fill (0); ch->x2.fill (0); ch->y1.fill (0); ch->y2.fill (0); }
    aDelay = 0.0f;
}

void Hilbert::process (float x, float& re, float& im) noexcept
{
    // цепочка a задержана на семпл: разность фаз с b — 90° (зеркальная частота подавлена примерно на 50 дБ)
    re = aDelay;
    aDelay = a.run (x);
    im = b.run (x);
}

// ===========================================================================
void YinTracker::prepare (double sampleRate, float minHz, float maxHz)
{
    sr = sampleRate;
    dec = juce::jmax (1, (int) std::round (sr / 12000.0));
    const double fsd = sr / dec;
    maxTau = (int) std::ceil (fsd / minHz) + 2;
    minTau = juce::jmax (2, (int) std::floor (fsd / maxHz));
    win = (int) std::round (0.040 * fsd);
    hop = juce::jmax (8, (int) std::round (0.005 * fsd));
    fineWin = (int) std::round (0.020 * sr);
    const int lowSize = pow2AtLeast (win + maxTau + 2 * hop + 8);
    low.assign ((size_t) lowSize, 0.0f);
    lowMask = lowSize - 1;
    const int fullSize = pow2AtLeast (fineWin + (maxTau + 2) * dec + 64);
    full.assign ((size_t) fullSize, 0.0f);
    fullMask = fullSize - 1;
    diff.assign ((size_t) maxTau + 2, 0.0f);
    reset();
}

void YinTracker::reset()
{
    std::fill (low.begin(), low.end(), 0.0f);
    std::fill (full.begin(), full.end(), 0.0f);
    lowW = fullW = decCount = hopCount = 0;
    decAcc = 0.0f;
    hz = period = confidence = 0.0f;
    fresh = false;
}

void YinTracker::push (float x) noexcept
{
    fresh = false;
    full[(size_t) fullW] = x;
    fullW = (fullW + 1) & fullMask;
    decAcc += x;
    if (++decCount < dec) return;
    low[(size_t) lowW] = decAcc / (float) dec;
    lowW = (lowW + 1) & lowMask;
    decAcc = 0.0f;
    decCount = 0;
    if (++hopCount >= hop) { hopCount = 0; analyse(); fresh = true; }
}

void YinTracker::analyse() noexcept
{
    // грубо: разностная функция и CMNDF на прореженном сигнале
    const int s = lowW - win - maxTau;
    float energy = 0.0f;
    for (int j = 0; j < win; ++j) { const float v = low[(size_t) ((s + j) & lowMask)]; energy += v * v; }
    if (energy < 1.0e-6f * (float) win) { period = hz = 0.0f; confidence = 0.0f; return; }

    diff[0] = 1.0f;
    float run = 0.0f;
    for (int tau = 1; tau <= maxTau; ++tau)
    {
        float d = 0.0f;
        for (int j = 0; j < win; ++j)
        {
            const float a = low[(size_t) ((s + j) & lowMask)] - low[(size_t) ((s + j + tau) & lowMask)];
            d += a * a;
        }
        run += d;
        diff[(size_t) tau] = run > 0.0f ? d * (float) tau / run : 1.0f;
    }
    int best = -1;
    for (int tau = minTau; tau < maxTau; ++tau)
        if (diff[(size_t) tau] < 0.12f)
        {
            while (tau + 1 < maxTau && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
            best = tau;
            break;
        }
    if (best < 0)
    {
        best = minTau;
        for (int tau = minTau; tau < maxTau; ++tau) if (diff[(size_t) tau] < diff[(size_t) best]) best = tau;
        if (diff[(size_t) best] > 0.35f) { period = hz = 0.0f; confidence = 1.0f - diff[(size_t) best]; return; }
    }
    confidence = juce::jlimit (0.0f, 1.0f, 1.0f - diff[(size_t) best]);

    // уточнение на полной частоте
    const int centre = best * dec;
    const int lo = juce::jmax (2, centre - dec - 1), hi = centre + dec + 1;
    const int fs = fullW - fineWin - hi - 2;
    float bestD = 1.0e30f, dPrev = 0.0f, dNext = 0.0f;
    int bestT = centre;
    float prev = 0.0f;
    std::array<float, 64> fineD {};
    const int span = juce::jmin (63, hi - lo);
    for (int k = 0; k <= span; ++k)
    {
        const int tau = lo + k;
        float d = 0.0f;
        for (int j = 0; j < fineWin; ++j)
        {
            const float a = full[(size_t) ((fs + j) & fullMask)] - full[(size_t) ((fs + j + tau) & fullMask)];
            d += a * a;
        }
        fineD[(size_t) k] = d;
        if (d < bestD) { bestD = d; bestT = tau; }
        prev = d;
    }
    juce::ignoreUnused (prev);
    const int k = bestT - lo;
    float p = (float) bestT;
    if (k > 0 && k < span)
    {
        dPrev = fineD[(size_t) k - 1];
        dNext = fineD[(size_t) k + 1];
        const float den = dPrev - 2.0f * bestD + dNext;
        if (std::abs (den) > 1.0e-12f) p += juce::jlimit (-0.5f, 0.5f, 0.5f * (dPrev - dNext) / den);
    }
    period = p;
    hz = (float) (sr / p);
}

// ===========================================================================
void Psola::prepare (double sampleRate, float minHz)
{
    sr = sampleRate;
    maxPeriod = (int) std::ceil (sr / minHz);
    latency = 3 * maxPeriod + 4;
    const int size = pow2AtLeast (latency + 4 * maxPeriod + 64);
    in.assign ((size_t) size, 0.0f);
    out.assign ((size_t) size, 0.0f);
    wsum.assign ((size_t) size, 0.0f);
    mask = size - 1;
    reset();
}

void Psola::reset()
{
    std::fill (in.begin(), in.end(), 0.0f);
    std::fill (out.begin(), out.end(), 0.0f);
    std::fill (wsum.begin(), wsum.end(), 0.0f);
    w = 0;
    synth = ana = -1.0e9;
}

float Psola::process (float x, float P, float r) noexcept
{
    in[(size_t) (w & mask)] = x;
    P = juce::jlimit (16.0f, (float) maxPeriod, P > 0.0f ? P : (float) (0.008 * sr));
    r = juce::jlimit (0.25f, 4.0f, r);
    const double tOut = (double) w - latency;
    if (synth < tOut) synth = ana = tOut + P;

    while (synth + 1.5 * P <= (double) w)
    {
        if (ana > synth + P) ana = synth;
        while (ana + 0.5 * P < synth) ana += P;
        const int ci = (int) std::lround (ana), co = (int) std::lround (synth), half = (int) std::lround (P);
        const float inv = juce::MathConstants<float>::pi / (float) half;
        for (int k = -half; k <= half; ++k)
        {
            const float g = 0.5f * (1.0f + std::cos (inv * (float) k));
            out[(size_t) ((co + k) & mask)] += in[(size_t) ((ci + k) & mask)] * g;
            wsum[(size_t) ((co + k) & mask)] += g;
        }
        synth += P / r;
    }
    const int o = (int) tOut & mask;
    const float y = out[(size_t) o] / juce::jmax (wsum[(size_t) o], juce::jmin (1.0f, r));
    out[(size_t) o] = wsum[(size_t) o] = 0.0f;
    if (++w >= (1 << 30))   // счётчик не переполняется: сдвиг на кратное размеру буфера
    {
        w -= (1 << 29);
        synth -= (double) (1 << 29);
        ana -= (double) (1 << 29);
    }
    return y;
}
} // namespace comma::dsp
