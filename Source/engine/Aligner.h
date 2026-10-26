#pragma once
#include "Model.h"

namespace hvx
{
/** Time step: quantize to the grid and align takes to the lead. */
struct Aligner
{
    /** Moves note starts toward the beat grid. amount 0..1, division e.g. 16 for 1/16. */
    static void quantize (Track& t, double bpm, int division, float amount);

    /** Aligns a recorded take to the lead using dynamic time warping. */
    static void alignTakeToLead (Track& take, const Track& lead, float tightness, bool alignEnds);

    /** Keeps warps sorted and strictly increasing. */
    static void tidy (std::vector<WarpPoint>& w);

    /** Places one warp point so that srcSec plays at dstSec, anchoring neighbours. */
    static void setWarp (Track& t, double srcSec, double dstSec);
};
} // namespace hvx
