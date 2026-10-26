#include "Aligner.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

namespace hvx
{
void Aligner::tidy (std::vector<WarpPoint>& w)
{
    std::sort (w.begin(), w.end(), [] (auto& a, auto& b) { return a.src < b.src; });
    std::vector<WarpPoint> out;
    for (auto& p : w)
    {
        if (! out.empty() && p.src - out.back().src < 0.002) { out.back() = p; continue; }
        out.push_back (p);
    }
    for (size_t i = 1; i < out.size(); ++i)
    {
        const double minDst = out[i - 1].dst + juce::jmax (0.002, (out[i].src - out[i - 1].src) * 0.15);
        if (out[i].dst < minDst) out[i].dst = minDst;
    }
    w = std::move (out);
}

void Aligner::setWarp (Track& t, double srcSec, double dstSec)
{
    for (auto& p : t.warps)
        if (std::abs (p.src - srcSec) < 0.002) { p.dst = dstSec; tidy (t.warps); return; }
    t.warps.push_back ({ srcSec, dstSec });
    tidy (t.warps);
}

void Aligner::quantize (Track& t, double bpm, int division, float amount)
{
    if (bpm <= 0 || t.notes.empty() || amount <= 0.0f) return;
    const double grid = (60.0 / bpm) * 4.0 / juce::jmax (1, division);
    // warp markers on note starts only; the audio between them stretches to fit
    std::vector<WarpPoint> pts;
    double lastShift = 0;
    for (auto& n : t.notes)
    {
        const double ds = srcToDst (t.warps, n.srcStart);
        const double host = t.clipStart + ds;
        const double snapped = std::round (host / grid) * grid;
        lastShift = (snapped - host) * amount;
        pts.push_back ({ n.srcStart, ds + lastShift });
    }
    const auto& last = t.notes.back();
    pts.push_back ({ last.srcEnd, srcToDst (t.warps, last.srcEnd) + lastShift });
    t.warps = pts;
    tidy (t.warps);
}

//==============================================================================
namespace
{
std::vector<std::array<float, 13>> features (const SourceAudio& s, int hop)
{
    constexpr int order = 10, N = 1 << order, nb = 24;
    juce::dsp::FFT fft (order);
    const int n = s.numSamples();
    const float* x = s.audio.getReadPointer (0);
    const double sr = s.sampleRate;

    // mel filterbank edges
    auto hz2mel = [] (double h) { return 2595.0 * std::log10 (1.0 + h / 700.0); };
    auto mel2hz = [] (double m) { return 700.0 * (std::pow (10.0, m / 2595.0) - 1.0); };
    std::vector<double> edges (nb + 2);
    for (int i = 0; i < nb + 2; ++i)
        edges[(size_t) i] = mel2hz (hz2mel (80.0) + (hz2mel (8000.0) - hz2mel (80.0)) * i / (nb + 1));

    std::vector<std::array<float, 13>> out;
    std::vector<float> buf ((size_t) N * 2);
    for (int c = 0; c < n; c += hop)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i)
        {
            const int j = c - N / 2 + i;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
            buf[(size_t) i] = (j >= 0 && j < n ? x[j] : 0.0f) * w;
        }
        fft.performFrequencyOnlyForwardTransform (buf.data(), true);
        float mel[nb];
        for (int b = 0; b < nb; ++b)
        {
            double e = 0;
            const double lo = edges[(size_t) b], mid = edges[(size_t) b + 1], hi = edges[(size_t) b + 2];
            for (int k = (int) (lo * N / sr); k <= (int) (hi * N / sr) && k <= N / 2; ++k)
            {
                const double f = k * sr / N;
                const double w = f < mid ? (f - lo) / (mid - lo) : (hi - f) / (hi - mid);
                if (w > 0) e += w * buf[(size_t) k] * buf[(size_t) k];
            }
            mel[b] = (float) std::log (e + 1e-8);
        }
        std::array<float, 13> f {};
        for (int q = 0; q < 13; ++q)
        {
            double acc = 0;
            for (int b = 0; b < nb; ++b) acc += mel[b] * std::cos (juce::MathConstants<double>::pi * q * (b + 0.5) / nb);
            f[(size_t) q] = (float) acc;
        }
        out.push_back (f);
    }
    // per-coefficient normalisation (removes mic / level differences)
    for (int q = 0; q < 13; ++q)
    {
        double m = 0, v = 0;
        for (auto& f : out) m += f[(size_t) q];
        m /= juce::jmax ((size_t) 1, out.size());
        for (auto& f : out) v += (f[(size_t) q] - m) * (f[(size_t) q] - m);
        const double sd = std::sqrt (v / juce::jmax ((size_t) 1, out.size())) + 1e-6;
        for (auto& f : out) f[(size_t) q] = (float) ((f[(size_t) q] - m) / sd) * (q == 0 ? 0.6f : 1.0f);
    }
    return out;
}
} // namespace

