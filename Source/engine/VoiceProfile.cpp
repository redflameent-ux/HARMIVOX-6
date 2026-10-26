#include "VoiceProfile.h"
#include "Analysis.h"
#include <juce_dsp/juce_dsp.h>
#include <complex>
#include <cstring>

namespace hvx
{
double VoiceProfile::bandCentreHz (int band)
{
    return 90.0 * std::pow (11000.0 / 90.0, (double) band / (kBands - 1));
}

static float bandPosForHz (double hz)
{
    if (hz <= 90.0) return 0.0f;
    return (float) ((VoiceProfile::kBands - 1) * std::log (hz / 90.0) / std::log (11000.0 / 90.0));
}

float VoiceProfile::envAt (float midi, int band) const
{
    const float pos = (midi - kLowMidi) / kStep;
    const int a = juce::jlimit (0, kPitchBins - 1, (int) std::floor (pos));
    const int b = juce::jlimit (0, kPitchBins - 1, a + 1);
    const float t = juce::jlimit (0.0f, 1.0f, pos - (float) a);
    return env[a][band] + (env[b][band] - env[a][band]) * t;
}

//==============================================================================
bool VoiceProfile::frameEnvelope (const float* x, int n, int centre, double sr, float period, float* out)
{
    constexpr int order = 11, N = 1 << order;
    thread_local juce::dsp::FFT fft (order);
    thread_local std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);

    double e = 0;
    for (int i = 0; i < N; ++i)
    {
        const int j = centre - N / 2 + i;
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
        const float v = (j >= 0 && j < n) ? x[j] * w : 0.0f;
        a[(size_t) i] = v; e += v * v;
    }
    if (e < 1e-7) return false;
    fft.perform (a.data(), b.data(), false);
    for (int i = 0; i < N; ++i) a[(size_t) i] = std::log (std::abs (b[(size_t) i]) + 1e-7f);
    fft.perform (a.data(), b.data(), true);               // real cepstrum
    const int L = juce::jlimit (12, 220, (int) (period * 0.65f));
    for (int i = 0; i < N; ++i)
    {
        const int q = i <= N / 2 ? i : N - i;
        if (q > L) b[(size_t) i] = 0.0f;
    }
    fft.perform (b.data(), a.data(), false);              // smoothed log spectrum (real part)

    float mean = 0;
    for (int band = 0; band < kBands; ++band)
    {
        const double lo = band == 0 ? 60.0 : std::sqrt (bandCentreHz (band - 1) * bandCentreHz (band));
        const double hi = band == kBands - 1 ? 13000.0 : std::sqrt (bandCentreHz (band) * bandCentreHz (band + 1));
        const int k0 = juce::jmax (1, (int) (lo * N / sr)), k1 = juce::jmin (N / 2, juce::jmax (k0, (int) (hi * N / sr)));
        double acc = 0;
        for (int k = k0; k <= k1; ++k) acc += a[(size_t) k].real();
        out[band] = (float) (8.6859 * acc / (k1 - k0 + 1));   // ln -> dB
        mean += out[band];
    }
    mean /= kBands;
    for (int band = 0; band < kBands; ++band) out[band] -= mean;
    return true;
}

