#pragma once
#include "Model.h"

namespace hvx
{
/** Clean step: noise reduction, breath control, gate between phrases, de-click. */
struct Cleaner
{
    static juce::AudioBuffer<float> process (const SourceAudio& src, const std::vector<Note>& notes,
                                             const CleanSettings& s);
    static bool isNeutral (const CleanSettings& s)
    {
        return s.noiseReduce <= 0.001f && s.breathDb <= 0.01f && s.gate <= 0.001f && s.deClick <= 0.001f;
    }
};
} // namespace hvx