void Aligner::alignTakeToLead (Track& take, const Track& lead, float tightness, bool alignEnds)
{
    if (take.source == nullptr || lead.source == nullptr || take.notes.empty()) return;

    const double hopSec = 0.01;
    const int hopL = (int) (hopSec * lead.source->sampleRate);
    const int hopT = (int) (hopSec * take.source->sampleRate);
    auto A = features (*lead.source, hopL);
    auto B = features (*take.source, hopT);
    const int N = (int) A.size(), M = (int) B.size();
    if (N < 2 || M < 2) return;

    // lead/take offsets on the host timeline, so the band follows where they actually sit
    const double offset = (take.clipStart - lead.clipStart) / hopSec;
    const int R = juce::jmax (200, (int) (0.15 * juce::jmax (N, M)));
    const int W = 2 * R + 1;

    std::vector<juce::uint8> back ((size_t) N * W, 0);

    auto centre = [&] (int i) { return (int) std::lround (i - offset); };
    auto dist = [&] (int i, int j)
    {
        float d = 0;
        for (int q = 0; q < 13; ++q) { const float e = A[(size_t) i][(size_t) q] - B[(size_t) j][(size_t) q]; d += e * e; }
        return std::sqrt (d);
    };

    std::vector<float> prevRow ((size_t) W, 1e30f), curRow ((size_t) W, 1e30f);
    for (int i = 0; i < N; ++i)
    {
        std::fill (curRow.begin(), curRow.end(), 1e30f);
        const int c = centre (i), cp = centre (i - 1);
        for (int k = 0; k < W; ++k)
        {
            const int j = c - R + k;
            if (j < 0 || j >= M) continue;
            const float d = dist (i, j);
            float best = 1e30f; juce::uint8 dir = 0;
            if (i == 0 || j == 0) { best = d; dir = 4; }   // open start

            // from (i-1, j-1)
            if (i > 0) { const int kp = (j - 1) - (cp - R); if (kp >= 0 && kp < W && prevRow[(size_t) kp] + d * 1.0f < best) { best = prevRow[(size_t) kp] + d; dir = 1; } }
            // from (i-1, j)
            if (i > 0) { const int kp = j - (cp - R); if (kp >= 0 && kp < W && prevRow[(size_t) kp] + d * 1.4f < best) { best = prevRow[(size_t) kp] + d * 1.4f; dir = 2; } }
            // from (i, j-1)
            if (k > 0 && curRow[(size_t) k - 1] + d * 1.4f < best) { best = curRow[(size_t) k - 1] + d * 1.4f; dir = 3; }
            if (dir == 0) continue;
            if (dir == 4) dir = 0;
            curRow[(size_t) k] = best;
            back[(size_t) i * W + k] = dir;
        }
        std::swap (prevRow, curRow);
    }

    // find end: best cell in last row
    int i = N - 1;
    int bestK = -1; float bc = 1e30f;
    for (int k = 0; k < W; ++k) if (prevRow[(size_t) k] < bc) { bc = prevRow[(size_t) k]; bestK = k; }
    if (bestK < 0) return;
    int j = centre (i) - R + bestK;

    std::vector<int> leadForTake ((size_t) M, -1);
    while (i >= 0 && j >= 0)
    {
        if (leadForTake[(size_t) j] < 0) leadForTake[(size_t) j] = i;
        const int k = j - (centre (i) - R);
        if (k < 0 || k >= W) break;
        const auto dir = back[(size_t) i * W + k];
        if (dir == 1) { --i; --j; }
        else if (dir == 2) { --i; }
        else if (dir == 3) { --j; }
        else break;
    }
    // fill gaps
    int last = 0;
    for (int q = 0; q < M; ++q) { if (leadForTake[(size_t) q] < 0) leadForTake[(size_t) q] = last; else last = leadForTake[(size_t) q]; }

    auto desiredDst = [&] (double takeSrc)
    {
        const int q = juce::jlimit (0, M - 1, (int) std::lround (takeSrc / hopSec));
        const double leadSrc = leadForTake[(size_t) q] * hopSec;
        const double leadHost = lead.clipStart + srcToDst (lead.warps, leadSrc);
        return leadHost - take.clipStart;
    };

    std::vector<WarpPoint> pts;
    for (auto& n : take.notes)
    {
        const double cs = srcToDst (take.warps, n.srcStart);
        pts.push_back ({ n.srcStart, cs + tightness * (desiredDst (n.srcStart) - cs) });
        const double ce = srcToDst (take.warps, n.srcEnd);
        if (alignEnds) pts.push_back ({ n.srcEnd, ce + tightness * (desiredDst (n.srcEnd) - ce) });
        else           pts.push_back ({ n.srcEnd, pts.back().dst + (n.srcEnd - n.srcStart) * (ce - cs) / juce::jmax (1e-6, n.srcEnd - n.srcStart) });
    }
    take.warps = pts;
    tidy (take.warps);
}
} // namespace hvx