//==============================================================================
VoiceProfile VoiceProfile::build (const std::vector<Take>& takes, const juce::String& name, const juce::String& range)
{
    VoiceProfile p;
    p.name = name; p.range = range;
    double sum[kPitchBins][kBands] = {};
    int cnt[kPitchBins] = {};
    std::vector<float> pitches, vibDepth, vibRate, drift, timing;

    for (auto& tk : takes)
    {
        if (tk.audio == nullptr) continue;
        const auto& s = *tk.audio;
        const float* x = s.audio.getReadPointer (0);
        float peak = -120.0f;
        for (auto& f : s.frames) peak = juce::jmax (peak, f.rmsDb);
        ++p.takesUsed;

        for (int f = 0; f < (int) s.frames.size(); f += 3)
        {
            const auto& fr = s.frames[(size_t) f];
            if (! fr.voiced || fr.clarity < 0.85f || fr.rmsDb < peak - 30.0f) continue;
            pitches.push_back (fr.midi);
            const int bin = juce::jlimit (0, kPitchBins - 1, (int) std::lround ((fr.midi - kLowMidi) / kStep));
            float bands[kBands];
            const float period = (float) (s.sampleRate / midiToHz (fr.midi));
            if (frameEnvelope (x, s.numSamples(), f * s.hop, s.sampleRate, period, bands))
            {
                for (int b = 0; b < kBands; ++b) sum[bin][b] += bands[b];
                ++cnt[bin];
            }
        }

        int nid = 1;
        auto notes = Analysis::detectNotes (s, nid);
        for (auto& n : notes)
        {
            const double len = n.srcEnd - n.srcStart;
            if (len < 0.6) continue;
            // detrended deviation
            std::vector<float> dev, mean;
            for (double t = n.srcStart + 0.1; t < n.srcEnd - 0.05; t += s.hop / s.sampleRate)
            {
                float f0 = 0; if (! s.pitchAt (t, f0)) continue;
                double acc = 0; int c = 0;
                for (double u = t - 0.15; u <= t + 0.15; u += s.hop / s.sampleRate)
                {
                    float g = 0; if (u >= n.srcStart && u <= n.srcEnd && s.pitchAt (u, g)) { acc += g; ++c; }
                }
                const float m = c > 0 ? (float) (acc / c) : f0;
                dev.push_back (f0 - m); mean.push_back (m - n.detected);
            }
            if (dev.size() < 20) continue;
            double v = 0; int zc = 0;
            for (size_t i = 0; i < dev.size(); ++i) { v += dev[i] * dev[i]; if (i > 0 && (dev[i] > 0) != (dev[i - 1] > 0)) ++zc; }
            const double sd = std::sqrt (v / dev.size());
            const double dur = dev.size() * s.hop / s.sampleRate;
            if (sd * 100.0 > 6.0)
            {
                vibDepth.push_back ((float) (sd * 141.0));
                vibRate.push_back ((float) (zc / 2.0 / dur));
            }
            double dv = 0; for (auto m : mean) dv += m * m;
            drift.push_back ((float) (std::sqrt (dv / mean.size()) * 100.0));
        }

        if (tk.clickBpm > 0)
        {
            const double grid = 60.0 / tk.clickBpm / 2.0;
            for (auto& n : notes)
            {
                const double off = n.srcStart - std::round (n.srcStart / grid) * grid;
                timing.push_back ((float) (off * 1000.0));
            }
        }
    }

    for (int b = 0; b < kPitchBins; ++b)
        if (cnt[b] >= 4)
        {
            p.has[b] = true;
            for (int k = 0; k < kBands; ++k) p.env[b][k] = (float) (sum[b][k] / cnt[b]);
        }

    // fill missing bins from the nearest measured ones (flat outside the range)
    int first = -1, last = -1;
    for (int b = 0; b < kPitchBins; ++b) if (p.has[b]) { if (first < 0) first = b; last = b; }
    if (first >= 0)
    {
        for (int b = 0; b < kPitchBins; ++b)
        {
            if (p.has[b]) continue;
            int lo = b, hi = b;
            while (lo >= 0 && ! p.has[lo]) --lo;
            while (hi < kPitchBins && ! p.has[hi]) ++hi;
            for (int k = 0; k < kBands; ++k)
            {
                if (lo < 0) p.env[b][k] = p.env[hi][k];
                else if (hi >= kPitchBins) p.env[b][k] = p.env[lo][k];
                else { const float t = (float) (b - lo) / (float) (hi - lo); p.env[b][k] = p.env[lo][k] + (p.env[hi][k] - p.env[lo][k]) * t; }
            }
        }
        // gentle smoothing across pitch
        float tmp[kPitchBins][kBands];
        std::memcpy (tmp, p.env, sizeof (tmp));
        for (int b = 1; b < kPitchBins - 1; ++b)
            for (int k = 0; k < kBands; ++k)
                p.env[b][k] = 0.25f * tmp[b - 1][k] + 0.5f * tmp[b][k] + 0.25f * tmp[b + 1][k];
        p.valid = true;
    }

    auto med = [] (std::vector<float> v, float dflt) { return v.empty() ? dflt : Analysis::medianOf (std::move (v)); };
    p.vibratoDepthCents = juce::jlimit (5.0f, 120.0f, med (vibDepth, 30.0f));
    p.vibratoRateHz     = juce::jlimit (3.5f, 8.0f, med (vibRate, 5.5f));
    p.driftCents        = juce::jlimit (1.0f, 40.0f, med (drift, 8.0f));
    if (! timing.empty())
    {
        double v = 0; for (auto t : timing) v += t * t;
        p.looseMs = juce::jlimit (4.0f, 40.0f, (float) std::sqrt (v / timing.size()));
    }
    if (! pitches.empty())
    {
        std::sort (pitches.begin(), pitches.end());
        p.lowMidi  = pitches[(size_t) (pitches.size() * 0.05)];
        p.highMidi = pitches[(size_t) (pitches.size() * 0.95)];
    }
    return p;
}

