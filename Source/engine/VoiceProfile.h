#pragma once
#include "Renderer.h"

namespace hvx
{
/** What the Voice lab learns about a singer: how their tone changes across
    their range, plus their natural vibrato, drift and timing feel. Generated
    voices use it so shifted harmonies keep sounding like that singer. */
struct VoiceProfile
{
    static constexpr int kBands = 32;
    static constexpr int kPitchBins = 17;      // MIDI 36..84 in steps of 3
    static constexpr int kLowMidi = 36, kStep = 3;

    juce::String name = "My voice";
    juce::String range = "Middle voice (tenor, alto)";
    float env[kPitchBins][kBands] {};
    bool  has[kPitchBins] {};
    float vibratoRateHz = 5.5f, vibratoDepthCents = 30.0f, driftCents = 8.0f, looseMs = 12.0f;
    float lowMidi = 48.0f, highMidi = 67.0f;
    int   takesUsed = 0;
    bool  valid = false;

    float envAt (float midi, int band) const;

    juce::var toVar() const;
    static VoiceProfile fromVar (const juce::var&);
    bool save (const juce::File&) const;
    static VoiceProfile load (const juce::File&);

    static juce::File folder();               // Documents/HARMIVOX-6/Voices
    static juce::Array<juce::File> listSaved();

    /** One recorded phrase: analysed audio plus what kind of exercise it was. */
    struct Take { std::shared_ptr<const SourceAudio> audio; juce::String kind; double clickBpm = 0; };

    static VoiceProfile build (const std::vector<Take>& takes, const juce::String& name, const juce::String& range);

    /** Shapes a rendered voice so its tone follows the singer at the new pitch. */
    static void apply (juce::AudioBuffer<float>& mono, double sr, const Plan& plan, const VoiceProfile& p, float amount);

    /** Band envelope (dB, mean removed) of one frame. Returns false if too quiet. */
    static bool frameEnvelope (const float* x, int n, int centre, double sr, float periodSamples, float* outBands);
    static double bandCentreHz (int band);
};
} // namespace hvx
