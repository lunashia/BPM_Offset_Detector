#include "OffsetDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "Polyfit.h"

namespace Vortex {
namespace {

// WHAT:
//   Round float to nearest int for sample-interval conversion.
static int roundToInt(float v)
{
    return static_cast<int>(std::floor(v + 0.5f));
}

// WHAT:
//   Select the highest-confidence tempo candidate.
// WHY:
//   Offset phase should be estimated against the strongest tempo hypothesis only.
static float pickBestBPM(const std::vector<TempoResult>& tempos)
{
    if(tempos.empty()) return 0.0f;

    size_t best = 0;
    for(size_t i = 1; i < tempos.size(); ++i) {
        if(tempos[i].fitness > tempos[best].fitness) best = i;
    }

    return tempos[best].bpm;
}

// WHAT:
//   Convert BPM to beat period in samples.
// ASSUMPTION:
//   sampleRate is in Hz and bpm > 0.
static int bpmToIntervalSamples(float bpm, int sampleRate)
{
    if(bpm <= 0.0f || sampleRate <= 0) return 0;
    return roundToInt((60.0f * static_cast<float>(sampleRate)) / bpm);
}

// WHAT:
//   Build beat-phase histogram in sample domain.
// WHY:
//   Peak phase indicates where beat grid best aligns with onset support.
static void buildPhaseHistogram(
    const std::vector<Onset>& onsets,
    int intervalSamples,
    std::vector<float>& histogram)
{
    histogram.assign(static_cast<size_t>(intervalSamples), 0.0f);

    // Strength gating: keep only strong onsets to reduce phase bias from weak/noisy events.
    std::vector<float> strengths;
    strengths.reserve(onsets.size());
    for(size_t i = 0; i < onsets.size(); ++i) {
        const float s = (onsets[i].strength > 0.0f) ? onsets[i].strength : 1.0f;
        strengths.push_back(s);
    }

    std::sort(strengths.begin(), strengths.end());
    const size_t qIndex = (strengths.size() * 8u) / 10u; // ~80th percentile
    const float gate = strengths.empty() ? 0.0f : strengths[std::min(qIndex, strengths.size() - 1u)];

    for(size_t i = 0; i < onsets.size(); ++i) {
        int phase = onsets[i].pos % intervalSamples;
        if(phase < 0) phase += intervalSamples;

        float s = onsets[i].strength;
        if(s <= 0.0f) s = 1.0f;
        if(s < gate) continue;

        histogram[static_cast<size_t>(phase)] += s;
    }
}

// WHAT:
//   Locate the strongest phase bin with optional offbeat reinforcement.
// WHY:
//   Some styles emphasize 8th-note structure; half-beat support stabilizes selection.
static int findBestPhaseIndex(const std::vector<float>& histogram)
{
    if(histogram.empty()) return 0;

    float bestScore = -std::numeric_limits<float>::infinity();
    int bestIndex = 0;

    const int n = static_cast<int>(histogram.size());
    const int half = n / 2;
    for(int i = 0; i < n; ++i) {
        const float mainSupport = histogram[static_cast<size_t>(i)];
        const float offSupport = histogram[static_cast<size_t>((i + half) % n)];
        const float score = mainSupport + 0.5f * offSupport;

        if(score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }

    return bestIndex;
}

// WHAT:
//   Sub-bin peak refinement via local quadratic interpolation around discrete peak.
// WHY:
//   Reduces phase quantization error while staying robust on noisy local shapes.
static float refinePhaseWithPolyfit(const std::vector<float>& histogram, int peakIndex)
{
    const int n = static_cast<int>(histogram.size());
    if(n <= 3) return static_cast<float>(peakIndex);

    const int left = (peakIndex - 1 + n) % n;
    const int right = (peakIndex + 1) % n;

    const double yl = static_cast<double>(histogram[static_cast<size_t>(left)]);
    const double yc = static_cast<double>(histogram[static_cast<size_t>(peakIndex)]);
    const double yr = static_cast<double>(histogram[static_cast<size_t>(right)]);

    const double denom = (yl - 2.0 * yc + yr);
    if(std::fabs(denom) < 1e-12) return static_cast<float>(peakIndex);

    double delta = 0.5 * (yl - yr) / denom;
    if(delta < -0.5) delta = -0.5;
    if(delta > 0.5) delta = 0.5;

    float refined = static_cast<float>(static_cast<double>(peakIndex) + delta);
    while(refined < 0.0f) refined += static_cast<float>(n);
    while(refined >= static_cast<float>(n)) refined -= static_cast<float>(n);
    return refined;
}

// WHAT:
//   Score how well a candidate offset aligns beat positions with observed onsets.
// WHY:
//   Used to resolve beat vs offbeat ambiguity by direct support comparison.
static float scoreOffsetSupport(
    const std::vector<Onset>& onsets,
    int sampleRate,
    float bpm,
    float offsetSec)
{
    if(onsets.empty() || sampleRate <= 0 || bpm <= 0.0f) return 0.0f;

    const float beatSec = 60.0f / bpm;
    if(beatSec <= 0.0f) return 0.0f;

    float score = 0.0f;
    const float sigma = 0.125f * beatSec;
    const float denom = 2.0f * sigma * sigma;
    for(size_t i = 0; i < onsets.size(); ++i) {
        const float t = static_cast<float>(onsets[i].pos) / static_cast<float>(sampleRate);
        float phase = std::fmod(t - offsetSec, beatSec);
        if(phase < 0.0f) phase += beatSec;

        const float dist = std::min(phase, beatSec - phase);
        const float w = std::exp(-(dist * dist) / denom);

        float s = onsets[i].strength;
        if(s <= 0.0f) s = 1.0f;

        score += s * w;
    }

    return score;
}

// WHAT:
//   Normalize offset into one beat cycle [0, beatSec).
// WHY:
//   Output contract uses phase offset relative to audio start, modulo one beat.
static float normalizeToBeat(float offsetSec, float beatSec)
{
    if(beatSec <= 0.0f) return 0.0f;
    float v = std::fmod(offsetSec, beatSec);
    if(v < 0.0f) v += beatSec;
    return v;
}

} // anonymous namespace

/*
 * WHAT:
 *   Estimate beat offset in milliseconds using tempo + onset evidence.
 *
 * INPUT / OUTPUT:
 *   - tempos: tempo candidates with fitness.
 *   - onsets: onset events (onset.pos is sample index).
 *   - sampleRate: Hz.
 *   - return: offset in ms, normalized to [0, beat_ms).
 *
 * ALGORITHM (high-level):
 *   1) Pick top-fitness BPM.
 *   2) Build phase histogram over one beat and pick peak.
 *   3) Refine peak with quadratic fit.
 *   4) Compare beat/offbeat support and choose better alignment.
 *
 * ASSUMPTIONS:
 *   - Offset is relative to audio start t=0 (not an external grid error).
 *   - onset.pos, sampleRate share the same time base.
 *
 * ERROR SOURCES:
 *   - Wrong top BPM yields wrong phase period.
 *   - Sparse/weak onsets reduce histogram reliability.
 *   - Highly syncopated rhythms may bias offbeat decision.
 */
float CalculateOffset(
    const std::vector<TempoResult>& tempos,
    const std::vector<Onset>& onsets,
    int sampleRate)
{
    if(sampleRate <= 0) {
        // TODO: align with project-wide error handling (log/throw/default value).
        return 0.0f;
    }

    if(tempos.empty()) {
        return 0.0f;
    }

    // Keep behavior close to FindTempo: offset requires meaningful onset evidence.
    if(onsets.size() < 2u) {
        return 0.0f;
    }

    const float bpm = pickBestBPM(tempos);
    if(bpm <= 0.0f) return 0.0f;

    const int intervalSamples = bpmToIntervalSamples(bpm, sampleRate);
    if(intervalSamples <= 0) return 0.0f;

    std::vector<float> histogram;
    buildPhaseHistogram(onsets, intervalSamples, histogram);

    const int discretePeak = findBestPhaseIndex(histogram);
    const float refinedPeak = refinePhaseWithPolyfit(histogram, discretePeak);

    float offsetSec = refinedPeak / static_cast<float>(sampleRate);
    const float beatSec = 60.0f / bpm;

    const float quarterBeatSec = 0.25f * beatSec;
    float bestOffsetSec = normalizeToBeat(offsetSec, beatSec);
    float bestScore = -std::numeric_limits<float>::infinity();
    for(int k = 0; k < 4; ++k) {
        const float candidate = normalizeToBeat(offsetSec + static_cast<float>(k) * quarterBeatSec, beatSec);
        const float score = scoreOffsetSupport(onsets, sampleRate, bpm, candidate);
        if(score > bestScore) {
            bestScore = score;
            bestOffsetSec = candidate;
        }
    }

    offsetSec = bestOffsetSec;
    offsetSec = normalizeToBeat(offsetSec, beatSec);

    // Output unit: milliseconds.
    return offsetSec * 1000.0f;
}

} // namespace Vortex
