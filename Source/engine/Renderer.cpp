#include "Renderer.h"
#include <algorithm>

namespace hvx
{
//==============================================================================
float hashNoise (juce::uint32 seed, int a, int b)
{
    juce::uint32 h = seed * 0x9E3779B1u ^ ((juce::uint32) a * 0x85EBCA77u) ^ ((juce::uint32) b * 0xC2B2AE3Du);
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return (float) ((h & 0xFFFFFF) / (double) 0x7FFFFF - 1.0);
}

static float gaussish (juce::uint32 seed, int a, int b)
{
    return (hashNoise (seed, a, b * 3) + hashNoise (seed, a, b * 3 + 1) + hashNoise (seed, a, b * 3 + 2)) / 1.7f;
}

//==============================================================================
std::vector<WarpPoint> effectiveWarps (const Track& t)
{
    std::vector<WarpPoint> w = t.warps;
    if (! t.isVoice() || t.notes.empty()) return w;

    const float spread = t.human.looseMs * t.human.timingScale * (0.6f + 0.8f * t.human.variation) / 1000.0f;
    if (spread <= 0.0001f) return w;

    std::vector<WarpPoint> out;
    for (const auto& n : t.notes)
    {
        const double ds = srcToDst (t.warps, n.srcStart);
        const double de = srcToDst (t.warps, n.srcEnd);
        const double jStart = gaussish (t.human.seed, n.id, 1) * spread;
        const double jEnd   = gaussish (t.human.seed, n.id, 2) * spread * 0.8;
        out.push_back ({ n.srcStart, ds + jStart });
        out.push_back ({ n.srcEnd,   de + jEnd });
    }
    std::sort (out.begin(), out.end(), [] (auto& a, auto& b) { return a.src < b.src; });
    // enforce monotonic output (keep at least 10 ms per segment)
    for (size_t i = 1; i < out.size(); ++i)
    {
        if (out[i].src - out[i - 1].src < 1e-6) { out.erase (out.begin() + (long) i); --i; continue; }
        const double minDst = out[i - 1].dst + juce::jmax (0.004, (out[i].src - out[i - 1].src) * 0.4);
        if (out[i].dst < minDst) out[i].dst = minDst;
    }
    return out;
}

//==============================================================================
static float localMean (const SourceAudio& src, const Note& n, double srcSec, double half)
{
    const double a = juce::jmax (n.srcStart, srcSec - half), b = juce::jmin (n.srcEnd, srcSec + half);
    const int fa = (int) std::floor (src.secondsToFrame (a)), fb = (int) std::ceil (src.secondsToFrame (b));
    double sum = 0; int cnt = 0;
    for (int f = juce::jmax (0, fa); f <= juce::jmin ((int) src.frames.size() - 1, fb); ++f)
        if (src.frames[(size_t) f].voiced) { sum += src.frames[(size_t) f].midi; ++cnt; }
    return cnt > 0 ? (float) (sum / cnt) : n.detected;
}

float correctedPitchAt (const Track& t, const SourceAudio& src, const Note& n, double srcSec, float f0)
{
    const float strength = t.tune.strength;
    const float driftFix = n.driftFix >= 0 ? n.driftFix : t.tune.driftFix;
    float vibAmt   = n.vibrato  >= 0 ? n.vibrato  : t.tune.vibrato;

    if (t.isVoice())
        vibAmt *= 1.0f + 0.35f * hashNoise (t.human.seed, n.id, 7) * (0.3f + t.human.variation);

    const float m = localMean (src, n, srcSec, 0.15);
    const float drift = m - n.detected;
    const float vib = f0 - m;

    const float corrected = n.target + drift * (1.0f - driftFix) + vib * vibAmt;
    const float natural   = f0 + (n.target - n.detected);
    float out = natural + strength * (corrected - natural);

    if (std::abs (n.addVibratoCents) > 0.01f)
    {
        const double age = srcSec - n.srcStart;
        const float fade = (float) juce::jlimit (0.0, 1.0, (age - 0.08) / 0.25);
        out += n.addVibratoCents / 100.0f * fade * (float) std::sin (juce::MathConstants<double>::twoPi * n.vibratoRateHz * age);
    }

    if (t.isVoice())
    {
        const float var = t.human.variation;
        const float noteCents = gaussish (t.human.seed, n.id, 5) * (4.0f + 16.0f * var);
        const float lfoRate = 0.25f + 0.2f * (hashNoise (t.human.seed, 11) + 1.0f);
        const float lfo = (3.0f + 9.0f * var) * (float) std::sin (juce::MathConstants<double>::twoPi * lfoRate * srcSec
                                                                    + 6.0 * hashNoise (t.human.seed, 12));
        out += (noteCents + lfo) / 100.0f;
    }
    return out;
}

//==============================================================================
namespace
{
struct NoteIndex
{
    explicit NoteIndex (const std::vector<Note>& ns) : notes (ns) {}
    const std::vector<Note>& notes;
    // index of the last note with srcStart <= s, or -1
    int at (double s) const
    {
        int lo = 0, hi = (int) notes.size() - 1, ans = -1;
        while (lo <= hi)
        {
            const int mid = (lo + hi) / 2;
            if (notes[(size_t) mid].srcStart <= s) { ans = mid; lo = mid + 1; } else hi = mid - 1;
        }
        return ans;
    }
};
} // namespace

Plan buildPlan (const Track& t, const SourceAudio& src)
{
    Plan p;
    p.dt = src.hop / src.sampleRate;
    p.warps = effectiveWarps (t);

    std::vector<Note> notes = t.notes;
    std::sort (notes.begin(), notes.end(), [] (auto& a, auto& b) { return a.srcStart < b.srcStart; });
    NoteIndex idx (notes);

    p.duration = srcToDst (p.warps, src.lengthSeconds()) + 0.05;
    const int nf = juce::jmax (1, (int) std::ceil (p.duration / p.dt) + 1);
    p.srcTime.resize ((size_t) nf); p.inMidi.resize ((size_t) nf); p.outMidi.resize ((size_t) nf);
    p.formantSt.resize ((size_t) nf); p.gain.resize ((size_t) nf);

    float voiceFormant = 0.0f, voiceGain = 1.0f;
    if (t.isVoice())
    {
        const float sign = hashNoise (t.human.seed, 21) >= 0 ? 1.0f : -1.0f;
        voiceFormant = sign * (0.12f + 2.2f * t.human.otherSinger) * (0.5f + 0.5f * std::abs (hashNoise (t.human.seed, 22)))
                     + 0.45f * t.human.belt;
        voiceGain = juce::Decibels::decibelsToGain (1.5f * t.human.belt);
    }

    // per-note natural offset used in gaps
    auto plainOffset = [] (const Note& n) { return n.target - n.detected; };

    auto offsetAt = [&] (double s, float f0) -> float
    {
        const int i = idx.at (s);
        if (i >= 0 && s <= notes[(size_t) i].srcEnd)
            return correctedPitchAt (t, src, notes[(size_t) i], s, f0) - f0;
        // in a gap: interpolate between neighbours
        const Note* prev = i >= 0 ? &notes[(size_t) i] : nullptr;
        const Note* next = (i + 1) < (int) notes.size() ? &notes[(size_t) i + 1] : nullptr;
        if (prev && next)
        {
            const double span = next->srcStart - prev->srcEnd;
            const float tt = span > 1e-6 ? (float) ((s - prev->srcEnd) / span) : 0.5f;
            return plainOffset (*prev) + (plainOffset (*next) - plainOffset (*prev)) * tt;
        }
        if (prev) return plainOffset (*prev);
        if (next) return plainOffset (*next);
        return 0.0f;
    };

    // join windows (retune speed / per-join transition)
    struct Join { double a, b; };
    std::vector<Join> joins;
    for (size_t i = 1; i < notes.size(); ++i)
    {
        const auto& A = notes[i - 1];
        const auto& B = notes[i];
        if (B.srcStart - A.srcEnd > 0.12) continue;   // separated notes need no glide
        double w;
        if (B.transition >= 0) w = 0.008 + B.transition * 0.22;
        else                   w = juce::jmax (0.006, t.tune.retuneMs / 1000.0 * 1.6);
        const double mid = 0.5 * (A.srcEnd + B.srcStart);
        w = juce::jmin (w, 0.9 * (A.srcEnd - A.srcStart), 0.9 * (B.srcEnd - B.srcStart)) ;
        w = juce::jmax (w, 0.004);
        joins.push_back ({ mid - w * 0.5, mid + w * 0.5 });
    }
    size_t joinCursor = 0;

    for (int f = 0; f < nf; ++f)
    {
        const double outT = f * p.dt;
        const double s = dstToSrc (p.warps, outT);
        p.srcTime[(size_t) f] = (float) s;

        float f0 = 0;
        const bool voiced = s >= 0 && s < src.lengthSeconds() && src.pitchAt (s, f0);
        p.inMidi[(size_t) f] = voiced ? f0 : 0.0f;

        const int ni = idx.at (s);
        const Note* cur = (ni >= 0 && s <= notes[(size_t) ni].srcEnd + 0.03) ? &notes[(size_t) ni] : nullptr;

        float gainDb = 0, form = voiceFormant;
        if (cur != nullptr) { gainDb = cur->volumeDb; form += cur->formantSt; }
        float g = juce::Decibels::decibelsToGain (gainDb) * voiceGain;
        if (cur != nullptr && cur->muted) g = 0.0f;
        p.gain[(size_t) f] = g;
        p.formantSt[(size_t) f] = form;

        if (! voiced) { p.outMidi[(size_t) f] = 0.0f; continue; }

        while (joinCursor < joins.size() && joins[joinCursor].b < s) ++joinCursor;
        float off;
        if (joinCursor < joins.size() && s >= joins[joinCursor].a && s <= joins[joinCursor].b)
        {
            const auto& j = joins[joinCursor];
            float fa = f0, fb = f0;
            src.pitchAt (j.a, fa); src.pitchAt (j.b, fb);
            const float oa = offsetAt (j.a, fa), ob = offsetAt (j.b, fb);
            float x = (float) ((s - j.a) / juce::jmax (1e-6, j.b - j.a));
            x = x * x * (3.0f - 2.0f * x);
            off = oa + (ob - oa) * x;
        }
        else off = offsetAt (s, f0);

        p.outMidi[(size_t) f] = f0 + off;
    }

    // smooth gain to avoid clicks on mutes / level jumps (about 6 ms)
    for (int pass = 0; pass < 2; ++pass)
        for (int f = 1; f < nf; ++f)
            p.gain[(size_t) f] = p.gain[(size_t) f - 1] + 0.5f * (p.gain[(size_t) f] - p.gain[(size_t) f - 1]);

    return p;
}

//==============================================================================
static inline float hermite (const float* x, int n, double pos)
{
    const int i = (int) std::floor (pos);
    const float t = (float) (pos - i);
    auto at = [&] (int k) { return (k >= 0 && k < n) ? x[k] : 0.0f; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c0 = x0, c1 = 0.5f * (x1 - xm1);
    const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
    const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

juce::AudioBuffer<float> renderPsola (const SourceAudio& src, const juce::AudioBuffer<float>& audio, const Plan& plan)
{
    const double sr = src.sampleRate;
    const int inN = audio.getNumSamples();
    const float* x = audio.getReadPointer (0);
    const int outN = (int) std::ceil (plan.duration * sr) + 4096;

    juce::AudioBuffer<float> out (1, outN);
    out.clear();
    std::vector<float> norm ((size_t) outN, 0.0f);
    float* y = out.getWritePointer (0);

    const auto& marks = src.marks;
    auto nearestMark = [&] (double pos) -> int
    {
        if (marks.empty()) return -1;
        auto it = std::lower_bound (marks.begin(), marks.end(), pos, [] (const PitchMark& m, double v) { return m.pos < v; });
        int i = (int) (it - marks.begin());
        if (i >= (int) marks.size()) i = (int) marks.size() - 1;
        if (i > 0 && std::abs (marks[(size_t) i - 1].pos - pos) < std::abs (marks[(size_t) i].pos - pos)) --i;
        return i;
    };

    auto planAt = [&] (const std::vector<float>& v, double fpos)
    {
        const int i = juce::jlimit (0, plan.size() - 1, (int) fpos);
        const int j = juce::jmin (plan.size() - 1, i + 1);
        const float t = (float) juce::jlimit (0.0, 1.0, fpos - i);
        return v[(size_t) i] + (v[(size_t) j] - v[(size_t) i]) * t;
    };

    const double unvStep = 0.005 * sr;
    double ts = 0.0;
    std::vector<float> win;

    while (ts < outN - 8)
    {
        const double fpos = (ts / sr) / plan.dt;
        if (fpos >= plan.size() - 1) break;

        const int fi = (int) fpos;
        const double srcPos = planAt (plan.srcTime, fpos) * sr;
        const float gain = planAt (plan.gain, fpos);
        const float formant = planAt (plan.formantSt, fpos);
        const float outMidi = plan.outMidi[(size_t) fi] > 0 && plan.outMidi[(size_t) juce::jmin (fi + 1, plan.size() - 1)] > 0
                                ? planAt (plan.outMidi, fpos) : plan.outMidi[(size_t) fi];

        double center, Pa, Ps;
        const int mi = nearestMark (srcPos);
        if (outMidi > 0 && mi >= 0 && marks[(size_t) mi].voiced)
        {
            center = marks[(size_t) mi].pos;
            Pa = marks[(size_t) mi].period;
            Ps = sr / midiToHz (outMidi);
            Ps = juce::jlimit (Pa * 0.25, Pa * 4.0, Ps);
        }
        else
        {
            center = srcPos;
            Pa = unvStep;
            Ps = unvStep;
        }

        const double phi = std::pow (2.0, formant / 12.0);
        const int half = juce::jmax (4, (int) std::lround (Pa / phi));
        const int t0 = (int) std::lround (ts);

        if (gain > 1e-5f && center > -Pa && center < inN + Pa)
        {
            for (int k = -half; k <= half; ++k)
            {
                const int o = t0 + k;
                if (o < 0 || o >= outN) continue;
                const float w = 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) k / (float) half);
                const float s = hermite (x, inN, center + k * phi);
                y[o] += w * s * gain;
                norm[(size_t) o] += w;
            }
        }
        else
        {
            for (int k = -half; k <= half; ++k)
            {
                const int o = t0 + k;
                if (o < 0 || o >= outN) continue;
                norm[(size_t) o] += 0.5f + 0.5f * std::cos (juce::MathConstants<float>::pi * (float) k / (float) half);
            }
        }
        ts += Ps;
    }

    for (int i = 0; i < outN; ++i)
        y[i] /= juce::jmax (1.0f, norm[(size_t) i]);

    return out;
}

} // namespace hvx
