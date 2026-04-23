#include "tempo_estimator_b.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace TempoEstimatorB {
namespace {

// Stable search range assumption for production B estimator.
static const float kMinBPM = 80.0f;
static const float kMaxBPM = 200.0f;
static const float kPreferredMinBPM = 110.0f;
static const int kTopK = 3;
static const float kAutoCorrBinSec = 0.0005f;
static const int kSmoothRadius = 2;
static const float kMergeRatio = 0.10f;
static const float kLocalRefineRangeBPM = 3.0f;
static const float kLocalRefineStepBPM = 0.05f;

struct Candidate {
    float bpm;
    float rawFitness;
};

struct CandidateSort {
    bool operator()(const Candidate& left, const Candidate& right) const
    {
        return left.rawFitness > right.rawFitness;
    }
};

// WHAT:
//   Numerical helper for sample/bin conversions.
int roundToInt(float value)
{
    return static_cast<int>(std::floor(value + 0.5f));
}

// WHAT:
//   Guard for non-positive onset strengths.
// WHY:
//   Keeps weighting robust if upstream detector outputs weak/invalid strengths.
float safeStrength(float value)
{
    return (value > 0.0f) ? value : 1.0f;
}

// WHAT:
//   Sort onsets by sample index.
// ASSUMPTION:
//   Later steps assume non-decreasing temporal order.
std::vector<Vortex::Onset> sortedOnsets(const std::vector<Vortex::Onset>& onsets)
{
    std::vector<Vortex::Onset> sorted = onsets;
    std::sort(sorted.begin(), sorted.end(), [](const Vortex::Onset& left, const Vortex::Onset& right) {
        return left.pos < right.pos;
    });
    return sorted;
}

// WHAT:
//   Local moving-average smoothing.
// WHY:
//   Reduces autocorrelation jitter before peak picking.
std::vector<float> movingAverage(const std::vector<float>& values, int radius)
{
    if(values.empty() || radius <= 0) return values;

    std::vector<float> out(values.size(), 0.0f);
    for(size_t i = 0; i < values.size(); ++i) {
        const int begin = std::max(0, static_cast<int>(i) - radius);
        const int end = std::min(static_cast<int>(values.size()) - 1, static_cast<int>(i) + radius);

        float sum = 0.0f;
        for(int j = begin; j <= end; ++j) {
            sum += values[static_cast<size_t>(j)];
        }
        out[i] = sum / static_cast<float>(end - begin + 1);
    }
    return out;
}

/*
 * WHAT:
 *   Convert sparse onset events into a dense onset envelope.
 *
 * WHY:
 *   Autocorrelation over a dense envelope is simpler and more stable than directly
 *   correlating sparse event lists.
 *
 * ASSUMPTIONS:
 *   - onset.pos is sample index in the same timeline as sampleRate.
 *   - Envelope bin size is fixed at kAutoCorrBinSec (stable B contract).
 */
bool buildOnsetEnvelope(const std::vector<Vortex::Onset>& onsets, int sampleRate, std::vector<float>& envelope)
{
    envelope.clear();
    const int binSamples = roundToInt(kAutoCorrBinSec * static_cast<float>(sampleRate));
    if(sampleRate <= 0 || onsets.size() < 2u || binSamples <= 0) return false;

    int maxPos = 0;
    for(const Vortex::Onset& onset : onsets) {
        if(onset.pos > maxPos) maxPos = onset.pos;
    }
    if(maxPos <= 0) return false;

    const int numBins = (maxPos / binSamples) + 1;
    if(numBins <= 2) return false;

    envelope.assign(static_cast<size_t>(numBins), 0.0f);
    for(const Vortex::Onset& onset : onsets) {
        if(onset.pos < 0) continue;
        int index = onset.pos / binSamples;
        if(index >= numBins) index = numBins - 1;
        envelope[static_cast<size_t>(index)] += safeStrength(onset.strength);
    }
    return true;
}

/*
 * WHAT:
 *   Estimate tempo candidates from envelope autocorrelation peaks.
 *
 * ALGORITHM:
 *   - Compute normalized ACF over lag range mapped from BPM range.
 *   - Smooth ACF and apply harmonic enhancement (1/2x, 1/3x, 2x relations).
 *   - Detect local peaks and refine lag using quadratic + weighted neighborhood.
 *
 * WHY:
 *   Harmonic-aware ACF helps suppress octave/half-tempo ambiguity and gives robust
 *   candidate peaks under noisy onset sequences.
 *
 * ERROR SOURCES:
 *   - Structured syncopation can produce competing harmonic peaks.
 *   - Very short signals reduce ACF reliability at target lags.
 */
std::vector<Candidate> estimateByAutoCorrelation(const std::vector<float>& envelope, int lagMin, int lagMax)
{
    std::vector<Candidate> candidates;
    if(envelope.empty() || lagMin <= 0 || lagMin > lagMax) return candidates;

    const int maxLag = std::min(lagMax, static_cast<int>(envelope.size()) - 2);
    if(maxLag < lagMin) return candidates;

    std::vector<float> acf(static_cast<size_t>(maxLag + 1), 0.0f);
    for(int lag = lagMin; lag <= maxLag; ++lag) {
        float dot = 0.0f;
        float e0 = 0.0f;
        float e1 = 0.0f;
        const int count = static_cast<int>(envelope.size()) - lag;
        for(int i = 0; i < count; ++i) {
            const float a = envelope[static_cast<size_t>(i)];
            const float b = envelope[static_cast<size_t>(i + lag)];
            dot += a * b;
            e0 += a * a;
            e1 += b * b;
        }
        if(e0 > 1e-8f && e1 > 1e-8f) {
            acf[static_cast<size_t>(lag)] = dot / std::sqrt(e0 * e1);
        }
    }

    acf = movingAverage(acf, kSmoothRadius);

    std::vector<float> enhanced(static_cast<size_t>(maxLag + 1), 0.0f);
    for(int lag = lagMin; lag <= maxLag; ++lag) {
        float score = acf[static_cast<size_t>(lag)];
        if(lag / 2 >= lagMin) score += 0.60f * acf[static_cast<size_t>(lag / 2)];
        if(lag / 3 >= lagMin) score += 0.30f * acf[static_cast<size_t>(lag / 3)];
        if(lag * 2 <= maxLag) score += 0.50f * acf[static_cast<size_t>(lag * 2)];

        const float bpm = 60.0f / (static_cast<float>(lag) * kAutoCorrBinSec);
        const float tempoPrior = std::sqrt(std::max(0.0f, bpm / kMaxBPM));
        enhanced[static_cast<size_t>(lag)] = score * tempoPrior;
    }

    float maxScore = 0.0f;
    for(int lag = lagMin; lag <= maxLag; ++lag) {
        maxScore = std::max(maxScore, enhanced[static_cast<size_t>(lag)]);
    }
    if(maxScore <= 0.0f) return candidates;

    auto refineLagFromNeighborhood = [&](int lagCenter) {
        float lagQuadratic = static_cast<float>(lagCenter);
        if(lagCenter > lagMin && lagCenter < maxLag) {
            const float left = enhanced[static_cast<size_t>(lagCenter - 1)];
            const float mid = enhanced[static_cast<size_t>(lagCenter)];
            const float right = enhanced[static_cast<size_t>(lagCenter + 1)];
            const float denom = (left - 2.0f * mid + right);
            if(std::fabs(denom) > 1e-8f) {
                float delta = 0.5f * (left - right) / denom;
                delta = std::min(0.5f, std::max(-0.5f, delta));
                lagQuadratic += delta;
            }
        }

        float lagWeighted = 0.0f;
        float weightSum = 0.0f;
        for(int offset = -1; offset <= 1; ++offset) {
            const int lag = lagCenter + offset;
            if(lag < lagMin || lag > maxLag) continue;
            const float weight = std::max(0.0f, enhanced[static_cast<size_t>(lag)]);
            lagWeighted += weight * static_cast<float>(lag);
            weightSum += weight;
        }
        if(weightSum > 1e-8f) return 0.5f * (lagQuadratic + lagWeighted / weightSum);
        return lagQuadratic;
    };

    const float peakThreshold = 0.35f * maxScore;
    for(int lag = lagMin; lag <= maxLag; ++lag) {
        const float score = enhanced[static_cast<size_t>(lag)];
        if(score < peakThreshold) continue;
        const float left = (lag > lagMin) ? enhanced[static_cast<size_t>(lag - 1)] : -1.0f;
        const float right = (lag < maxLag) ? enhanced[static_cast<size_t>(lag + 1)] : -1.0f;
        if(score < left || score < right) continue;

        const float refinedLag = refineLagFromNeighborhood(lag);
        if(refinedLag <= 0.0f) continue;
        Candidate candidate{};
        candidate.bpm = 60.0f / (refinedLag * kAutoCorrBinSec);
        candidate.rawFitness = score;
        if(candidate.bpm >= kMinBPM && candidate.bpm <= kMaxBPM) {
            candidates.push_back(candidate);
        }
    }

    if(candidates.empty()) {
        int bestLag = lagMin;
        for(int lag = lagMin + 1; lag <= maxLag; ++lag) {
            if(enhanced[static_cast<size_t>(lag)] > enhanced[static_cast<size_t>(bestLag)]) bestLag = lag;
        }
        const float refinedLag = refineLagFromNeighborhood(bestLag);
        if(refinedLag > 0.0f) {
            Candidate candidate{};
            candidate.bpm = 60.0f / (refinedLag * kAutoCorrBinSec);
            candidate.rawFitness = enhanced[static_cast<size_t>(bestLag)];
            if(candidate.bpm >= kMinBPM && candidate.bpm <= kMaxBPM && candidate.rawFitness > 0.0f) {
                candidates.push_back(candidate);
            }
        }
    }

    return candidates;
}

// WHAT:
//   Merge near-duplicate BPM peaks.
// WHY:
//   Adjacent lags often represent the same tempo mode after interpolation.
std::vector<Candidate> mergeClosePeaks(std::vector<Candidate> peaks)
{
    if(peaks.empty()) return peaks;
    std::sort(peaks.begin(), peaks.end(), [](const Candidate& left, const Candidate& right) { return left.bpm < right.bpm; });

    struct Cluster {
        float weightedBpmSum;
        float weightSum;
    };

    std::vector<Cluster> clusters;
    for(const Candidate& peak : peaks) {
        bool attached = false;
        for(Cluster& cluster : clusters) {
            const float center = cluster.weightedBpmSum / cluster.weightSum;
            const float ratio = std::fabs(peak.bpm - center) / center;
            if(ratio < kMergeRatio) {
                cluster.weightedBpmSum += peak.bpm * peak.rawFitness;
                cluster.weightSum += peak.rawFitness;
                attached = true;
                break;
            }
        }
        if(!attached) {
            clusters.push_back({peak.bpm * peak.rawFitness, peak.rawFitness});
        }
    }

    std::vector<Candidate> merged;
    merged.reserve(clusters.size());
    for(const Cluster& cluster : clusters) {
        if(cluster.weightSum <= 0.0f) continue;
        Candidate candidate{};
        candidate.bpm = cluster.weightedBpmSum / cluster.weightSum;
        candidate.rawFitness = cluster.weightSum;
        merged.push_back(candidate);
    }
    return merged;
}

// WHAT:
//   Fold octave/half-tempo ambiguities toward preferred operating range.
// WHY:
//   In dance/pop contexts the perceived tempo is often the doubled interpretation
//   when raw estimate falls far below the preferred BPM band.
void foldTempoOctave(std::vector<Candidate>& candidates)
{
    for(Candidate& candidate : candidates) {
        while(candidate.bpm < kPreferredMinBPM && candidate.bpm * 2.0f <= kMaxBPM) {
            candidate.bpm *= 2.0f;
        }
        while(candidate.bpm > kMaxBPM && candidate.bpm * 0.5f >= kMinBPM) {
            candidate.bpm *= 0.5f;
        }
    }
}

// WHAT:
//   Phase-alignment score of a BPM hypothesis.
// WHY:
//   Two BPM candidates can have similar ACF scores; phase support provides an
//   orthogonal cue from onset alignment consistency.
float evaluatePhaseSupportForBpm(const std::vector<Vortex::Onset>& onsets, float bpm, int sampleRate)
{
    if(bpm <= 0.0f || sampleRate <= 0 || onsets.size() < 2u) return 0.0f;
    const float periodSamples = (60.0f * static_cast<float>(sampleRate)) / bpm;
    if(periodSamples <= 1.0f) return 0.0f;

    const int phaseBins = 128;
    std::vector<float> phaseHist(static_cast<size_t>(phaseBins), 0.0f);
    for(const Vortex::Onset& onset : onsets) {
        if(onset.pos < 0) continue;
        float phase = std::fmod(static_cast<float>(onset.pos), periodSamples);
        if(phase < 0.0f) phase += periodSamples;
        int bin = static_cast<int>(std::floor((phase / periodSamples) * static_cast<float>(phaseBins)));
        if(bin >= phaseBins) bin = phaseBins - 1;
        if(bin >= 0) phaseHist[static_cast<size_t>(bin)] += safeStrength(onset.strength);
    }

    float best = 0.0f;
    const int half = phaseBins / 2;
    for(int i = 0; i < phaseBins; ++i) {
        const float score = phaseHist[static_cast<size_t>(i)] + 0.35f * phaseHist[static_cast<size_t>((i + half) % phaseBins)];
        best = std::max(best, score);
    }
    return best;
}

/*
 * WHAT:
 *   Local BPM micro-search around each candidate.
 *
 * WHY:
 *   Reduces residual quantization/interpolation error by directly maximizing
 *   phase support in a narrow neighborhood.
 *
 * ASSUMPTION:
 *   Search radius/step are small enough to preserve candidate identity.
 */
void refineCandidateTempoLocalSearch(std::vector<Candidate>& candidates, const std::vector<Vortex::Onset>& onsets, int sampleRate)
{
    if(candidates.empty() || sampleRate <= 0) return;

    for(Candidate& candidate : candidates) {
        const float baseBpm = candidate.bpm;
        const float low = std::max(kMinBPM, baseBpm - kLocalRefineRangeBPM);
        const float high = std::min(kMaxBPM, baseBpm + kLocalRefineRangeBPM);

        const float baseScore = evaluatePhaseSupportForBpm(onsets, baseBpm, sampleRate);
        float bestScore = baseScore;
        float bestBpm = baseBpm;

        for(float bpm = low; bpm <= high; bpm += kLocalRefineStepBPM) {
            const float score = evaluatePhaseSupportForBpm(onsets, bpm, sampleRate);
            if(score > bestScore) {
                bestScore = score;
                bestBpm = bpm;
            }
        }

        candidate.bpm = bestBpm;
        if(baseScore > 1e-8f) {
            const float gain = std::min(1.5f, std::max(0.7f, bestScore / baseScore));
            candidate.rawFitness *= gain;
        }
    }
}

// WHAT:
//   Reweight candidate fitness by normalized phase-support evidence.
// WHY:
//   Blends periodicity strength and alignment strength for more stable ranking.
void rerankWithPhaseSupport(std::vector<Candidate>& candidates, const std::vector<Vortex::Onset>& onsets, int sampleRate)
{
    if(candidates.empty() || sampleRate <= 0) return;

    std::vector<float> phaseScores(candidates.size(), 0.0f);
    float maxPhase = 0.0f;
    for(size_t i = 0; i < candidates.size(); ++i) {
        phaseScores[i] = evaluatePhaseSupportForBpm(onsets, candidates[i].bpm, sampleRate);
        maxPhase = std::max(maxPhase, phaseScores[i]);
    }
    if(maxPhase <= 0.0f) return;

    for(size_t i = 0; i < candidates.size(); ++i) {
        const float phaseNorm = phaseScores[i] / maxPhase;
        candidates[i].rawFitness *= (0.5f + 0.5f * phaseNorm);
    }
}

// WHAT:
//   Conservative fallback when signal quality is insufficient.
std::vector<Vortex::TempoResult> fallbackResult()
{
    return {{120.0f, 1.0f}};
}

} // namespace

