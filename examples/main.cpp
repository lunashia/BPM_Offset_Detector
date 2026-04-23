#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "OffsetDetector.h"
#include "OnsetDetectorFFT.h"
#include "load_audio.h"
#include "tempo_estimator_b.h"

/*
 * WHAT:
 *   Minimal release example for stable pipeline:
 *   loadAudio -> DetectOnsetsFFT -> estimateTempo(B) -> CalculateOffset.
 *
 * WHY:
 *   Demonstrates the intended production integration path with explicit unit flow:
 *   samples -> BPM -> offset(ms).
 *
 * ASSUMPTIONS:
 *   - Input file can be decoded to mono PCM by loadAudio.
 *   - onset.pos remains in sample units throughout the pipeline.
 */
int main(int argc, char** argv)
{
    auto fileExists = [](const std::string& path) {
        std::ifstream stream(path.c_str(), std::ios::binary);
        return stream.good();
    };

    std::string inputPath;
    if(argc >= 2) {
        inputPath = argv[1];
    } else {
        const std::vector<std::string> fallbackCandidates = {
            "test.wav",
            "../test.wav",
            "../../test.wav",
            "../../../test.wav"
        };
        for(const std::string& candidate : fallbackCandidates) {
            if(fileExists(candidate)) {
                inputPath = candidate;
                break;
            }
        }
    }

    if(inputPath.empty()) {
        std::cerr << "Error: input path is empty and no default test.wav was found.\n";
        return EXIT_FAILURE;
    }

    std::vector<float> audio;
    int numFrames = 0;
    int sampleRate = 0;
    if(!loadAudio(inputPath, audio, numFrames, sampleRate)) {
        std::cerr << "Error: failed to load audio file: " << inputPath << "\n";
        return EXIT_FAILURE;
    }
    if(sampleRate <= 0) {
        std::cerr << "Error: invalid sampleRate after loading (" << sampleRate << ").\n";
        return EXIT_FAILURE;
    }
    if(audio.empty() || numFrames <= 0) {
        std::cerr << "Error: decoded audio is empty.\n";
        return EXIT_FAILURE;
    }
    if(numFrames != static_cast<int>(audio.size())) {
        std::cerr << "Error: inconsistent frame metadata (numFrames != audio.size()).\n";
        return EXIT_FAILURE;
    }

    const std::vector<Vortex::Onset> onsets = Vortex::DetectOnsetsFFT(audio, numFrames, sampleRate);
    if(onsets.empty()) {
        std::cerr << "Error: no onset detected.\n";
        return EXIT_FAILURE;
    }
    if(onsets.size() < 2u) {
        std::cerr << "Error: onset count is too low for tempo estimation.\n";
        return EXIT_FAILURE;
    }

    const std::vector<Vortex::TempoResult> rawTempos = TempoEstimatorB::estimateTempo(onsets, sampleRate);
    if(rawTempos.empty()) {
        std::cerr << "Error: no tempo candidates.\n";
        return EXIT_FAILURE;
    }
    std::vector<Vortex::TempoResult> tempos;
    tempos.reserve(rawTempos.size());
    for(const Vortex::TempoResult& tempo : rawTempos) {
        const bool validBpm = std::isfinite(tempo.bpm) && tempo.bpm > 0.0f;
        const bool validFitness = std::isfinite(tempo.fitness);
        if(validBpm && validFitness) {
            tempos.push_back(tempo);
        }
    }
    if(tempos.empty()) {
        std::cerr << "Error: all tempo candidates are invalid (non-finite or non-positive).\n";
        return EXIT_FAILURE;
    }
    std::stable_sort(
        tempos.begin(),
        tempos.end(),
        [](const Vortex::TempoResult& left, const Vortex::TempoResult& right) {
            return left.fitness > right.fitness;
        });

    const float offsetMs = Vortex::CalculateOffset(tempos, onsets, sampleRate);
    if(!std::isfinite(offsetMs) || offsetMs < 0.0f) {
        std::cerr << "Error: invalid offset value.\n";
        return EXIT_FAILURE;
    }
    const float beatMs = 60000.0f / tempos.front().bpm;
    if(std::isfinite(beatMs) && beatMs > 0.0f && offsetMs >= beatMs) {
        std::cerr << "Error: offset is out of normalized beat range.\n";
        return EXIT_FAILURE;
    }

    std::cout << "BPM: " << tempos.front().bpm << "\n";
    std::cout << "Offset(ms): " << offsetMs << "\n";
    return EXIT_SUCCESS;
}
