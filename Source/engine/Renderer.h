#pragma once
#include "Model.h"

namespace hvx
{
/** Frame-by-frame plan of what the output should do: where to read in the
    source, what pitch to hit, formant and level. Used for rendering and for
    drawing the corrected pitch line in the editor. */
struct Plan
{
    double dt = 256.0 / 44100.0;           // seconds per plan frame (output time)
    std::vector<float> srcTime;            // seconds in source
    std::vector<float> inMidi;             // detected pitch (0 = unvoiced)
    std::vector<float> outMidi;            // output pitch (0 = unvoiced)
    std::vector<float> formantSt;
    std::vector<float> gain;               // linear
    std::vector<WarpPoint> warps;          // effective warps (incl. humanize)
    double duration = 0;

    int size() const { return (int) srcTime.size(); }
};

/** Deterministic noise in [-1, 1] from a seed and some integers. */
float hashNoise (juce::uint32 seed, int a, int b = 0);

/** Track warps plus the per-note timing feel of generated voices. */
std::vector<WarpPoint> effectiveWarps (const Track& t);

/** Builds the full output plan for a track. */
Plan buildPlan (const Track& t, const SourceAudio& src);

/** Pitch at a source time for one note after correction (no join smoothing). */
float correctedPitchAt (const Track& t, const SourceAudio& src, const Note& n, double srcSec, float f0);

/** TD-PSOLA resynthesis following the plan. Returns mono at the source rate. */
juce::AudioBuffer<float> renderPsola (const SourceAudio& src, const juce::AudioBuffer<float>& audio, const Plan& plan);

inline double midiToHz (double m) { return 440.0 * std::pow (2.0, (m - 69.0) / 12.0); }
} // namespace hvx
