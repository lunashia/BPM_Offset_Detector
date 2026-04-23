#pragma once

#include <vector>

#include "OnsetDetectorFFT.h"
#include "TempoDetector.h"

namespace TempoEstimatorB {

/*
 * WHAT:
 *   Stable production tempo estimator (frozen "B" algorithm).
 *
 * WHY:
 *   This interface isolates the agreed stable implementation so future
 *   experimental variants can evolve without changing production behavior.
 *
 * INPUT / OUTPUT:
 *   - onsets: rhythmic events in sample-index timeline.
 *   - sampleRate: Hz.
 *   - return: tempo candidates sorted by descending fitness.
 *
 * ASSUMPTIONS:
 *   - onset.pos unit is sample index.
 *   - Frozen key parameter: autocorrelation envelope bin size = 0.0005s.
 *   - Expected working BPM search range is [80, 200].
 *
 * NAMING NOTE:
 *   - Public entrypoint uses lowerCamelCase (estimateTempo) for this module.
 *   - Vortex utility APIs keep their existing PascalCase names for backward compatibility.
 */
std::vector<Vortex::TempoResult> estimateTempo(
    const std::vector<Vortex::Onset>& onsets,
    Vortex::SampleRateHz sampleRate);

} // namespace TempoEstimatorB
