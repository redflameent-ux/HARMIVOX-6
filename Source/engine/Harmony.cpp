#include "Harmony.h"
#include <map>

namespace hvx
{
const std::vector<HarmonyPreset>& Harmony::presets()
{
    static const std::vector<HarmonyPreset> p {
        { "Double (L/R)",   "Two more takes of the lead, panned left and right: the classic thick lead.",
          { { 0, 2, -4.0f } }, IntervalMode::FollowKey, 0.25f },
        { "Quad doubles",   "Four doubles spread across the stereo field for a huge, glued lead.",
          { { 0, 4, -7.0f } }, IntervalMode::FollowKey, 0.35f },
        { "Pop stack",      "Doubles plus a third above and below: radio-ready hook stack.",
          { { 0, 2, -5.0f }, { 2, 2, -8.0f }, { -2, 2, -9.0f } }, IntervalMode::FollowKey, 0.25f },
        { "Thirds & fifths","Third and fifth above, both as L/R pairs.",
          { { 2, 2, -6.0f }, { 4, 2, -8.0f } }, IntervalMode::FollowKey, 0.25f },
        { "Wide choir",     "Loose doubles, harmonies and a low octave: sounds like a room full of singers.",
          { { 0, 4, -8.0f }, { 2, 2, -9.0f }, { -3, 2, -10.0f }, { -7, 1, -12.0f } }, IntervalMode::FollowKey, 0.7f },
        { "Chord pad",      "Every voice lands on a chord tone, following the chord lane.",
          { { 2, 1, -7.0f }, { 4, 1, -8.0f }, { -2, 1, -9.0f }, { -7, 1, -11.0f } }, IntervalMode::FollowChords, 0.3f },
        { "Octave layers",  "One octave up and one octave down for weight and shine.",
          { { 7, 1, -10.0f }, { -7, 1, -8.0f } }, IntervalMode::FollowKey, 0.2f },
        { "Air layer",      "Quiet high octave pair that adds air on top.",
          { { 7, 2, -15.0f } }, IntervalMode::FollowKey, 0.3f },
    };
    return p;
}

const std::vector<HarmonyPreset>& Harmony::ideas()
{
    static const std::vector<HarmonyPreset> p {
        { "Classic 3rd",  "A third above, doubled L/R. Works on almost any melody.",
          { { 2, 2, -6.0f } }, IntervalMode::FollowKey, 0.25f },
        { "Spicy 7th",    "Third and seventh above for a smoky, R&B colour.",
          { { 2, 1, -7.0f }, { 6, 1, -9.0f } }, IntervalMode::FollowKey, 0.25f },
        { "Dreamy 9th",   "Fifth and ninth above: floating, open, unresolved.",
          { { 4, 1, -8.0f }, { 8, 1, -10.0f } }, IntervalMode::FollowKey, 0.3f },
        { "Gospel stack", "Third, fifth, third below and low octave following the chords.",
          { { 2, 1, -6.0f }, { 4, 1, -7.0f }, { -2, 1, -8.0f }, { -7, 1, -10.0f } }, IntervalMode::FollowChords, 0.35f },
        { "Power",        "Fifth below and octave below: dark and heavy.",
          { { -3, 1, -7.0f }, { -7, 1, -8.0f } }, IntervalMode::FollowKey, 0.2f },
        { "Dissonant",    "Second above and seventh below for tension and unease.",
          { { 1, 1, -9.0f }, { -6, 1, -10.0f } }, IntervalMode::FollowKey, 0.3f },
    };
    return p;
}

const juce::StringArray& Harmony::intervalLabels()
{
    static juce::StringArray s { "Double", "2nd", "3rd", "4th", "5th", "6th", "7th", "Oct", "9th", "10th" };
    return s;
}

int Harmony::exactSemitones (int steps)
{
    static const int st[] = { 0, 2, 4, 5, 7, 9, 10, 12, 14, 16 };
    const int a = juce::jlimit (0, 9, std::abs (steps));
    return steps < 0 ? -st[a] : st[a];
}

bool Harmony::isSpicy (int steps)
{
    const int a = std::abs (steps);
    return a == 1 || a == 6 || a == 8;
}

juce::String Harmony::voiceName (int steps, int takeIndex, int takeCount, const juce::String& parentName)
{
    juce::String base;
    if (steps == 0) base = "Double";
    else base = intervalLabels()[juce::jlimit (0, 9, std::abs (steps))] + (steps > 0 ? " above" : " below");

    juce::String side;
    if (takeCount == 2) side = takeIndex == 0 ? " L" : " R";
    else if (takeCount > 2)
    {
        static const char* q[] = { " L", " R", " LL", " RR", " L3", " R3" };
        side = q[takeIndex % 6];
    }
    return base + side + " (" + parentName + ")";
}

//==============================================================================
static float keyBased (float lead, int steps, int key, int scale)
{
    const int p = (int) std::lround (lead);
    const float cents = lead - (float) p;
    // reference = nearest scale note at or below p
    int ref = p;
    for (int d = 0; d < 12; ++d) if (inScale (p - d, key, scale)) { ref = p - d; break; }
    const int chromaticRest = p - ref;

    std::vector<int> tones;
    for (int m = ref - 40; m <= ref + 40; ++m) if (inScale (m, key, scale)) tones.push_back (m);
    int idx = 0;
    for (int i = 0; i < (int) tones.size(); ++i) if (tones[(size_t) i] == ref) { idx = i; break; }

    // an octave is always 12 semitones, whatever the scale size
    const int octaves = steps / 7;
    const int rest = steps % 7;
    const int perOct = (int) scaleIntervals (scale).size();
    int restSteps = rest;
    if (perOct != 7) restSteps = (int) std::lround (rest * perOct / 7.0);
    const int target = juce::jlimit (0, (int) tones.size() - 1, idx + restSteps);
    return (float) (tones[(size_t) target] + chromaticRest + 12 * octaves) + cents;
}

const Chord* Harmony::chordAt (const std::vector<Chord>& chords, double t)
{
    for (auto& c : chords) if (t >= c.start && t < c.end) return &c;
    if (! chords.empty() && t >= chords.back().end) return &chords.back();
    return nullptr;
}

float Harmony::harmonyTarget (float lead, const VoiceSpec& v, int key, int scale,
                              const std::vector<Chord>& chords, double hostTime)
{
    if (v.steps == 0) return lead;
    if (v.mode == IntervalMode::Exact) return lead + (float) exactSemitones (v.steps);

    if (v.mode == IntervalMode::FollowChords)
    {
        const Chord* c = chordAt (chords, hostTime);
        const int a = std::abs (v.steps);
        int chordSteps = -1, extraOct = 0;
        switch (a)
        {
            case 2: chordSteps = 1; break;
            case 4: chordSteps = 2; break;
            case 6: chordSteps = 3; break;
            case 7: chordSteps = 0; extraOct = 1; break;
            case 9: chordSteps = 1; extraOct = 1; break;
            default: break;
        }
        if (c != nullptr && c->root >= 0 && chordSteps >= 0)
        {
            if (chordSteps == 0) return lead + (v.steps > 0 ? 12.0f : -12.0f);
            auto pcs = chordPitchClasses (c->root, c->quality);
            const int p = (int) std::lround (lead);
            const float cents = lead - (float) p;
            const int dir = v.steps > 0 ? 1 : -1;
            int found = 0, m = p;
            for (int guard = 0; guard < 48 && found < chordSteps; ++guard)
            {
                m += dir;
                const int pc = ((m % 12) + 12) % 12;
                if (std::find (pcs.begin(), pcs.end(), pc) != pcs.end()) ++found;
            }
            return (float) (m + dir * 12 * extraOct) + cents;
        }
    }
    const int dir = v.steps > 0 ? 1 : -1;
    if (dir > 0) return keyBased (lead, v.steps, key, scale);

    // below: walk down the scale
    const int p = (int) std::lround (lead);
    const float cents = lead - (float) p;
    int ref = p;
    for (int d = 0; d < 12; ++d) if (inScale (p - d, key, scale)) { ref = p - d; break; }
    std::vector<int> tones;
    for (int m = ref - 40; m <= ref + 40; ++m) if (inScale (m, key, scale)) tones.push_back (m);
    int idx = 0;
    for (int i = 0; i < (int) tones.size(); ++i) if (tones[(size_t) i] == ref) { idx = i; break; }
    const int a = -v.steps;
    const int octaves = a / 7, rest = a % 7;
    const int perOct = (int) scaleIntervals (scale).size();
    const int restSteps = perOct == 7 ? rest : (int) std::lround (rest * perOct / 7.0);
    const int target = juce::jlimit (0, (int) tones.size() - 1, idx - restSteps);
    return (float) (tones[(size_t) target] + (p - ref) - 12 * octaves) + cents;
}

//==============================================================================
std::vector<Chord> Harmony::autoChords (const Document& doc)
{
    std::vector<Chord> out;
    const Track* lead = doc.lead();
    if (lead == nullptr || lead->notes.empty() || doc.bpm <= 0) return out;

    const double bar = doc.beatsPerBar * 60.0 / doc.bpm;
    double endT = 0;
    for (auto& n : lead->notes) endT = juce::jmax (endT, lead->clipStart + srcToDst (lead->warps, n.srcEnd));
    const int bars = (int) std::ceil (endT / bar) + 1;

    // diatonic triads of the key
    const auto& iv = scaleIntervals (doc.scale <= 5 ? doc.scale : (doc.scale == 7 ? 1 : 0));
    struct Cand { int root, quality; float bonus; };
    std::vector<Cand> cands;
    for (int d = 0; d < (int) iv.size() && iv.size() == 7; ++d)
    {
        const int r = (doc.key + iv[(size_t) d]) % 12;
        const int third = (iv[(size_t) (d + 2) % 7] - iv[(size_t) d] + 12) % 12;
        const int fifth = (iv[(size_t) (d + 4) % 7] - iv[(size_t) d] + 12) % 12;
        int q = 0;
        if (third == 4 && fifth == 7) q = 0;
        else if (third == 3 && fifth == 7) q = 1;
        else if (third == 3 && fifth == 6) q = 2;
        else if (third == 4 && fifth == 8) q = 3;
        const float bonus = (d == 0) ? 0.25f : (d == 3 || d == 4) ? 0.12f : (d == 5) ? 0.06f : 0.0f;
        if (q != 2) cands.push_back ({ r, q, bonus });
    }
    if (cands.empty()) cands.push_back ({ doc.key, doc.scale == 1 ? 1 : 0, 0.0f });

    Chord prev { 0, 0, doc.key, doc.scale == 1 ? 1 : 0 };
    for (int b = 0; b < bars; ++b)
    {
        const double a = b * bar, e = a + bar;
        float w[12] = {}; float tot = 0;
        for (auto& n : lead->notes)
        {
            const double ns = lead->clipStart + srcToDst (lead->warps, n.srcStart);
            const double ne = lead->clipStart + srcToDst (lead->warps, n.srcEnd);
            const double ov = juce::jmin (e, ne) - juce::jmax (a, ns);
            if (ov <= 0 || n.muted) continue;
            const int pc = (((int) std::lround (n.target)) % 12 + 12) % 12;
            // earlier notes in the bar weigh a bit more (downbeat)
            const float wt = (float) ov * (ns < a + bar * 0.3 ? 1.3f : 1.0f);
            w[pc] += wt; tot += wt;
        }
        Chord c { a, e, prev.root, prev.quality };
        if (tot > 0)
        {
            float best = -1e9f;
            for (auto& cd : cands)
            {
                auto pcs = chordPitchClasses (cd.root, cd.quality);
                float sc = 0;
                for (int pc = 0; pc < 12; ++pc)
                {
                    const bool inC = std::find (pcs.begin(), pcs.end(), pc) != pcs.end();
                    sc += inC ? w[pc] : -0.45f * w[pc];
                }
                sc = sc / tot + cd.bonus + (cd.root == prev.root ? 0.05f : 0.0f);
                if (sc > best) { best = sc; c.root = cd.root; c.quality = cd.quality; }
            }
        }
        if (! out.empty() && out.back().root == c.root && out.back().quality == c.quality) out.back().end = e;
        else out.push_back (c);
        prev = c;
    }
    return out;
}
} // namespace hvx
