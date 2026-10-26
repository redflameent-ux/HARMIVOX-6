#include "Analysis.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <algorithm>
#include <numeric>

namespace hvx
{
namespace
{
constexpr double kMinHz = 55.0;
constexpr double kMaxHz = 1100.0;

inline float hzToMidi (double hz) { return (float) (69.0 + 12.0 * std::log2 (hz / 440.0)); }

/** FFT-based YIN. Reusable per thread. */
struct Yin
{
    explicit Yin (double sr)
        : sampleRate (sr)
    {
        tauMax = (int) std::ceil (sr / kMinHz);
        tauMin = juce::jmax (2, (int) std::floor (sr / kMaxHz));
        W = juce::nextPowerOfTwo (tauMax + 1);
        const int N = juce::nextPowerOfTwo (2 * W + tauMax);
        order = (int) std::log2 ((double) N);
        fft = std::make_unique<juce::dsp::FFT> (order);
        n = N;
        A.resize ((size_t) n); B.resize ((size_t) n); R.resize ((size_t) n);
        tmp.resize ((size_t) n);
        d.resize ((size_t) tauMax + 2); dn.resize ((size_t) tauMax + 2);
        prefix.resize ((size_t) (W + tauMax + 2));
    }

    /** Analyses the segment starting at seg (length W + tauMax). */
    bool run (const float* seg, float& midiOut, float& clarityOut)
    {
        const int L = W + tauMax;
        std::fill (A.begin(), A.end(), std::complex<float>());
        std::fill (B.begin(), B.end(), std::complex<float>());
        for (int i = 0; i < W; ++i) A[(size_t) i] = seg[i];
        for (int i = 0; i < L; ++i) B[(size_t) i] = seg[i];

        fft->perform (A.data(), tmp.data(), false); A.swap (tmp);
        fft->perform (B.data(), tmp.data(), false); B.swap (tmp);
        for (int i = 0; i < n; ++i) R[(size_t) i] = std::conj (A[(size_t) i]) * B[(size_t) i];
        fft->perform (R.data(), tmp.data(), true);    // juce inverse is unscaled? (it is scaled by 1/n)

        prefix[0] = 0.0;
        for (int i = 0; i < L; ++i) prefix[(size_t) i + 1] = prefix[(size_t) i] + (double) seg[i] * seg[i];

        const double Ea = prefix[(size_t) W];
        if (Ea < 1e-9) { clarityOut = 0; return false; }

        d[0] = 0.0;
        double running = 0.0;
        dn[0] = 1.0;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            const double Eb = prefix[(size_t) (tau + W)] - prefix[(size_t) tau];
            const double r  = tmp[(size_t) tau].real();
            double v = Ea + Eb - 2.0 * r;
            if (v < 0) v = 0;
            d[(size_t) tau] = v;
            running += v;
            dn[(size_t) tau] = running > 0 ? v * tau / running : 1.0;
        }

