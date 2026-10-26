#pragma once
#include "Model.h"

namespace hvx
{
/** One voice to create from the Add voices dialog. */
struct VoiceRequest
{
    int   steps = 0;          // scale steps (0 = double). Negative = below.
    int   takes = 1;          // 2 = L/R pair, 4 = quad
    float levelDb = -4.0f;
};

struct HarmonyPreset
{
    juce::String name, description;
    std::vector<VoiceRequest> voices;
    IntervalMode mode = IntervalMode::FollowKey;
    float looseness = 0.25f;  // 0..1 (tight..loose)
};

struct Harmony
{
    static const std::vector<HarmonyPreset>& presets();   // main presets
    static const std::vector<HarmonyPreset>& ideas();     // "harmony ideas"

    /** Labels for the interval grid: Double, 2nd .. 10th (index = steps). */
    static const juce::StringArray& intervalLabels();
    static int exactSemitones (int steps);
    static bool isSpicy (int steps);                      // 2nd, 7th, 9th

    static juce::String voiceName (int steps, int takeIndex, int takeCount, const juce::String& parentName);

    /** Where a harmony voice should sit for a lead note. Keeps the lead's cents. */
    static float harmonyTarget (float leadTarget, const VoiceSpec& v, int key, int scale,
                                const std::vector<Chord>& chords, double hostTimeSec);

    /** One chord per bar from the lead melody (diatonic triads). */
    static std::vector<Chord> autoChords (const Document& doc);

    static const Chord* chordAt (const std::vector<Chord>& chords, double t);
};
} // namespace hvx