//==============================================================================
void VoiceProfile::apply (juce::AudioBuffer<float>& mono, double sr, const Plan& plan, const VoiceProfile& p, float amount)
{
    if (! p.valid || amount <= 0.001f || plan.size() == 0) return;
    constexpr int order = 11, N = 1 << order, H = N / 4;
    juce::dsp::FFT fft (order);
    const int n = mono.getNumSamples();
    float* x = mono.getWritePointer (0);

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / N);
    std::vector<float> out ((size_t) n, 0.0f), wsum ((size_t) n, 0.0f);
    std::vector<std::complex<float>> a ((size_t) N), b ((size_t) N);

    // per-bin band position
    std::vector<float> binBand ((size_t) N / 2 + 1);
    for (int k = 0; k <= N / 2; ++k) binBand[(size_t) k] = bandPosForHz (k * sr / N);

    float gains[kBands] = {};
    const int frames = n / H + 2;
    for (int f = 0; f < frames; ++f)
    {
        const int c = f * H;
        const double t = c / sr;
        const int pf = juce::jlimit (0, plan.size() - 1, (int) (t / plan.dt));
        const float in = plan.inMidi[(size_t) pf], o = plan.outMidi[(size_t) pf];
        for (int k = 0; k < kBands; ++k)
        {
            float target = 0.0f;
            if (in > 0 && o > 0 && std::abs (o - in) > 0.3f)
                target = juce::jlimit (-9.0f, 9.0f, amount * (p.envAt (o, k) - p.envAt (in, k)));
            gains[k] += 0.5f * (target - gains[k]);
        }
        bool any = false;
        for (int k = 0; k < kBands; ++k) if (std::abs (gains[k]) > 0.05f) any = true;

        const int s = c - N / 2;
        for (int i = 0; i < N; ++i) { const int j = s + i; a[(size_t) i] = (j >= 0 && j < n ? x[j] : 0.0f) * win[(size_t) i]; }
        if (any)
        {
            fft.perform (a.data(), b.data(), false);
            for (int k = 0; k <= N / 2; ++k)
            {
                const float bp = binBand[(size_t) k];
                const int b0 = juce::jlimit (0, kBands - 1, (int) bp), b1 = juce::jmin (kBands - 1, b0 + 1);
                const float tt = bp - (float) b0;
                const float g = juce::Decibels::decibelsToGain (gains[b0] + (gains[b1] - gains[b0]) * tt);
                b[(size_t) k] *= g;
                if (k > 0 && k < N / 2) b[(size_t) (N - k)] *= g;
            }
            fft.perform (b.data(), a.data(), true);
        }
        for (int i = 0; i < N; ++i)
        {
            const int j = s + i;
            if (j < 0 || j >= n) continue;
            out[(size_t) j] += a[(size_t) i].real() * win[(size_t) i];
            wsum[(size_t) j] += win[(size_t) i] * win[(size_t) i];
        }
    }
    for (int i = 0; i < n; ++i) x[i] = wsum[(size_t) i] > 1e-6f ? out[(size_t) i] / wsum[(size_t) i] : x[i];
}

//==============================================================================
juce::var VoiceProfile::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("name", name);
    o->setProperty ("range", range);
    o->setProperty ("vibratoRateHz", vibratoRateHz);
    o->setProperty ("vibratoDepthCents", vibratoDepthCents);
    o->setProperty ("driftCents", driftCents);
    o->setProperty ("looseMs", looseMs);
    o->setProperty ("lowMidi", lowMidi);
    o->setProperty ("highMidi", highMidi);
    o->setProperty ("takesUsed", takesUsed);
    o->setProperty ("valid", valid);
    juce::Array<juce::var> rows;
    for (int b = 0; b < kPitchBins; ++b)
    {
        juce::Array<juce::var> row;
        for (int k = 0; k < kBands; ++k) row.add (std::round (env[b][k] * 100.0f) / 100.0f);
        rows.add (row);
    }
    o->setProperty ("env", rows);
    return juce::var (o);
}

VoiceProfile VoiceProfile::fromVar (const juce::var& v)
{
    VoiceProfile p;
    p.name = v.getProperty ("name", p.name).toString();
    p.range = v.getProperty ("range", p.range).toString();
    p.vibratoRateHz = (float) v.getProperty ("vibratoRateHz", p.vibratoRateHz);
    p.vibratoDepthCents = (float) v.getProperty ("vibratoDepthCents", p.vibratoDepthCents);
    p.driftCents = (float) v.getProperty ("driftCents", p.driftCents);
    p.looseMs = (float) v.getProperty ("looseMs", p.looseMs);
    p.lowMidi = (float) v.getProperty ("lowMidi", p.lowMidi);
    p.highMidi = (float) v.getProperty ("highMidi", p.highMidi);
    p.takesUsed = (int) v.getProperty ("takesUsed", 0);
    p.valid = (bool) v.getProperty ("valid", false);
    if (auto* rows = v.getProperty ("env", {}).getArray())
        for (int b = 0; b < juce::jmin (kPitchBins, rows->size()); ++b)
            if (auto* row = (*rows)[b].getArray())
            {
                for (int k = 0; k < juce::jmin (kBands, row->size()); ++k) p.env[b][k] = (float) (*row)[k];
                p.has[b] = true;
            }
    return p;
}

juce::File VoiceProfile::folder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("HARMIVOX-6").getChildFile ("Voices");
    f.createDirectory();
    return f;
}

bool VoiceProfile::save (const juce::File& f) const
{
    return f.replaceWithText (juce::JSON::toString (toVar()));
}

VoiceProfile VoiceProfile::load (const juce::File& f)
{
    return fromVar (juce::JSON::parse (f));
}

juce::Array<juce::File> VoiceProfile::listSaved()
{
    auto files = folder().findChildFiles (juce::File::findFiles, false, "*.hvxvoice");
    files.sort();
    return files;
}
} // namespace hvx
