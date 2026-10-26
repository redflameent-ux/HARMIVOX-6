#pragma once
#include "Model.h"

namespace hvx
{
/** Finish step: vocal chain (low cut, de-plosive, mud cut, de-esser,
    presence, air, compressor) and the per-voice stereo spread. */
struct Finish
{
    static const juce::StringArray& chainNames();     // Off, Natural, Pop, Rap, Airy
    static FinishSettings preset (int chain);

    /** Processes a mono buffer in place. belt adds a pushed brightness. */
    static void process (juce::AudioBuffer<float>& mono, double sr, const FinishSettings& s, float belt);

    /** Mono -> stereo with decorrelation ("stereoize"). seed picks the flavour. */
    static juce::AudioBuffer<float> toStereo (const juce::AudioBuffer<float>& mono, double sr, float amount, juce::uint32 seed);
};
} // namespace hvx
