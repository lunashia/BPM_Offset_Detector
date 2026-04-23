#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "OffsetDetector.h"
#include "OnsetDetectorFFT.h"
#include "tempo_estimator_b.h"

namespace {

bool check(bool condition, const std::string& message)
{
    if(!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

std::vector<float> makeImpulseAudio(int sampleRate, float bpm, int beats, float amplitude = 0.95f)
{
    const int samplesPerBeat = static_cast<int>(std::round((60.0f * static_cast<float>(sampleRate)) / bpm));
    const int totalSamples = (beats + 1) * samplesPerBeat;
    std::vector<float> audio(static_cast<size_t>(totalSamples), 0.0f);

    for(int beat = 0; beat < beats; ++beat) {
        const int center = beat * samplesPerBeat;
        if(center >= 0 && center < totalSamples) audio[static_cast<size_t>(center)] = amplitude;
        if(center + 1 >= 0 && center + 1 < totalSamples) audio[static_cast<size_t>(center + 1)] = amplitude * 0.5f;
    }
    return audio;
}

std::vector<Vortex::Onset> makeRegularOnsets(int sampleRate, float bpm, int beats, int startSample = 0)
{
    std::vector<Vortex::Onset> onsets;
    const float samplesPerBeat = 60.0f * static_cast<float>(sampleRate) / bpm;
    for(int i = 0; i < beats; ++i) {
        Vortex::Onset onset{};
        onset.pos = startSample + static_cast<int>(std::round(samplesPerBeat * static_cast<float>(i)));
        onset.strength = 1.0f;
        onsets.push_back(onset);
    }
    return onsets;
}

bool testOnsetDetector()
{
    const int sampleRate = 44100;
    const float bpm = 120.0f;
    const int beats = 16;
    const std::vector<float> audio = makeImpulseAudio(sampleRate, bpm, beats);

    const std::vector<Vortex::Onset> onsets = Vortex::DetectOnsetsFFT(audio, static_cast<int>(audio.size()), sampleRate);
    if(!check(!onsets.empty(), "Onset detector should find onsets on impulse track.")) return false;
    if(!check(onsets.size() >= 8u, "Onset detector should find enough events on clean impulses.")) return false;

    for(const Vortex::Onset& onset : onsets) {
        if(!check(onset.pos >= 0, "Onset sample index must be non-negative.")) return false;
    }
    return true;
}

bool testTempoEstimator()
{
    const int sampleRate = 44100;
    const float targetBpm = 128.0f;
    const std::vector<Vortex::Onset> onsets = makeRegularOnsets(sampleRate, targetBpm, 20);

    const std::vector<Vortex::TempoResult> tempos = TempoEstimatorB::estimateTempo(onsets, sampleRate);
    if(!check(!tempos.empty(), "Tempo estimator should return candidates for regular onsets.")) return false;

    const float error = std::fabs(tempos.front().bpm - targetBpm);
    if(!check(error <= 2.0f, "Top BPM should be close to synthetic target (<=2 BPM).")) return false;
    if(!check(tempos.front().fitness >= 0.0f && tempos.front().fitness <= 1.0f, "Fitness should be normalized to [0,1].")) return false;
    return true;
}

bool testOffsetEstimator()
{
    const int sampleRate = 44100;
    const float bpm = 120.0f;
    const int phaseOffsetSamples = static_cast<int>(std::round(0.100f * static_cast<float>(sampleRate))); // 100ms
    const std::vector<Vortex::Onset> onsets = makeRegularOnsets(sampleRate, bpm, 16, phaseOffsetSamples);

    std::vector<Vortex::TempoResult> tempos;
    tempos.push_back({bpm, 1.0f});

    const float offsetMs = Vortex::CalculateOffset(tempos, onsets, sampleRate);
    const float beatMs = 60000.0f / bpm;

    if(!check(offsetMs >= 0.0f && offsetMs < beatMs, "Offset must be normalized into [0, beat_ms).")) return false;
    if(!check(std::fabs(offsetMs - 100.0f) <= 25.0f, "Offset should be close to injected 100ms phase (<=25ms).")) return false;
    return true;
}

} // namespace

int main()
{
    try {
        bool ok = true;
        ok = testOnsetDetector() && ok;
        ok = testTempoEstimator() && ok;
        ok = testOffsetEstimator() && ok;

        if(ok) {
            std::cout << "[PASS] Unit tests\n";
            return 0;
        }
        return 1;
    } catch(const std::exception& e) {
        std::cerr << "[EXCEPTION] " << e.what() << "\n";
        return 1;
    } catch(...) {
        std::cerr << "[EXCEPTION] Unknown error\n";
        return 1;
    }
}
