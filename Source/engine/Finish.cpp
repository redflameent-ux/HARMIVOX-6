#include "Finish.h"
#include <algorithm>

namespace hvx
{
namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    inline float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    void highpass (double sr, double f, double q = 0.707)
    {
        const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void lowpass (double sr, double f, double q = 0.707)
    {
        const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandpass (double sr, double f, double q)
    {
        const double w = juce::MathConstants<double>::twoPi * f / sr, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double sr, double f, double q, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
        const double c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void highshelf (double sr, double f, double db)
    {
        const double A = std::pow (10.0, db / 40.0), w = juce::MathConstants<double>::twoPi * f / sr;
        const double c = std::cos (w), s = std::sin (w), al = s / 2 * std::sqrt (2.0);
        const double sq = 2 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
             (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

struct Env
{
    float a, r, v = 0;
    Env (double sr, double attMs, double relMs)
        : a ((float) std::exp (-1.0 / (sr * attMs / 1000.0))), r ((float) std::exp (-1.0 / (sr * relMs / 1000.0))) {}
    inline float operator() (float x)
    {
        x = std::abs (x);
        v = x > v ? a * v + (1 - a) * x : r * v + (1 - r) * x;
        return v;
    }
};
} // namespace

const juce::StringArray& Finish::chainNames()
{
    static juce::StringArray s { "Off", "Natural", "Pop", "Rap", "Airy" };
    return s;
}

FinishSettings Finish::preset (int chain)
{
    FinishSettings f;
    f.chain = chain;
    switch (chain)
    {
        case 0: f.enabled = false; break;
        case 1: f.deplosive = 0.5f; f.mudCutDb = -2.0f; f.deEsser = 0.5f; f.presenceDb = 1.0f; f.airDb = 2.0f; f.compressDb = 4.0f; f.ratio = 3.0f; break;
        case 2: f.deplosive = 0.6f; f.mudCutDb = -2.5f; f.deEsser = 0.7f; f.presenceDb = 2.0f; f.airDb = 3.0f; f.compressDb = 6.0f; f.ratio = 4.2f; break;
        case 3: f.deplosive = 0.7f; f.mudCutDb = -3.0f; f.deEsser = 0.6f; f.presenceDb = 3.0f; f.airDb = 1.0f; f.compressDb = 9.0f; f.ratio = 5.0f; break;
        case 4: f.deplosive = 0.5f; f.mudCutDb = -2.0f; f.deEsser = 0.8f; f.presenceDb = 1.5f; f.airDb = 6.0f; f.compressDb = 5.0f; f.ratio = 3.5f; break;
        default: break;
    }
    return f;
}

void Finish::process (juce::AudioBuffer<float>& buf, double sr, const FinishSettings& s, float belt)
{
    const int n = buf.getNumSamples();
    float* x = buf.getWritePointer (0);

    if (belt > 0.001f)
    {
        Biquad b; b.highshelf (sr, 2500.0, 4.0 * belt);
        Biquad p; p.peak (sr, 1200.0, 0.8, 2.0 * belt);
        for (int i = 0; i < n; ++i) x[i] = p.process (b.process (x[i]));
    }
    if (! s.enabled) return;

    Biquad hp;  hp.highpass (sr, s.lowCutHz > 0 ? s.lowCutHz : 90.0, 0.75);
    Biquad hp2; hp2.highpass (sr, s.lowCutHz > 0 ? s.lowCutHz : 90.0, 0.75);
    Biquad lowBand; lowBand.lowpass (sr, 160.0);
    Biquad mud; mud.peak (sr, 350.0, 1.0, s.mudCutDb);
    Biquad ess; ess.highpass (sr, 5200.0, 0.7);
    Biquad pres; pres.peak (sr, 3500.0, 0.9, s.presenceDb);
    Biquad air; air.highshelf (sr, 10000.0, s.airDb);

    Env envLow (sr, 1.0, 40.0), envFull (sr, 1.0, 40.0), envS (sr, 0.5, 30.0), envFull2 (sr, 0.5, 30.0);

    for (int i = 0; i < n; ++i)
    {
        float v = hp2.process (hp.process (x[i]));

        // de-plosive: low-frequency bursts that dominate the signal get ducked
        if (s.deplosive > 0.001f)
        {
            const float lb = lowBand.process (v);
            const float el = envLow (lb), ef = envFull (v) + 1e-6f;
            const float ratio = el / ef;
            const float thr = 0.75f - 0.35f * s.deplosive;
            if (ratio > thr)
            {
                const float g = juce::jmax (0.1f, std::pow (thr / ratio, 1.0f + 2.0f * s.deplosive));
                v -= lb * (1.0f - g);
            }
        }

        v = mud.process (v);

        if (s.deEsser > 0.001f)
        {
            const float hs = ess.process (v);
            const float es = envS (hs), ef = envFull2 (v) + 1e-6f;
            const float thr = 0.5f - 0.3f * s.deEsser;
            if (es / ef > thr)
            {
                const float g = juce::jmax (0.15f, std::pow (thr * ef / es, 0.6f + s.deEsser));
                v -= hs * (1.0f - g);
            }
        }

        v = air.process (pres.process (v));
        x[i] = v;
    }

    // compressor with automatic threshold
    if (s.compressDb > 0.05f && s.ratio > 1.01f)
    {
        Env det (sr, 3.0, 90.0);
        std::vector<float> envDb ((size_t) n);
        std::vector<float> active;
        for (int i = 0; i < n; ++i)
        {
            const float e = juce::Decibels::gainToDecibels (det (x[i]), -120.0f);
            envDb[(size_t) i] = e;
            if (e > -50.0f && (i % 64) == 0) active.push_back (e);
        }
        if (! active.empty())
        {
            std::sort (active.begin(), active.end());
            const float P = active[(size_t) (active.size() * 0.95)];
            const float T = P - s.compressDb * s.ratio / (s.ratio - 1.0f);
            const float knee = 6.0f;
            const float makeup = s.compressDb * 0.55f;
            float smoothGr = 0.0f;
            const float smooth = (float) std::exp (-1.0 / (sr * 0.004));
            for (int i = 0; i < n; ++i)
            {
                const float over = envDb[(size_t) i] - T;
                float gr = 0;
                if (over > knee * 0.5f) gr = over * (1.0f - 1.0f / s.ratio);
                else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; gr = (1.0f - 1.0f / s.ratio) * k * k / (2.0f * knee); }
                smoothGr = smooth * smoothGr + (1.0f - smooth) * gr;
                x[i] *= juce::Decibels::decibelsToGain (makeup - smoothGr);
            }
        }
    }
}

juce::AudioBuffer<float> Finish::toStereo (const juce::AudioBuffer<float>& mono, double, float amount, juce::uint32 seed)
{
    const int n = mono.getNumSamples();
    juce::AudioBuffer<float> st (2, n);
    const float* x = mono.getReadPointer (0);
    float* L = st.getWritePointer (0);
    float* R = st.getWritePointer (1);
    if (amount <= 0.001f)
    {
        st.copyFrom (0, 0, mono, 0, 0, n);
        st.copyFrom (1, 0, mono, 0, 0, n);
        return st;
    }
    // chains of first-order allpasses with different corners per side
    juce::Random rng ((juce::int64) seed * 7919 + 13);
    float aL[4], aR[4], zL[4] = {}, zR[4] = {};
    for (int k = 0; k < 4; ++k) { aL[k] = 0.25f + 0.6f * rng.nextFloat(); aR[k] = -(0.25f + 0.6f * rng.nextFloat()); }
    const float k = juce::jlimit (0.0f, 1.0f, amount) * 1.6f;
    const float norm = 1.0f / (1.0f + 0.35f * k);
    for (int i = 0; i < n; ++i)
    {
        float l = x[i], r = x[i];
        for (int q = 0; q < 4; ++q)
        {
            const float yl = -aL[q] * l + zL[q]; zL[q] = l + aL[q] * yl; l = yl;
            const float yr = -aR[q] * r + zR[q]; zR[q] = r + aR[q] * yr; r = yr;
        }
        L[i] = (x[i] + k * (l - x[i]) * 0.5f) * norm;
        R[i] = (x[i] + k * (r - x[i]) * 0.5f) * norm;
    }
    return st;
}
} // namespace hvx
