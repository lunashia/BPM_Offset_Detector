#pragma once

#include <vector>

#include "OnsetDetectorFFT.h"
#include "TempoDetector.h"

namespace Vortex {

using Milliseconds = float; // unit: ms

/*
 * WHAT:
 *   Estimate beat phase offset from tempo candidates and onset events.
 *
 * WHY:
 *   Tempo alone does not determine beat grid alignment in time; this function
 *   selects a phase offset so beat positions align with onset support.
 *
 * INPUT / OUTPUT:
 *   - tempos: tempo candidates with fitness.
 *   - onsets: detected onset events; onset.pos is sample index.
 *   - sampleRate: Hz.
 *   - return: offset in milliseconds.
 *
 * ASSUMPTIONS:
 *   - Offset is relative to audio start (t=0), not to an external reference grid.
 *   - Output is normalized to one beat period: [0, beat_ms).
 */
Milliseconds CalculateOffset(
    const std::vector<TempoResult>& tempos,
    const std::vector<Onset>& onsets,
    SampleRateHz sampleRate);

} // namespace Vortex
