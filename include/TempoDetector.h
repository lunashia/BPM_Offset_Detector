#pragma once

#include <vector>

#include "OnsetDetectorFFT.h"

namespace Vortex {

using Bpm = float;         // unit: beats per minute
using Fitness = float;     // unitless confidence score

struct TempoResult {
    Bpm bpm;
    // ASSUMPTION:
    //   normalized confidence in [0, 1], larger means stronger evidence.
    Fitness fitness;
};

/*
 * WHAT:
 *   Contract-only declaration for tempo candidate output.
 *
 * WHY:
 *   Kept as a shared data contract so onset/offset/estimator modules can exchange
 *   tempo candidates without coupling to a specific tempo implementation file.
 *
 * ASSUMPTIONS:
 *   - bpm unit is beats-per-minute.
 *   - onset.pos uses sample index in the same timeline as sampleRate.
 */
std::vector<TempoResult> CalculateTempo(const std::vector<Onset>& onsets, SampleRateHz sampleRate);

} // namespace Vortex
