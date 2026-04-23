#include "OnsetDetectorFFT.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Vortex {
extern void rdft(int n, int isgn, float* a, int* ip, float* w);
}

namespace Vortex {
namespace {

// WHAT:
//   Utility clamp for local-window indexing.
// WHY:
//   Keeps window-based statistics robust near boundaries without branching explosion.
static int clampInt(int value, int low, int high)
{
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

// WHAT:
//   Normalize phase into [-pi, pi].
// WHY:
//   Phase-deviation novelty depends on wrapped phase error, not raw angle drift.
static float wrapPhase(float phase)
{
    static const float kPi = 3.14159265358979323846f;
    while(phase > kPi) phase -= 2.0f * kPi;
    while(phase < -kPi) phase += 2.0f * kPi;
    return phase;
}

// WHAT:
//   Build Hann analysis window.
// WHY:
//   Reduces spectral leakage so novelty features are less sensitive to frame cuts.
static void buildHannWindow(int frameSize, std::vector<float>& window)
{
    window.resize(static_cast<size_t>(frameSize), 0.0f);
    if(frameSize <= 1) {
        if(frameSize == 1) window[0] = 1.0f;
        return;
    }

    const float denom = static_cast<float>(frameSize - 1);
    for(int i = 0; i < frameSize; ++i) {
        const float phase = (2.0f * 3.14159265358979323846f * static_cast<float>(i)) / denom;
        window[static_cast<size_t>(i)] = 0.5f - 0.5f * std::cos(phase);
    }
}

// WHAT:
//   Compute magnitude and phase spectrum from one windowed frame.
// WHY:
//   Flux/HFC/phase-deviation each need different spectral views from the same FFT.
static void computeSpectrum(
    const std::vector<float>& frameWindowed,
    std::vector<float>& magnitudes,
    std::vector<float>& phases)
{
    const int n = static_cast<int>(frameWindowed.size());
    const int half = n / 2;

    std::vector<float> packed = frameWindowed;
    std::vector<int> ip(static_cast<size_t>(2 + std::sqrt(static_cast<float>(n / 2))) + 8u, 0);
    std::vector<float> w(static_cast<size_t>(n / 2) + 8u, 0.0f);

    rdft(n, 1, packed.data(), ip.data(), w.data());

    magnitudes.assign(static_cast<size_t>(half + 1), 0.0f);
    phases.assign(static_cast<size_t>(half + 1), 0.0f);

    magnitudes[0] = std::abs(packed[0]);
    phases[0] = 0.0f;
    magnitudes[half] = std::abs(packed[1]);
    phases[half] = 0.0f;

    for(int k = 1; k < half; ++k) {
        const float real = packed[2 * k];
        const float imag = packed[2 * k + 1];
        magnitudes[static_cast<size_t>(k)] = std::sqrt(real * real + imag * imag);
        phases[static_cast<size_t>(k)] = std::atan2(imag, real);
    }
}

// WHAT:
//   Positive spectral flux novelty.
// WHY:
//   Onsets are characterized by sudden spectral energy rise, so negative changes
//   are suppressed to reduce decay-tail false positives.
static float computeSpectralFlux(const std::vector<float>& prevMag, const std::vector<float>& curMag)
{
    float flux = 0.0f;
    const int size = static_cast<int>(curMag.size());
    for(int i = 0; i < size; ++i) {
        const float delta = curMag[static_cast<size_t>(i)] - prevMag[static_cast<size_t>(i)];
        if(delta > 0.0f) flux += delta;
    }
    return flux;
}

// WHAT:
//   High-Frequency Content novelty.
// WHY:
//   Percussive attacks often emphasize high bands; this complements flux in dense mixes.
static float computeHfc(const std::vector<float>& magnitudes)
{
    float hfc = 0.0f;
    const int size = static_cast<int>(magnitudes.size());
    for(int k = 1; k < size; ++k) {
        hfc += static_cast<float>(k) * magnitudes[static_cast<size_t>(k)];
    }
    return hfc;
}

// WHAT:
//   Phase-deviation novelty weighted by current magnitude.
// WHY:
//   Abrupt events disturb short-term phase prediction; this catches attacks that
//   may have weak amplitude increase but clear phase discontinuity.
static float computePhaseDeviation(
    const std::vector<float>& prevPrevPhase,
    const std::vector<float>& prevPhase,
    const std::vector<float>& curPhase,
    const std::vector<float>& curMag)
{
    float score = 0.0f;
    const int size = static_cast<int>(curPhase.size());
    for(int k = 1; k < size - 1; ++k) {
        const float predicted = 2.0f * prevPhase[static_cast<size_t>(k)] - prevPrevPhase[static_cast<size_t>(k)];
        const float deviation = std::fabs(wrapPhase(curPhase[static_cast<size_t>(k)] - predicted));
        score += curMag[static_cast<size_t>(k)] * deviation;
    }
    return score;
}

// WHAT:
//   Lightweight moving-average smoothing.
// WHY:
//   Suppresses frame-to-frame jitter before peak picking.
static std::vector<float> smoothMovingAverage(const std::vector<float>& input, int radius)
{
    std::vector<float> output(input.size(), 0.0f);
    if(input.empty()) return output;

    for(size_t i = 0; i < input.size(); ++i) {
        const int begin = std::max(0, static_cast<int>(i) - radius);
        const int end = std::min(static_cast<int>(input.size()) - 1, static_cast<int>(i) + radius);
        float sum = 0.0f;
        for(int j = begin; j <= end; ++j) {
            sum += input[static_cast<size_t>(j)];
        }
        output[i] = sum / static_cast<float>(end - begin + 1);
    }

    return output;
}

// WHAT:
//   Min-max normalization to [0,1].
// WHY:
//   Feature fusion needs comparable scales; otherwise one feature dominates by magnitude alone.
static void normalizeMinMax(std::vector<float>& values)
{
    if(values.empty()) return;

    float minValue = values[0];
    float maxValue = values[0];
    for(size_t i = 1; i < values.size(); ++i) {
        minValue = std::min(minValue, values[i]);
        maxValue = std::max(maxValue, values[i]);
    }

    const float range = maxValue - minValue;
    if(range < 1e-12f) {
        std::fill(values.begin(), values.end(), 0.0f);
        return;
    }

    for(float& value : values) {
        value = (value - minValue) / range;
    }
}

static float localMean(const std::vector<float>& values, int center, int radius)
{
    const int n = static_cast<int>(values.size());
    const int begin = clampInt(center - radius, 0, n - 1);
    const int end = clampInt(center + radius, 0, n - 1);

    float sum = 0.0f;
    for(int i = begin; i <= end; ++i) {
        sum += values[static_cast<size_t>(i)];
    }
    return sum / static_cast<float>(end - begin + 1);
}

static float localStd(const std::vector<float>& values, int center, int radius, float mean)
{
    const int n = static_cast<int>(values.size());
    const int begin = clampInt(center - radius, 0, n - 1);
    const int end = clampInt(center + radius, 0, n - 1);

    float sumSquared = 0.0f;
    for(int i = begin; i <= end; ++i) {
        const float d = values[static_cast<size_t>(i)] - mean;
        sumSquared += d * d;
    }
    const float variance = sumSquared / static_cast<float>(end - begin + 1);
    return std::sqrt(std::max(0.0f, variance));
}

// WHAT:
//   Adaptive local-threshold peak picker with refractory period.
// WHY:
//   Local mean/std threshold handles loudness variation across tracks better than
//   global thresholds, while minInterOnsetSamples avoids duplicate peaks per hit.
//
// ASSUMPTION:
//   novelty index i maps to sample i*hopSize in the original timeline.
static void peakPick(
    const std::vector<float>& novelty,
    int hopSize,
    int minInterOnsetSamples,
    std::vector<Onset>& onsets)
{
    const int n = static_cast<int>(novelty.size());
    if(n < 3) return;

    const int thresholdRadius = 10;
    const float alpha = 0.6f;
    const float beta = 0.02f;
    int lastOnsetPos = -minInterOnsetSamples;

    for(int i = 1; i < n - 1; ++i) {
        const float mean = localMean(novelty, i, thresholdRadius);
        const float stdValue = localStd(novelty, i, thresholdRadius, mean);
        const float threshold = mean + alpha * stdValue + beta;

        const bool isPeak = novelty[static_cast<size_t>(i)] > novelty[static_cast<size_t>(i - 1)] &&
                            novelty[static_cast<size_t>(i)] >= novelty[static_cast<size_t>(i + 1)];
        if(!isPeak) continue;
        if(novelty[static_cast<size_t>(i)] <= threshold) continue;

        const int pos = i * hopSize;
        if(pos - lastOnsetPos < minInterOnsetSamples) continue;

        Onset onset{};
        onset.pos = pos;
        onset.strength = novelty[static_cast<size_t>(i)];
        onsets.push_back(onset);
        lastOnsetPos = pos;
    }
}

} // anonymous namespace

/*
 * WHAT:
 *   Detect onset events from mono PCM.
 *
 * INPUT / OUTPUT:
 *   - audio: mono PCM samples.
 *   - numFrames: PCM frame count (expected to match audio.size()).
 *   - sampleRate: Hz.
 *   - return: onset list where onset.pos is sample index.
 *
 * ALGORITHM (high-level):
 *   1) STFT with Hann window.
 *   2) Build three novelty streams: spectral flux, HFC, phase deviation.
 *   3) Normalize and fuse weighted novelty; smooth and peak-pick.
 *
 * ASSUMPTIONS:
 *   - onset.pos is sample index (not milliseconds, not STFT frame index).
 *   - input is mono and sample-aligned to sampleRate.
 *
 * ERROR SOURCES:
 *   - Weak/legato music can produce sparse onsets.
 *   - Very dense transients can merge under refractory constraint.
 *   - Fixed frame/hop introduce time quantization before peak refinement.
 */
std::vector<Onset> DetectOnsetsFFT(const std::vector<float>& audio, int numFrames, int sampleRate)
{
    std::vector<Onset> onsets;

    // ASSUMPTION:
    //   Power-of-two frame size for rdft; hop defines temporal resolution.
    const int frameSize = 1024;
    const int hopSize = 256;
    if(audio.empty() || numFrames <= 0 || sampleRate <= 0) return onsets;

    const int validFrames = std::min(numFrames, static_cast<int>(audio.size()));
    if(validFrames < frameSize) return onsets;

    // WHY:
    //   Ignore detections closer than 30 ms to avoid multiple peaks per single hit.
    const int minInterOnsetSamples = static_cast<int>(0.03f * static_cast<float>(sampleRate));

    std::vector<float> window;
    buildHannWindow(frameSize, window);

    std::vector<float> prevMag(static_cast<size_t>(frameSize / 2 + 1), 0.0f);
    std::vector<float> prevPhase(static_cast<size_t>(frameSize / 2 + 1), 0.0f);
    std::vector<float> prevPrevPhase(static_cast<size_t>(frameSize / 2 + 1), 0.0f);

    std::vector<float> curMag;
    std::vector<float> curPhase;
    std::vector<float> frame(static_cast<size_t>(frameSize), 0.0f);

    std::vector<float> fluxNovelty;
    std::vector<float> hfcNovelty;
    std::vector<float> phaseNovelty;

    int frameIndex = 0;
    for(int pos = 0; pos <= validFrames - frameSize; pos += hopSize, ++frameIndex) {
        for(int i = 0; i < frameSize; ++i) {
            frame[static_cast<size_t>(i)] = audio[static_cast<size_t>(pos + i)] * window[static_cast<size_t>(i)];
        }

        computeSpectrum(frame, curMag, curPhase);

        if(frameIndex == 0) {
            fluxNovelty.push_back(0.0f);
            hfcNovelty.push_back(0.0f);
            phaseNovelty.push_back(0.0f);
        } else if(frameIndex == 1) {
            fluxNovelty.push_back(computeSpectralFlux(prevMag, curMag));
            hfcNovelty.push_back(computeHfc(curMag));
            phaseNovelty.push_back(0.0f);
        } else {
            fluxNovelty.push_back(computeSpectralFlux(prevMag, curMag));
            hfcNovelty.push_back(computeHfc(curMag));
            phaseNovelty.push_back(computePhaseDeviation(prevPrevPhase, prevPhase, curPhase, curMag));
        }

        prevPrevPhase = prevPhase;
        prevPhase = curPhase;
        prevMag = curMag;
    }

    normalizeMinMax(fluxNovelty);
    normalizeMinMax(hfcNovelty);
    normalizeMinMax(phaseNovelty);

    std::vector<float> fusedNovelty(fluxNovelty.size(), 0.0f);
    // WHY:
    //   Weighted fusion balances complementary onset cues:
    //   flux (broad attack), HFC (percussive brightness), phase discontinuity.
    const float wFlux = 0.45f;
    const float wHfc = 0.35f;
    const float wPhase = 0.20f;
    for(size_t i = 0; i < fusedNovelty.size(); ++i) {
        fusedNovelty[i] = wFlux * fluxNovelty[i] + wHfc * hfcNovelty[i] + wPhase * phaseNovelty[i];
    }

    fusedNovelty = smoothMovingAverage(fusedNovelty, 1);
    peakPick(fusedNovelty, hopSize, minInterOnsetSamples, onsets);
    return onsets;
}

} // namespace Vortex
