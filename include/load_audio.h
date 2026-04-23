#pragma once

#include <string>
#include <vector>

using AudioFrameCount = int;    // unit: sample frame in mono PCM timeline
using AudioSampleRateHz = int;  // unit: Hz (samples per second)

/*
 * WHAT:
 *   Decode an audio file and return a mono PCM float buffer.
 *
 * WHY:
 *   The downstream onset/tempo/offset pipeline assumes a single amplitude series.
 *   This API hides decoder/channel details so callers can focus on rhythm logic.
 *
 * INPUT / OUTPUT:
 *   - path: input audio file path.
 *   - audio: output mono samples, normalized roughly in [-1, 1].
 *   - numFrames: output PCM frame count; on success it MUST equal audio.size().
 *   - sampleRate: output sample rate in Hz.
 *
 * ASSUMPTIONS:
 *   - Decoder reads interleaved multi-channel audio; function downmixes by per-frame mean.
 *   - Time index i in audio[i] corresponds to sample index i in later modules.
 *
 * ERROR NOTES:
 *   - Returns false on decode/validation failure.
 *   - The implementation is intentionally conservative for invalid sampleRate/channels.
 *
 * NAMING NOTE:
 *   - Uses lowerCamelCase to preserve existing call sites.
 */
bool loadAudio(
    const std::string& path,
    std::vector<float>& audio,
    AudioFrameCount& numFrames,
    AudioSampleRateHz& sampleRate
);
