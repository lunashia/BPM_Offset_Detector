#include <cstdlib>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "OffsetDetector.h"
#include "OnsetDetectorFFT.h"
#include "load_audio.h"
#include "tempo_estimator_b.h"

// Legacy example retained intentionally for compatibility/reference.
// New release entrypoint is examples/main.cpp.
int main(int argc, char** argv)
{
    const std::string inputPath = (argc >= 2) ? argv[1] : "test.wav";

    std::vector<float> audio;
    int numFrames = 0;
    int sampleRate = 0;
    if(!loadAudio(inputPath, audio, numFrames, sampleRate)) {
        std::cerr << "Error: failed to load audio file: " << inputPath << "\n";
        return EXIT_FAILURE;
    }

    const std::vector<Vortex::Onset> onsets = Vortex::DetectOnsetsFFT(audio, numFrames, sampleRate);
    if(onsets.size() < 2u) {
        std::cerr << "Error: onset count is too low for tempo estimation.\n";
        return EXIT_FAILURE;
    }

    const std::vector<Vortex::TempoResult> tempos = TempoEstimatorB::estimateTempo(onsets, sampleRate);
    if(tempos.empty()) {
        std::cerr << "Error: no tempo candidates.\n";
        return EXIT_FAILURE;
    }

    std::vector<Vortex::TempoResult> rankedTempos = tempos;
    std::stable_sort(
        rankedTempos.begin(),
        rankedTempos.end(),
        [](const Vortex::TempoResult& left, const Vortex::TempoResult& right) {
            return left.fitness > right.fitness;
        });

    const size_t topK = std::min<size_t>(3, rankedTempos.size());
    std::cout << std::fixed << std::setprecision(3);
    for(size_t i = 0; i < topK; ++i) {
        std::vector<Vortex::TempoResult> singleTempo{rankedTempos[i]};
        const float offsetMs = Vortex::CalculateOffset(singleTempo, onsets, sampleRate);
        std::cout << "Rank " << (i + 1)
                  << ": tempo=" << rankedTempos[i].bpm << " bpm"
                  << ", offset=" << offsetMs << " ms"
                  << ", fitness=" << rankedTempos[i].fitness
                  << "\n";
    }
    return EXIT_SUCCESS;
}
