#include "Modules.h"

namespace comma
{
// Spectral mapper: STFT (Ханн, перекрытие 75 %), пики спектра, их точная частота по разности фаз,
// перенос каждого пика со своей окрестностью на целевую частоту с привязкой фаз (Laroche — Dolson).
// Задержка — длина окна: 2048 (Quality) или 1024 (Low latency).
namespace
{
constexpr int kMaxN = 2048;
constexpr int kMaxPeaks = 160;
}

void SpectralModule::prepare (double s, int)
{
    sr = s;
    fft10 = std::make_unique<juce::dsp::FFT> (10);
    fft11 = std::make_unique<juce::dsp::FFT> (11);
    // масштаб обратного преобразования JUCE — измеряем на импульсе
    for (int o = 0; o < 2; ++o)
    {
        auto* f = o == 0 ? fft10.get() : fft11.get();
        std::vector<float> d ((size_t) (2 << (10 + o)), 0.0f);
        d[0] = 1.0f;
        f->performRealOnlyForwardTransform (d.data(), true);
        f->performRealOnlyInverseTransform (d.data());
        invScale[o] = std::abs (d[0]) > 1.0e-9f ? 1.0f / d[0] : 1.0f;
    }
    window.assign ((size_t) kMaxN, 0.0f);
    mag.assign ((size_t) kMaxN / 2 + 2, 0.0f);
    phs.assign ((size_t) kMaxN / 2 + 2, 0.0f);
    spec.assign ((size_t) kMaxN * 2, 0.0f);
    outSpec.assign ((size_t) kMaxN * 2, 0.0f);
    peaks.assign ((size_t) kMaxPeaks, 0);
    for (auto& c : chans)
    {
        c.in.assign ((size_t) kMaxN, 0.0f);
        c.out.assign ((size_t) kMaxN, 0.0f);
        c.frame.assign ((size_t) kMaxN, 0.0f);
        c.prevPhase.assign ((size_t) kMaxN / 2 + 2, 0.0f);
        c.outPhase.assign ((size_t) kMaxN / 2 + 2, 0.0f);
        c.peakBin.assign ((size_t) kMaxPeaks, 0.0f);
        c.peakPhase.assign ((size_t) kMaxPeaks, 0.0f);
    }
    configure (getLatency() >= 2048 ? 11 : 10);
    reset();
}

void SpectralModule::configure (int o)
{
    order = o;
    N = 1 << order;
    H = N / 4;
    fft = order == 11 ? fft11.get() : fft10.get();
    for (int j = 0; j < N; ++j) window[(size_t) j] = 0.5f - 0.5f * std::cos ((float) dsp::kTwoPi * (float) j / (float) N);
}

void SpectralModule::reset()
{
    for (auto& c : chans)
    {
        std::fill (c.in.begin(), c.in.end(), 0.0f);
        std::fill (c.out.begin(), c.out.end(), 0.0f);
        std::fill (c.prevPhase.begin(), c.prevPhase.end(), 0.0f);
        c.numPrevPeaks = 0;
        c.pos = c.hop = 0;
    }
}

int SpectralModule::computeLatency (const float* v, double) const
{
    return v != nullptr && juce::roundToInt (v[P::SpeQuality]) == 0 ? 1024 : 2048;
}

void SpectralModule::frame (Chan& c, const ProcessContext& ctx) noexcept
{
    const int K = N / 2 + 1;
    for (int j = 0; j < N; ++j) spec[(size_t) j] = c.in[(size_t) ((c.pos + j) & (N - 1))] * window[(size_t) j];
    std::fill (spec.begin() + N, spec.begin() + 2 * N, 0.0f);
    fft->performRealOnlyForwardTransform (spec.data(), true);

    const float amount = ctx.v (P::SpeAmount) * 0.01f;
    const bool stretch = ctx.i (P::SpeMode) == 1;
    const double binHz = sr / N;
    for (int k = 0; k < K; ++k)
    {
        const float re = spec[(size_t) (2 * k)], im = spec[(size_t) (2 * k + 1)];
        mag[(size_t) k] = std::sqrt (re * re + im * im);
        phs[(size_t) k] = std::atan2 (im, re);
    }

    // пики выше порога (порог — в dBFS синуса: амплитуда A даёт пик A·N/4)
    const float thr = juce::Decibels::decibelsToGain (ctx.v (P::SpeThreshold)) * (float) N * 0.25f;
    int np = 0;
    float maxMag = 0.0f;
    for (int k = 2; k < K - 2 && np < kMaxPeaks; ++k)
    {
        const float m = mag[(size_t) k];
        if (m > thr && m >= mag[(size_t) k - 1] && m >= mag[(size_t) k + 1] && m > mag[(size_t) k - 2] && m > mag[(size_t) k + 2])
        {
            peaks[(size_t) np++] = k;
            maxMag = juce::jmax (maxMag, m);
        }
    }

    bool modify = amount > 0.001f && np > 0 && ctx.tuning != nullptr;
    if (modify && stretch && std::abs (ctx.tuning->periodCents - 1200.0) < 0.01) modify = false;   // октавный строй — растягивать нечего
    if (! modify)
    {
        std::copy (spec.begin(), spec.begin() + 2 * K, outSpec.begin());
        for (int k = 0; k < K; ++k) c.prevPhase[(size_t) k] = phs[(size_t) k];
        c.numPrevPeaks = 0;
    }
    else
    {
        std::fill (outSpec.begin(), outSpec.begin() + 2 * N, 0.0f);
        // основной тон для Stretch — нижний заметный пик
        double f0 = 0.0;
        const double expo = 1.0 + amount * (ctx.tuning->periodCents / 1200.0 - 1.0);
        int newPeaks = 0;
        std::array<float, kMaxPeaks> nb {}, nph {};
        for (int p = 0; p < np; ++p)
        {
            const int k = peaks[(size_t) p];
            double dphi = phs[(size_t) k] - c.prevPhase[(size_t) k] - dsp::kTwoPi * k * H / N;
            dphi -= dsp::kTwoPi * std::round (dphi / dsp::kTwoPi);
            const double fb = k + dphi * N / (dsp::kTwoPi * H);   // точная частота в бинах
            const double f = fb * binHz;
            if (f0 <= 0.0 && mag[(size_t) k] > 0.1f * maxMag && f > 30.0) f0 = f;
            double ft;
            if (stretch) ft = f0 > 0.0 ? f0 * std::pow (f / f0, expo) : f;
            else
            {
                const double c0 = hzToCents (f);
                ft = centsToHz (c0 + amount * (ctx.tuning->nearest (c0) - c0));
            }
            const int s = (int) std::lround (ft / binHz - fb);
            const int kp = k + s;
            const int lo = p == 0 ? 0 : (peaks[(size_t) p - 1] + k) / 2 + 1;
            const int hi = p == np - 1 ? K - 1 : (k + peaks[(size_t) p + 1]) / 2;
            if (kp < 1 || kp >= K - 1) continue;
            // фаза пика: продолжение прошлого кадра, если там был пик рядом, иначе — фаза входа
            double ph = phs[(size_t) k];
            for (int q = 0; q < c.numPrevPeaks; ++q)
                if (std::abs (c.peakBin[(size_t) q] - (float) kp) <= 2.0f) { ph = c.peakPhase[(size_t) q] + dsp::kTwoPi * (ft / binHz) * H / N; break; }
            ph -= dsp::kTwoPi * std::floor (ph / dsp::kTwoPi);
            if (newPeaks < kMaxPeaks) { nb[(size_t) newPeaks] = (float) kp; nph[(size_t) newPeaks] = (float) ph; ++newPeaks; }
            for (int b = lo; b <= hi; ++b)
            {
                const int bo = b + s;
                if (bo < 0 || bo >= K) continue;
                const double th = ph + (phs[(size_t) b] - phs[(size_t) k]);
                outSpec[(size_t) (2 * bo)] += mag[(size_t) b] * (float) std::cos (th);
                outSpec[(size_t) (2 * bo + 1)] += mag[(size_t) b] * (float) std::sin (th);
            }
        }
        for (int q = 0; q < newPeaks; ++q) { c.peakBin[(size_t) q] = nb[(size_t) q]; c.peakPhase[(size_t) q] = nph[(size_t) q]; }
        c.numPrevPeaks = newPeaks;
        for (int k = 0; k < K; ++k) c.prevPhase[(size_t) k] = phs[(size_t) k];
    }

    fft->performRealOnlyInverseTransform (outSpec.data());
    const float norm = invScale[order == 11 ? 1 : 0] / 1.5f;   // сумма квадратов окна Ханна при шаге N/4 — 1.5
    for (int j = 0; j < N; ++j)
        c.out[(size_t) ((c.pos + j) & (N - 1))] += outSpec[(size_t) j] * window[(size_t) j] * norm;
}

void SpectralModule::process (float* const* ch, int nc, int n, const ProcessContext& ctx)
{
    const int want = getLatency() >= 2048 ? 11 : 10;
    if (want != order) { configure (want); reset(); }
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < juce::jmin (2, nc); ++k)
        {
            auto& c = chans[(size_t) k];
            c.in[(size_t) c.pos] = ch[k][i];
            ch[k][i] = c.out[(size_t) c.pos];
            c.out[(size_t) c.pos] = 0.0f;
            c.pos = (c.pos + 1) & (N - 1);
            if (++c.hop >= H) { c.hop = 0; frame (c, ctx); }
        }
}
} // namespace comma
