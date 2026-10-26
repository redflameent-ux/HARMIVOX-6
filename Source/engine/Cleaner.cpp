#include "Cleaner.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <complex>

namespace hvx
{
namespace
{
void deClick (float* x, int n, double sr, float amount)
{
    const float k = 9.0f - 6.0f * amount;               // spike threshold in local deviations
    const int winLen = (int) (0.004 * sr);
    double acc = 0; std::vector<float> dabs ((size_t) n, 0.0f);
    for (int i = 1; i < n; ++i) dabs[(size_t) i] = std::abs (x[i] - x[i - 1]);
    std::vector<float> local ((size_t) n, 0.0f);
    for (int i = 0; i < n; ++i)
    {
        acc += dabs[(size_t) i];
        if (i >= winLen) acc -= dabs[(size_t) (i - winLen)];
        local[(size_t) i] = (float) (acc / juce::jmax (1, juce::jmin (i + 1, winLen)));
    }
    const int fix = (int) (0.0015 * sr);
    for (int i = winLen; i < n - fix - 1; ++i)
    {
        if (dabs[(size_t) i] > k * local[(size_t) (i - 1)] + 0.02f)
        {
            const int a = juce::jmax (0, i - fix), b = juce::jmin (n - 1, i + fix);
            const float va = x[a], vb = x[b];
            for (int q = a + 1; q < b; ++q) x[q] = va + (vb - va) * (float) (q - a) / (float) (b - a);
            i = b;
        }
    }
}

void spectralDenoise (float* x, int n, float amount)
{
    constexpr int order = 11, N = 1 << order, H = N / 4;
    juce::dsp::FFT fft (order);
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);

    const int frames = n / H + 2;
    std::vector<std::vector<float>> mags ((size_t) frames, std::vector<float> (N / 2 + 1));
    std::vector<float> energy ((size_t) frames, 0.0f);
    std::vector<float> buf ((size_t) N * 2);

    for (int f = 0; f < frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        const int s = f * H - N / 2;
        for (int i = 0; i < N; ++i) { const int j = s + i; buf[(size_t) i] = (j >= 0 && j < n ? x[j] : 0.0f) * win[(size_t) i]; }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        double e = 0;
        for (int b = 0; b <= N / 2; ++b) { mags[(size_t) f][(size_t) b] = buf[(size_t) b]; e += buf[(size_t) b]; }
        energy[(size_t) f] = (float) e;
    }

    // noise floor = quiet frames' typical spectrum
    std::vector<float> sorted = energy;
    std::sort (sorted.begin(), sorted.end());
    const float quietCut = sorted[(size_t) juce::jmin ((int) sorted.size() - 1, (int) (sorted.size() * 0.2))];
    std::vector<float> noise ((size_t) N / 2 + 1, 0.0f);
    {
        std::vector<float> col;
        for (int b = 0; b <= N / 2; ++b)
        {
            col.clear();
            for (int f = 0; f < frames; ++f) if (energy[(size_t) f] <= quietCut && energy[(size_t) f] > 0) col.push_back (mags[(size_t) f][(size_t) b]);
            if (col.empty()) continue;
            std::nth_element (col.begin(), col.begin() + (long) (col.size() / 2), col.end());
            noise[(size_t) b] = col[col.size() / 2];
        }
    }

    const float over = 1.0f + 2.5f * amount;
    const float floorG = juce::Decibels::decibelsToGain (-6.0f - 22.0f * amount);
    std::vector<float> out ((size_t) n + N, 0.0f), wsum ((size_t) n + N, 0.0f);
    std::vector<float> prevG ((size_t) N / 2 + 1, 1.0f);
    std::vector<std::complex<float>> spec ((size_t) N), tmp ((size_t) N);
    juce::dsp::FFT cfft (order);

