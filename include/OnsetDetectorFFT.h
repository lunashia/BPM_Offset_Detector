#pragma once

#include <vector>

namespace Vortex {

// Public API scalar aliases for unit clarity and cross-header consistency.
using SampleIndex = int;   // unit: sample
using FrameCount = int;    // unit: PCM frame in mono timeline
using SampleRateHz = int;  // unit: Hz (samples per second)

struct Onset {
    // ASSUMPTION:
    //   pos is always the absolute sample index in the original mono PCM timeline.
    //   (Not STFT frame index and not milliseconds.)
    SampleIndex pos;
    // Relative onset confidence, dimensionless.
    float strength;
};

/*
 * WHAT:
 *   Detect onset events from mono PCM audio using FFT-domain novelty.
 *
 * WHY:
 *   Onset events provide a sparse rhythmic representation that is more stable
 *   for tempo/offset estimation than raw waveform peaks.
 *
 * INPUT / OUTPUT:
 *   - audio: mono PCM samples.
 *   - numFrames: PCM frame count in audio timeline (expected to match audio.size()).
 *   - sampleRate: Hz.
 *   - return: detected onsets, each with sample-index position and strength.
 *
 * ASSUMPTIONS:
 *   - audio is mono, sample-aligned, and sampled at sampleRate.
 *   - onset.pos shares the same sample timeline as audio and sampleRate.
 */
std::vector<Onset> DetectOnsetsFFT(const std::vector<float>& audio, FrameCount numFrames, SampleRateHz sampleRate);

} // namespace Vortex
