#pragma once
#include "Model.h"
#include <atomic>

namespace hvx
{
/** Pitch tracking (YIN), pitch marks, note detection and key detection. */
struct Analysis
{
    /** Turns mono audio into an analysed SourceAudio (frames + pitch marks). */
    static std::shared_ptr<SourceAudio> analyse (juce::AudioBuffer<float> mono, double sampleRate,
                                                 const juce::String& name,
                                                 const std::atomic<bool>* cancel = nullptr);

    /** Finds the sung notes. Targets are left equal to the detected pitch. */
    static std::vector<Note> detectNotes (const SourceAudio& src, int& nextNoteId);

    /** Krumhansl key finding on note durations. Picks major or minor. */
    static void detectKey (const std::vector<Note>& notes, int& key, int& scale);

    /** Pitch tracker on a short block; used by the Voice lab live meter. */
    static bool quickPitch (const float* x, int n, double sampleRate, float& midiOut, float& clarityOut);

    static float medianOf (std::vector<float> v);
};
} // namespace hvx