    for (int f = 0; f < frames; ++f)
    {
        const int s = f * H - N / 2;
        for (int i = 0; i < N; ++i) { const int j = s + i; spec[(size_t) i] = (j >= 0 && j < n ? x[j] : 0.0f) * win[(size_t) i]; }
        cfft.perform (spec.data(), tmp.data(), false);
        for (int b = 0; b <= N / 2; ++b)
        {
            const float m = std::abs (tmp[(size_t) b]) + 1e-9f;
            const float r = over * noise[(size_t) b] / m;
            float g = juce::jmax (floorG, std::sqrt (juce::jmax (0.0f, 1.0f - r * r)));
            // release smoothing to avoid musical noise
            g = juce::jmax (g, prevG[(size_t) b] * 0.6f);
            prevG[(size_t) b] = g;
            tmp[(size_t) b] *= g;
            if (b > 0 && b < N / 2) tmp[(size_t) (N - b)] *= g;
        }
        cfft.perform (tmp.data(), spec.data(), true);
        for (int i = 0; i < N; ++i)
        {
            const int j = s + i;
            if (j < 0 || j >= n) continue;
            out[(size_t) j] += spec[(size_t) i].real() * win[(size_t) i];
            wsum[(size_t) j] += win[(size_t) i] * win[(size_t) i];
        }
    }
    for (int i = 0; i < n; ++i) x[i] = wsum[(size_t) i] > 1e-6f ? out[(size_t) i] / wsum[(size_t) i] : 0.0f;
}
} // namespace

juce::AudioBuffer<float> Cleaner::process (const SourceAudio& src, const std::vector<Note>& notes, const CleanSettings& s)
{
    juce::AudioBuffer<float> buf (src.audio);
    if (isNeutral (s)) return buf;

    const int n = buf.getNumSamples();
    float* x = buf.getWritePointer (0);
    const double sr = src.sampleRate;

    if (s.deClick > 0.001f)     deClick (x, n, sr, s.deClick);
    if (s.noiseReduce > 0.001f) spectralDenoise (x, n, s.noiseReduce);

    if (s.gate > 0.001f || s.breathDb > 0.01f)
    {
        const auto& fr = src.frames;
        const int nf = (int) fr.size();
        float peak = -120.0f;
        for (auto& f : fr) peak = juce::jmax (peak, f.rmsDb);

        std::vector<float> gDb ((size_t) nf, 0.0f);
        const double fs = src.hop / sr;

        auto nearNote = [&] (double t, double margin)
        {
            for (auto& nt : notes) if (t >= nt.srcStart - margin && t <= nt.srcEnd + margin) return true;
            return false;
        };

        // breaths: long unvoiced stretches with some energy
        if (s.breathDb > 0.01f)
        {
            int i = 0;
            while (i < nf)
            {
                if (fr[(size_t) i].voiced) { ++i; continue; }
                int j = i;
                while (j < nf && ! fr[(size_t) j].voiced) ++j;
                if ((j - i) * fs >= 0.14)
                {
                    for (int k = i; k < j; ++k)
                        if (fr[(size_t) k].rmsDb > peak - 48.0f && fr[(size_t) k].rmsDb < peak - 10.0f)
                            gDb[(size_t) k] = juce::jmin (gDb[(size_t) k], -s.breathDb);
                }
                i = j;
            }
        }
        if (s.gate > 0.001f)
        {
            const float thr = peak - (46.0f - 26.0f * s.gate);
            const float depth = -12.0f - 50.0f * s.gate;
            for (int k = 0; k < nf; ++k)
                if (fr[(size_t) k].rmsDb < thr && ! nearNote (k * fs, 0.05))
                    gDb[(size_t) k] = juce::jmin (gDb[(size_t) k], depth);
        }

        // smooth gain (attack 5 ms, release 40 ms) and apply per sample
        std::vector<float> gl ((size_t) nf);
        for (int k = 0; k < nf; ++k) gl[(size_t) k] = juce::Decibels::decibelsToGain (gDb[(size_t) k]);
        for (int k = 1; k < nf; ++k) gl[(size_t) k] = gl[(size_t) k] < gl[(size_t) k - 1] ? gl[(size_t) k - 1] + 0.35f * (gl[(size_t) k] - gl[(size_t) k - 1]) : gl[(size_t) k];
        for (int k = nf - 2; k >= 0; --k) gl[(size_t) k] = gl[(size_t) k] < gl[(size_t) k + 1] ? gl[(size_t) k + 1] + 0.5f * (gl[(size_t) k] - gl[(size_t) k + 1]) : gl[(size_t) k];
        for (int i = 0; i < n; ++i)
        {
            const double fp = (double) i / src.hop;
            const int a = juce::jmin (nf - 1, (int) fp), b = juce::jmin (nf - 1, a + 1);
            const float t = (float) (fp - a);
            x[i] *= gl[(size_t) a] + (gl[(size_t) b] - gl[(size_t) a]) * t;
        }
    }
    return buf;
}
} // namespace hvx
