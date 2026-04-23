#include "load_audio.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

// Build note:
// - This file assumes miniaudio is available in the include path.
// - In exactly one translation unit, MINIAUDIO_IMPLEMENTATION must be defined before including miniaudio.h.
#include "miniaudio.h"

/*
 * WHAT:
 *   Decode file audio into mono float PCM and expose timeline metadata.
 *
 * WHY:
 *   The rhythm pipeline only needs one mono stream; averaging channels removes
 *   API complexity and keeps onset/tempo math in a single sample timeline.
 *
 * INPUT / OUTPUT:
 *   - path: source file path.
 *   - audio: output mono samples.
 *   - numFrames: output frame count; set to audio.size() on success.
 *   - sampleRate: output in Hz.
 *
 * ASSUMPTIONS:
 *   - miniaudio decoder returns interleaved frames for multi-channel sources.
 *   - Output sample index i maps directly to analysis sample index i.
 *
 * ERROR SOURCES:
 *   - Decoder init/read failures.
 *   - Invalid decoder metadata (channels == 0 or sampleRate <= 0).
 *   - Empty decoded payload.
 */
bool loadAudio(
    const std::string& path,
    std::vector<float>& audio,
    int& numFrames,
    int& sampleRate
) {
    audio.clear();
    numFrames = 0;
    sampleRate = 0;

    if (path.empty()) {
        return false;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);

    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        return false;
    }

    const ma_uint32 channels = decoder.outputChannels;
    sampleRate = static_cast<int>(decoder.outputSampleRate);

    if (channels == 0 || sampleRate <= 0) {
        ma_decoder_uninit(&decoder);
        return false;
    }

    constexpr ma_uint64 kChunkFrames = 4096;
    std::vector<float> chunk;
    chunk.resize(static_cast<size_t>(kChunkFrames * channels));

    std::vector<float> interleaved;

    bool ok = true;
    while (true) {
        ma_uint64 framesRead = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kChunkFrames, &framesRead);

        if (r != MA_SUCCESS && r != MA_AT_END) {
            ok = false;
            break;
        }

        if (framesRead > 0) {
            const size_t samplesRead = static_cast<size_t>(framesRead * channels);
            interleaved.insert(interleaved.end(), chunk.begin(), chunk.begin() + samplesRead);
        }

        if (r == MA_AT_END || framesRead == 0) {
            break;
        }
    }

    ma_decoder_uninit(&decoder);

    if (!ok) {
        audio.clear();
        numFrames = 0;
        sampleRate = 0;
        return false;
    }

    if (interleaved.empty()) {
        // Contract:
        //   Failure should expose a clean state to callers.
        audio.clear();
        numFrames = 0;
        sampleRate = 0;
        return false;
    }

    if ((interleaved.size() % channels) != 0u) {
        // Defensive guard: malformed decoder output should not leak partial frame state.
        audio.clear();
        numFrames = 0;
        sampleRate = 0;
        return false;
    }

    const size_t totalFrames = interleaved.size() / channels;
    if (totalFrames > static_cast<size_t>(std::numeric_limits<int>::max())) {
        // Prevent narrowing overflow when writing numFrames.
        audio.clear();
        numFrames = 0;
        sampleRate = 0;
        return false;
    }

    // ASSUMPTION:
    //   Decoder output is interleaved by frame. We intentionally downmix by
    //   per-frame channel average because downstream modules consume mono only.
    if (channels == 1) {
        audio = std::move(interleaved);
    } else {
        audio.resize(totalFrames);

        for (size_t f = 0; f < totalFrames; ++f) {
            float sum = 0.0f;
            const size_t base = f * channels;
            for (ma_uint32 c = 0; c < channels; ++c) {
                sum += interleaved[base + c];
            }

            float mono = sum / static_cast<float>(channels);
            mono = std::max(-1.0f, std::min(1.0f, mono));
            audio[f] = mono;
        }
    }

    numFrames = static_cast<int>(audio.size());
    return true;
}
