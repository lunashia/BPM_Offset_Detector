#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "OffsetDetector.h"
#include "OnsetDetectorFFT.h"
#include "load_audio.h"
#include "tempo_estimator_b.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#ifdef _MSC_VER
#pragma comment(lib, "comdlg32.lib")
#endif
#endif

/*
 * WHAT:
 *   Minimal release example for stable pipeline:
 *   loadAudio -> DetectOnsetsFFT -> estimateTempo(B) -> CalculateOffset.
 *
 * WHY:
 *   Demonstrates the intended production integration path with explicit unit flow:
 *   samples -> BPM -> offset(ms).
 *
 * ENTRY MODES:
 *   - CLI:           example.exe <audio path>   (back-compat for scripts/tests)
 *   - Double-click:  launched without argv[1] on Windows opens a file picker
 *                    and keeps the console open so the user can read results.
 */

namespace {

struct BpmPreset {
    const char* label;
    float minBpm;
    float maxBpm;
};

TempoEstimatorB::BpmRange selectBpmRangePreset()
{
    static const BpmPreset kPresets[] = {
        {"70-180 (default)", 70.0f, 180.0f},
        {"48-95", 48.0f, 95.0f},
        {"58-115", 58.0f, 115.0f},
        {"68-135", 68.0f, 135.0f},
        {"78-155", 78.0f, 155.0f},
        {"88-175", 88.0f, 175.0f},
        {"98-195", 98.0f, 195.0f},
        {"108-215", 108.0f, 215.0f},
        {"118-235", 118.0f, 235.0f},
        {"128-255", 128.0f, 255.0f}
    };

    std::cout << "Select BPM preset range:\n";
    for(size_t i = 0; i < (sizeof(kPresets) / sizeof(kPresets[0])); ++i) {
        std::cout << "  " << (i + 1) << ". " << kPresets[i].label << "\n";
    }
    std::cout << "Choice [1-10, Enter=1]: " << std::flush;

    std::string line;
    if(!std::getline(std::cin, line) || line.empty()) {
        return {kPresets[0].minBpm, kPresets[0].maxBpm};
    }

    try {
        const int choice = std::stoi(line);
        if(choice >= 1 && choice <= 10) {
            const BpmPreset& preset = kPresets[static_cast<size_t>(choice - 1)];
            return {preset.minBpm, preset.maxBpm};
        }
    } catch(...) {
    }

    std::cout << "Invalid selection, fallback to default 70-180.\n";
    return {kPresets[0].minBpm, kPresets[0].maxBpm};
}

bool fileExists(const std::string& path)
{
    std::ifstream stream(path.c_str(), std::ios::binary);
    return stream.good();
}

#ifdef _WIN32
// WHAT:
//   Open a Win32 "open file" dialog and return the chosen path.
// WHY:
//   Users who double-click the .exe need a GUI way to point at audio input.
//   Returns empty string if the user cancels or on any failure.
std::string pickAudioFileDialog()
{
    char fileBuffer[MAX_PATH] = {0};

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = fileBuffer;
    ofn.nMaxFile = sizeof(fileBuffer);
    // Filter format: pairs of "label\0pattern\0", terminated by an extra \0.
    ofn.lpstrFilter =
        "Audio files (*.wav;*.mp3;*.flac;*.ogg)\0*.wav;*.mp3;*.flac;*.ogg\0"
        "WAV (*.wav)\0*.wav\0"
        "MP3 (*.mp3)\0*.mp3\0"
        "FLAC (*.flac)\0*.flac\0"
        "OGG (*.ogg)\0*.ogg\0"
        "All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrTitle = "Select an audio file";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;

    if(GetOpenFileNameA(&ofn)) {
        return std::string(fileBuffer);
    }
    return std::string();
}

// WHY:
//   When the program is started by double-click the console window closes as
//   soon as main returns, hiding the BPM/offset result. Pause before exit.
void waitForUserBeforeExit()
{
    std::cout << "\nPress Enter to exit..." << std::flush;
    std::cin.get();
}
#endif

int runPipeline(const std::string& inputPath)
{
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

    const TempoEstimatorB::BpmRange bpmRange = selectBpmRangePreset();
    const std::vector<Vortex::TempoResult> rawTempos = TempoEstimatorB::estimateTempo(onsets, sampleRate, bpmRange);
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

    std::cout << "Input: " << inputPath << "\n";

    // WHY:
    //   Fixed 3-decimal formatting keeps ranks visually aligned and matches the
    //   reference report style (e.g. "fitness=1.000" rather than "fitness=1").
    std::cout << std::fixed << std::setprecision(3);

    // WHY:
    //   Surface top-3 tempo hypotheses with their per-candidate offsets so users
    //   can spot half/double ambiguity when rank-1 is not the perceived tempo.
    const size_t ranksToShow = std::min<size_t>(tempos.size(), 3u);
    for(size_t i = 0; i < ranksToShow; ++i) {
        const Vortex::TempoResult& candidate = tempos[i];

        // Call CalculateOffset with a single-element vector so the offset is
        // computed against this specific BPM rather than always the top-fitness one.
        const std::vector<Vortex::TempoResult> singleCandidate(1, candidate);
        const float offsetMs = Vortex::CalculateOffset(singleCandidate, onsets, sampleRate);

        float displayedOffsetMs = offsetMs;
        const float beatMs = 60000.0f / candidate.bpm;
        if(!std::isfinite(displayedOffsetMs) || displayedOffsetMs < 0.0f) {
            displayedOffsetMs = 0.0f;
        } else if(std::isfinite(beatMs) && beatMs > 0.0f && displayedOffsetMs >= beatMs) {
            displayedOffsetMs = std::fmod(displayedOffsetMs, beatMs);
        }

        std::cout << "Rank " << (i + 1)
                  << ": tempo=" << candidate.bpm
                  << " bpm, offset=" << displayedOffsetMs
                  << " ms, fitness=" << candidate.fitness
                  << "\n";
    }
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
    const bool launchedWithoutArg = (argc < 2);
    std::string inputPath;

    if(argc >= 2) {
        inputPath = argv[1];
    } else {
#ifdef _WIN32
        // Double-click path: present a file picker instead of silent failure.
        inputPath = pickAudioFileDialog();
#else
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
#endif
    }

    int rc = EXIT_FAILURE;
    if(inputPath.empty()) {
        std::cerr << "Error: no audio file selected.\n";
    } else {
        rc = runPipeline(inputPath);
    }

#ifdef _WIN32
    if(launchedWithoutArg) {
        waitForUserBeforeExit();
    }
#endif
    return rc;
}
