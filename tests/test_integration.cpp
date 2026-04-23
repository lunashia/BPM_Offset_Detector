#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "OffsetDetector.h"
#include "OnsetDetectorFFT.h"
#include "load_audio.h"
#include "tempo_estimator_b.h"

int main()
{
    const char* envPath = std::getenv("BPM_TEST_AUDIO");
    const std::string audioPath = (envPath != nullptr) ? std::string(envPath) : std::string("../test.wav");

    std::vector<float> audio;
    int numFrames = 0;
    int sampleRate = 0;
    if(!loadAudio(audioPath, audio, numFrames, sampleRate)) {
        std::cerr << "[FAIL] Integration: cannot load audio: " << audioPath << "\n";
        return 1;
    }

    const std::vector<Vortex::Onset> onsets = Vortex::DetectOnsetsFFT(audio, numFrames, sampleRate);
    if(onsets.size() < 2u) {
        std::cerr << "[FAIL] Integration: too few onsets.\n";
        return 1;
    }

    const std::vector<Vortex::TempoResult> tempos = TempoEstimatorB::estimateTempo(onsets, sampleRate);
    if(tempos.empty()) {
        std::cerr << "[FAIL] Integration: no tempo candidates.\n";
        return 1;
    }

    const float offsetMs = Vortex::CalculateOffset(tempos, onsets, sampleRate);
    const float beatMs = 60000.0f / tempos.front().bpm;
    if(!(offsetMs >= 0.0f && offsetMs < beatMs)) {
        std::cerr << "[FAIL] Integration: offset out of normalized beat range.\n";
        return 1;
    }

    std::cout << "[PASS] Integration\n";
    std::cout << "audio=" << audioPath
              << " onsets=" << onsets.size()
              << " bpm=" << tempos.front().bpm
              << " offset_ms=" << offsetMs << "\n";
    return 0;
}