        int best = -1;
        const double thresh = 0.15;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (dn[(size_t) tau] < thresh)
            {
                while (tau + 1 < tauMax && dn[(size_t) tau + 1] < dn[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        }
        if (best < 0)
        {
            double mn = 1e9;
            for (int tau = tauMin; tau < tauMax; ++tau)
                if (dn[(size_t) tau] < mn) { mn = dn[(size_t) tau]; best = tau; }
        }
        if (best <= 0) { clarityOut = 0; return false; }

        double t = best;
        if (best > 1 && best < tauMax)
        {
            const double a = dn[(size_t) best - 1], b = dn[(size_t) best], c = dn[(size_t) best + 1];
            const double den = a - 2 * b + c;
            if (std::abs (den) > 1e-12) t = best + 0.5 * (a - c) / den;
        }
        clarityOut = (float) juce::jlimit (0.0, 1.0, 1.0 - dn[(size_t) best]);
        midiOut = hzToMidi (sampleRate / t);

        // is the octave below also a convincing period? (strong 2nd harmonic case)
        lowerOctaveOk = false;
        const int t2 = (int) std::lround (2.0 * t);
        if (t2 + 3 < tauMax)
        {
            double mn = 1e9;
            for (int k = t2 - 3; k <= t2 + 3; ++k) mn = juce::jmin (mn, dn[(size_t) k]);
            // a true period also repeats at 2T with about the same dip; a wrong (half) period
            // has a much deeper dip at 2T
            lowerOctaveOk = dn[(size_t) best] > 0.02 && mn < 0.55 * dn[(size_t) best];
            if (lowerOctaveOk && 2.0 * t <= tauMax)
            {
                midiOut -= 12.0f;
                clarityOut = (float) juce::jlimit (0.0, 1.0, 1.0 - mn);
            }
        }
        return true;
    }

    double sampleRate;
    bool lowerOctaveOk = false;
    int tauMin = 2, tauMax = 800, W = 1024, n = 4096, order = 12;
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<std::complex<float>> A, B, R, tmp;
    std::vector<double> d, dn, prefix;
};

void medianFilterVoiced (std::vector<PitchFrame>& fr, int radius)
{
    std::vector<float> out (fr.size());
    std::vector<float> win;
    for (size_t i = 0; i < fr.size(); ++i)
    {
        out[i] = fr[i].midi;
        if (! fr[i].voiced) continue;
        win.clear();
        for (int k = -radius; k <= radius; ++k)
        {
            const long j = (long) i + k;
            if (j < 0 || j >= (long) fr.size() || ! fr[(size_t) j].voiced) continue;
            win.push_back (fr[(size_t) j].midi);
        }
        out[i] = Analysis::medianOf (win);
    }
    for (size_t i = 0; i < fr.size(); ++i) fr[i].midi = out[i];
}
} // namespace

float Analysis::medianOf (std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    const size_t m = v.size() / 2;
    std::nth_element (v.begin(), v.begin() + (long) m, v.end());
    return v[m];
}

//==============================================================================
bool Analysis::quickPitch (const float* x, int num, double sr, float& midiOut, float& clarityOut)
{
    thread_local std::unique_ptr<Yin> yin;
    if (yin == nullptr || std::abs (yin->sampleRate - sr) > 1.0) yin = std::make_unique<Yin> (sr);
    const int L = yin->W + yin->tauMax;
    if (num < L) return false;
    return yin->run (x + (num - L), midiOut, clarityOut) && clarityOut > 0.7f;
}

//==============================================================================
std::shared_ptr<SourceAudio> Analysis::analyse (juce::AudioBuffer<float> mono, double sr,
                                                const juce::String& name, const std::atomic<bool>* cancel)
{
    auto src = std::make_shared<SourceAudio>();
    src->sampleRate = sr;
    src->name = name;
    src->hop = sr > 60000.0 ? 512 : 256;
    src->audio = std::move (mono);

    const int N = src->numSamples();
    const int hop = src->hop;
    const float* x = src->audio.getReadPointer (0);

    Yin yin (sr);
    const int L = yin.W + yin.tauMax;
    std::vector<float> seg ((size_t) L);

    const int numFrames = N / hop + 1;
    src->frames.resize ((size_t) numFrames);

    // Loudness reference
    float peakRms = -120.0f;
    std::vector<float> rmsDb ((size_t) numFrames);
    for (int f = 0; f < numFrames; ++f)
    {
        const int c = f * hop;
        double e = 0; int cnt = 0;
        for (int i = c - yin.W / 2; i < c + yin.W / 2; ++i)
            if (i >= 0 && i < N) { e += (double) x[i] * x[i]; ++cnt; }
        const float db = cnt > 0 ? (float) (10.0 * std::log10 (e / cnt + 1e-12)) : -120.0f;
        rmsDb[(size_t) f] = db;
        peakRms = juce::jmax (peakRms, db);
    }
    const float floorDb = juce::jmax (-55.0f, peakRms - 42.0f);

    for (int f = 0; f < numFrames; ++f)
    {
        if (cancel != nullptr && cancel->load()) return nullptr;
        auto& pf = src->frames[(size_t) f];
        pf.rmsDb = rmsDb[(size_t) f];
        if (pf.rmsDb < floorDb) continue;

        const int start = f * hop - yin.W / 2;
        for (int i = 0; i < L; ++i)
        {
            const int j = start + i;
            seg[(size_t) i] = (j >= 0 && j < N) ? x[j] : 0.0f;
        }
        float m = 0, c = 0;
        if (yin.run (seg.data(), m, c))
        {
            pf.midi = m;
            pf.clarity = c;
            pf.voiced = c > 0.78f && m > 30.0f && m < 90.0f;
        }
    }

    auto& fr = src->frames;

    // Octave continuity: inside each voiced run, take the octave-lower reading
    // when it is valid and keeps the line smooth (fixes strong-2nd-harmonic jumps)
    {
        size_t i = 0;
        while (i < fr.size())
        {
            if (! fr[i].voiced) { ++i; continue; }
            size_t j = i;
            while (j < fr.size() && fr[j].voiced) ++j;
            float prev = -1.0f;
            for (size_t k = i; k < j; ++k)
            {
                float m = fr[k].midi;
                if (prev > 0 && m - prev > 6.0f && std::abs (m - 12.0f - prev) < std::abs (m - prev)) m -= 12.0f;
                else if (prev > 0 && prev - m > 6.0f && std::abs (m + 12.0f - prev) < std::abs (m - prev)) m += 12.0f;
                fr[k].midi = m;
                prev = m;
            }
            i = j;
        }
    }

    // Octave-jump repair against a wide median
    {
        std::vector<float> win;
        for (size_t i = 0; i < fr.size(); ++i)
        {
            if (! fr[i].voiced) continue;
            win.clear();
            for (int k = -12; k <= 12; ++k)
            {
                const long j = (long) i + k;
                if (j >= 0 && j < (long) fr.size() && fr[(size_t) j].voiced) win.push_back (fr[(size_t) j].midi);
            }
            const float med = medianOf (win);
            const float diff = fr[i].midi - med;
            if (std::abs (diff - 12.0f) < 1.5f) fr[i].midi -= 12.0f;
            else if (std::abs (diff + 12.0f) < 1.5f) fr[i].midi += 12.0f;
        }
    }
    medianFilterVoiced (fr, 2);

    // Remove tiny voiced islands, bridge tiny gaps
    {
        size_t i = 0;
        while (i < fr.size())
        {
            if (! fr[i].voiced) { ++i; continue; }
            size_t j = i;
            while (j < fr.size() && fr[j].voiced) ++j;
            if (j - i < 4) for (size_t k = i; k < j; ++k) fr[k].voiced = false;
            i = j;
        }
        for (size_t k = 1; k + 2 < fr.size(); ++k)
        {
            if (! fr[k].voiced && fr[k - 1].voiced)
            {
                size_t e = k;
                while (e < fr.size() && ! fr[e].voiced && e - k < 3) ++e;
                if (e < fr.size() && fr[e].voiced && std::abs (fr[e].midi - fr[k - 1].midi) < 1.0f)
                    for (size_t q = k; q < e; ++q)
                    {
                        const float t = (float) (q - k + 1) / (float) (e - k + 1);
                        fr[q].voiced = true;
                        fr[q].midi = fr[k - 1].midi + (fr[e].midi - fr[k - 1].midi) * t;
                        fr[q].clarity = 0.8f;
                    }
            }
        }
    }

    // Pitch marks
    auto& marks = src->marks;
    const int unvoicedStep = (int) (0.005 * sr);
    auto periodAt = [&] (int pos) -> float
    {
        float m = 0;
        if (src->pitchAt (pos / sr, m) && m > 0)
            return (float) (sr / (440.0 * std::pow (2.0, (m - 69.0) / 12.0)));
        return 0.0f;
    };

    int pos = 0;
    while (pos < N)
    {
        if (cancel != nullptr && cancel->load()) return nullptr;
        const float P = periodAt (pos);
        if (P <= 0.0f)
        {
            marks.push_back ({ pos, (float) unvoicedStep, false });
            pos += unvoicedStep;
            continue;
        }
        // start of a voiced run: lock onto the strongest peak in one period
        int best = pos; float bv = -1.0f; float polarity = 1.0f;
        for (int i = pos; i < juce::jmin (N, pos + (int) P); ++i)
            if (std::abs (x[i]) > bv) { bv = std::abs (x[i]); best = i; polarity = x[i] >= 0 ? 1.0f : -1.0f; }
        int m = best;
        while (m < N)
        {
            const float Pm = periodAt (m);
            if (Pm <= 0.0f) break;
            marks.push_back ({ m, Pm, true });
            const int expected = m + (int) std::lround (Pm);
            const int r = juce::jmax (1, (int) (Pm * 0.2f));
            int nb = expected; float nv = -1e9f;
            for (int i = expected - r; i <= expected + r; ++i)
            {
                if (i <= m || i >= N) continue;
                const float v = x[i] * polarity;
                if (v > nv) { nv = v; nb = i; }
            }
            if (nb <= m) nb = expected;
            m = nb;
        }
        pos = juce::jmax (m, pos + 1);
    }
    return src;
}

//==============================================================================
std::vector<Note> Analysis::detectNotes (const SourceAudio& src, int& nextNoteId)
{
    std::vector<Note> notes;
    const auto& fr = src.frames;
    const int n = (int) fr.size();
    const double frameSec = src.hop / src.sampleRate;
    const int minFrames  = juce::jmax (3, (int) std::ceil (0.06 / frameSec));
    const int holdFrames = juce::jmax (3, (int) std::ceil (0.035 / frameSec));

    struct Seg { int a, b; };
    std::vector<Seg> segs;

    int i = 0;
    while (i < n)
    {
        if (! fr[(size_t) i].voiced) { ++i; continue; }
        int j = i;
        while (j < n && fr[(size_t) j].voiced) ++j;

        // split this voiced run where the pitch settles somewhere new or a new onset hits
        int noteStart = i;
        std::vector<float> cur;
        for (int k = i; k < j; ++k)
        {
            const float p = fr[(size_t) k].midi;
            if ((int) cur.size() >= holdFrames)
            {
                const float ref = medianOf (std::vector<float> (cur.begin() + (long) juce::jmin ((int) cur.size() / 4, 3), cur.end()));
                bool jump = std::abs (p - ref) > 0.75f;
                if (jump)
                {
                    // must be stable for holdFrames
                    int stable = 0;
                    for (int q = k; q < juce::jmin (j, k + holdFrames); ++q)
                        if (std::abs (fr[(size_t) q].midi - p) < 0.55f) ++stable;
                    jump = stable >= holdFrames - 1;
                }
                bool onset = false;
                if (k >= 3)
                {
                    const float dip = juce::jmin (fr[(size_t) k - 1].rmsDb, fr[(size_t) k - 2].rmsDb, fr[(size_t) k - 3].rmsDb);
                    onset = fr[(size_t) k].rmsDb - dip > 7.0f;
                }
                if (jump || onset)
                {
                    // place the boundary at the steepest pitch move before k
                    int bnd = k;
                    float steep = 0;
                    for (int q = juce::jmax (noteStart + 1, k - holdFrames); q <= k; ++q)
                    {
                        const float s = std::abs (fr[(size_t) q].midi - fr[(size_t) q - 1].midi);
                        if (s > steep) { steep = s; bnd = q; }
                    }
                    segs.push_back ({ noteStart, bnd });
                    noteStart = bnd;
                    cur.clear();
                    for (int q = bnd; q <= k; ++q) cur.push_back (fr[(size_t) q].midi);
                    continue;
                }
            }
            cur.push_back (p);
        }
        segs.push_back ({ noteStart, j });
        i = j;
    }

    // merge short segments into a neighbour
    for (size_t s = 0; s < segs.size();)
    {
        if (segs[s].b - segs[s].a >= minFrames || segs.size() == 1) { ++s; continue; }
        const bool prevTouch = s > 0 && segs[s - 1].b == segs[s].a;
        const bool nextTouch = s + 1 < segs.size() && segs[s + 1].a == segs[s].b;
        if (prevTouch)      { segs[s - 1].b = segs[s].b; segs.erase (segs.begin() + (long) s); }
        else if (nextTouch) { segs[s + 1].a = segs[s].a; segs.erase (segs.begin() + (long) s); }
        else if (segs[s].b - segs[s].a < minFrames / 2) segs.erase (segs.begin() + (long) s);
        else ++s;
    }

    for (const auto& sg : segs)
    {
        std::vector<float> ps;
        const int len = sg.b - sg.a;
        const int trim = len > 8 ? len / 7 : 0;
        for (int k = sg.a + trim; k < sg.b - trim; ++k) ps.push_back (fr[(size_t) k].midi);
        if (ps.empty()) for (int k = sg.a; k < sg.b; ++k) ps.push_back (fr[(size_t) k].midi);

        Note nt;
        nt.id = nextNoteId++;
        nt.srcStart = sg.a * frameSec;
        nt.srcEnd   = sg.b * frameSec;
        nt.detected = medianOf (ps);
        nt.target   = nt.detected;
        notes.push_back (nt);
    }
    return notes;
}

//==============================================================================
void Analysis::detectKey (const std::vector<Note>& notes, int& key, int& scale)
{
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    double hist[12] = {};
    for (auto& nt : notes)
    {
        const int pc = (((int) std::lround (nt.detected)) % 12 + 12) % 12;
        hist[pc] += juce::jmax (0.05, nt.srcEnd - nt.srcStart);
    }

    auto corr = [&] (const double* prof, int rot)
    {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += hist[i]; my += prof[i]; }
        mx /= 12; my /= 12;
        double num = 0, dx = 0, dy = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = hist[(i + rot) % 12] - mx, b = prof[i] - my;
            num += a * b; dx += a * a; dy += b * b;
        }
        return (dx > 0 && dy > 0) ? num / std::sqrt (dx * dy) : 0.0;
    };

    double best = -2; key = 0; scale = 0;
    for (int r = 0; r < 12; ++r)
    {
        const double a = corr (maj, r), b = corr (mnr, r);
        if (a > best) { best = a; key = r; scale = 0; }
        if (b > best) { best = b; key = r; scale = 1; }
    }
}

} // namespace hvx
