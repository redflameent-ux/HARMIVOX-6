#include "Model.h"

namespace hvx
{
//==============================================================================
static float lerpFrame (const std::vector<PitchFrame>& fr, double f, bool& voiced)
{
    voiced = false;
    if (fr.empty()) return 0.0f;
    const int n = (int) fr.size();
    int i0 = juce::jlimit (0, n - 1, (int) std::floor (f));
    int i1 = juce::jmin (n - 1, i0 + 1);
    const double t = juce::jlimit (0.0, 1.0, f - i0);
    const auto& a = fr[(size_t) i0];
    const auto& b = fr[(size_t) i1];
    if (a.voiced && b.voiced)  { voiced = true; return (float) (a.midi + (b.midi - a.midi) * t); }
    if (a.voiced && t < 0.5)   { voiced = true; return a.midi; }
    if (b.voiced && t >= 0.5)  { voiced = true; return b.midi; }
    return 0.0f;
}

bool SourceAudio::pitchAt (double seconds, float& midiOut) const
{
    bool v = false;
    midiOut = lerpFrame (frames, secondsToFrame (seconds), v);
    return v;
}

float SourceAudio::rmsDbAt (double seconds) const
{
    if (frames.empty()) return -120.0f;
    int i = juce::jlimit (0, (int) frames.size() - 1, (int) std::lround (secondsToFrame (seconds)));
    return frames[(size_t) i].rmsDb;
}

//==============================================================================
Track* Document::find (int id)
{
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}
const Track* Document::find (int id) const
{
    for (auto& t : tracks) if (t.id == id) return &t;
    return nullptr;
}
Track* Document::lead()
{
    for (auto& t : tracks) if (t.isLead) return &t;
    return tracks.empty() ? nullptr : &tracks.front();
}
const Track* Document::lead() const
{
    for (auto& t : tracks) if (t.isLead) return &t;
    return tracks.empty() ? nullptr : &tracks.front();
}
int Document::indexOf (int id) const
{
    for (int i = 0; i < (int) tracks.size(); ++i) if (tracks[(size_t) i].id == id) return i;
    return -1;
}

//==============================================================================
const juce::StringArray& scaleNames()
{
    static juce::StringArray s { "Major", "Minor", "Harmonic minor", "Dorian", "Mixolydian",
                                 "Phrygian", "Major pentatonic", "Minor pentatonic", "Blues", "Chromatic" };
    return s;
}

const std::vector<int>& scaleIntervals (int scale)
{
    static const std::vector<std::vector<int>> sc {
        { 0, 2, 4, 5, 7, 9, 11 },     // major
        { 0, 2, 3, 5, 7, 8, 10 },     // natural minor
        { 0, 2, 3, 5, 7, 8, 11 },     // harmonic minor
        { 0, 2, 3, 5, 7, 9, 10 },     // dorian
        { 0, 2, 4, 5, 7, 9, 10 },     // mixolydian
        { 0, 1, 3, 5, 7, 8, 10 },     // phrygian
        { 0, 2, 4, 7, 9 },            // major pentatonic
        { 0, 3, 5, 7, 10 },           // minor pentatonic
        { 0, 3, 5, 6, 7, 10 },        // blues
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    return sc[(size_t) juce::jlimit (0, (int) sc.size() - 1, scale)];
}

const juce::StringArray& keyNames()
{
    static juce::StringArray k { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return k;
}

const juce::StringArray& chordQualityNames()
{
    static juce::StringArray q { "", "m", "dim", "aug", "sus2", "sus4", "7", "maj7", "m7" };
    return q;
}

std::vector<int> chordPitchClasses (int root, int quality)
{
    static const std::vector<std::vector<int>> shapes {
        { 0, 4, 7 }, { 0, 3, 7 }, { 0, 3, 6 }, { 0, 4, 8 }, { 0, 2, 7 }, { 0, 5, 7 },
        { 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 } };
    std::vector<int> out;
    if (root < 0) return out;
    for (int iv : shapes[(size_t) juce::jlimit (0, (int) shapes.size() - 1, quality)])
        out.push_back ((root + iv) % 12);
    return out;
}

juce::String noteName (float midi)
{
    const int m = (int) std::lround (midi);
    const int pc = ((m % 12) + 12) % 12;
    return keyNames()[pc] + juce::String (m / 12 - 1);   // 60 = C4
}

juce::String chordName (const Chord& c)
{
    if (c.root < 0) return "N.C.";
    return keyNames()[c.root % 12] + chordQualityNames()[c.quality];
}

bool inScale (int midi, int key, int scale)
{
    const int pc = (((midi - key) % 12) + 12) % 12;
    for (int iv : scaleIntervals (scale)) if (iv == pc) return true;
    return false;
}

int snapToScale (float midi, int key, int scale)
{
    const int base = (int) std::lround (midi);
    int best = base; float bestD = 1000.0f;
    for (int d = -6; d <= 6; ++d)
    {
        const int c = base + d;
        if (! inScale (c, key, scale)) continue;
        const float dist = std::abs ((float) c - midi);
        if (dist < bestD) { bestD = dist; best = c; }
    }
    return best;
}

//==============================================================================
static double mapPiecewise (const std::vector<WarpPoint>& w, double x, bool forward)
{
    if (w.empty()) return x;
    auto in  = [forward] (const WarpPoint& p) { return forward ? p.src : p.dst; };
    auto out = [forward] (const WarpPoint& p) { return forward ? p.dst : p.src; };

    if (x <= in (w.front())) return x + (out (w.front()) - in (w.front()));
    if (x >= in (w.back()))  return x + (out (w.back())  - in (w.back()));

    size_t lo = 0, hi = w.size() - 1;
    while (hi - lo > 1)
    {
        const size_t mid = (lo + hi) / 2;
        if (in (w[mid]) <= x) lo = mid; else hi = mid;
    }
    const double span = in (w[hi]) - in (w[lo]);
    const double t = span > 1e-9 ? (x - in (w[lo])) / span : 0.0;
    return out (w[lo]) + t * (out (w[hi]) - out (w[lo]));
}

double srcToDst (const std::vector<WarpPoint>& w, double src) { return mapPiecewise (w, src, true); }
double dstToSrc (const std::vector<WarpPoint>& w, double dst) { return mapPiecewise (w, dst, false); }

juce::Colour trackColourForIndex (int index)
{
    // Ember-to-ice palette (distinct per lane)
    static const juce::uint32 cols[] = {
        0xffff6a3d, // ember (lead)
        0xffffb547, // amber
        0xffe8486e, // crimson rose
        0xff7c8cff, // periwinkle
        0xff3fd0c9, // ice teal
        0xffb36bff, // violet
        0xff9be15d, // lime
        0xffff8fd2, // pink
        0xff5ab0ff, // sky
        0xfff2e25c, // lemon
    };
    return juce::Colour (cols[(size_t) index % (sizeof (cols) / sizeof (cols[0]))]);
}

} // namespace hvx