/*
 * WHAT:
 *   Stable production tempo estimation (version B).
 *
 * INPUT / OUTPUT:
 *   - onsets: detected onsets, onset.pos in samples.
 *   - sampleRate: Hz.
 *   - return: top tempo candidates with fitness in [0,1], sorted descending.
 *
 * ALGORITHM SUMMARY:
 *   onset envelope -> autocorrelation peak candidates -> merge/fold ->
 *   local BPM refinement -> phase reranking -> normalized fitness output.
 *
 * ASSUMPTIONS:
 *   - Valid BPM search interval is [80, 200].
 *   - kAutoCorrBinSec = 0.0005s is frozen for this stable release.
 *
 * ERROR SOURCES:
 *   - Incorrect or sparse onsets from upstream detector.
 *   - Strong polyrhythms can still create close competing tempos.
 */
std::vector<Vortex::TempoResult> estimateTempo(const std::vector<Vortex::Onset>& onsets, int sampleRate)
{
    if(sampleRate <= 0 || onsets.size() < 2u) return fallbackResult();

    const std::vector<Vortex::Onset> sorted = sortedOnsets(onsets);
    std::vector<float> envelope;
    if(!buildOnsetEnvelope(sorted, sampleRate, envelope)) return fallbackResult();

    const int lagMin = std::max(1, roundToInt((60.0f / kMaxBPM) / kAutoCorrBinSec));
    const int lagMax = std::max(lagMin, roundToInt((60.0f / kMinBPM) / kAutoCorrBinSec));

    std::vector<Candidate> candidates = estimateByAutoCorrelation(envelope, lagMin, lagMax);
    candidates = mergeClosePeaks(candidates);
    foldTempoOctave(candidates);
    candidates = mergeClosePeaks(candidates);
    refineCandidateTempoLocalSearch(candidates, sorted, sampleRate);
    candidates = mergeClosePeaks(candidates);
    rerankWithPhaseSupport(candidates, sorted, sampleRate);
    if(candidates.empty()) return fallbackResult();

    std::stable_sort(candidates.begin(), candidates.end(), CandidateSort());
    if(static_cast<int>(candidates.size()) > kTopK) {
        candidates.resize(static_cast<size_t>(kTopK));
    }

    const float maxFitness = candidates.front().rawFitness;
    if(maxFitness > 0.0f) {
        for(Candidate& candidate : candidates) {
            float normalized = candidate.rawFitness / maxFitness;
            normalized = std::min(1.0f, std::max(0.0f, normalized));
            candidate.rawFitness = normalized;
        }
    }

    std::vector<Vortex::TempoResult> out;
    out.reserve(candidates.size());
    for(const Candidate& candidate : candidates) {
        out.push_back({candidate.bpm, candidate.rawFitness});
    }
    return out;
}

} // namespace TempoEstimatorB
