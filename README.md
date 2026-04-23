# BPM Offset Detector

## 1) Project Overview

`BPM Offset Detector` is a C++ audio analysis project for:

- loading audio (`loadAudio`)
- detecting onset events (`DetectOnsetsFFT`)
- estimating tempo candidates (`TempoEstimatorB::estimateTempo`)
- estimating beat phase offset (`CalculateOffset`)

The example entrypoint is `examples/main.cpp`, which prints final `BPM` and `offset`.

---

## 2) Build (g++)

From `BPM_offset_detector/`:

```bash
g++ -std=c++17 -O2 -Iinclude -Ithird_party \
  examples/main.cpp \
  src/load_audio.cpp src/OnsetDetectorFFT.cpp src/tempo_estimator_b.cpp \
  src/OffsetDetector.cpp src/FFT.cpp src/miniaudio_impl.cpp \
  -o bpm_offset_detector_example
```

On Windows PowerShell, line-break with backtick or keep it in one line.

---

## 3) Usage (CLI)

### Run with explicit audio path

```bash
./bpm_offset_detector_example /path/to/audio.wav
```

Windows:

```powershell
.\bpm_offset_detector_example.exe C:\path\to\audio.wav
```

### Run without argument

If no argument is provided, the example tries default files in order:

1. `test.wav`
2. `../test.wav`
3. `../../test.wav`
4. `../../../test.wav`

---

## 4) Output Explanation

Program output:

- `BPM`: top tempo candidate (unit: beats per minute)
- `Offset(ms)`: beat phase offset relative to audio start `t=0` (unit: milliseconds)

Offset is normalized to one beat cycle (`[0, beat_ms)` in intent).

---

## 5) Current Limitations

- Best suited for music with relatively stable meter/tempo (commonly 4/4 material).
- Complex rhythms (heavy syncopation, polyrhythms, sparse percussion) may reduce accuracy.
- Tempo changes (accelerando/ritardando, drift, live recordings) can introduce error because current estimator assumes a mostly global tempo.
- Very noisy audio or weak transients can hurt onset quality, which affects both BPM and offset.

---

## 6) Offset Status (Important)

Offset detection is **not fully mature yet**.

- It is usable for many regular tracks, but edge cases can still produce unstable or biased phase.
- The offset module is planned for future improvement (better phase modeling, stronger ambiguity handling, and more robust behavior under complex rhythm).

---

## 7) Additional Notes

## Module Structure and Algorithm Overview

### Modules

- `src/load_audio.cpp` + `include/load_audio.h`  
  **Function**: decode audio file and downmix to mono PCM.  
  **Input**: file path.  
  **Output**: `audio` (`std::vector<float>`), `numFrames`, `sampleRate`.

- `src/OnsetDetectorFFT.cpp` + `include/OnsetDetectorFFT.h`  
  **Function**: detect onset events from mono audio.  
  **Input**: mono PCM (`audio`), `numFrames`, `sampleRate`.  
  **Output**: `std::vector<Onset>` where each onset has `{pos, strength}`.

- `src/tempo_estimator_b.cpp` + `include/tempo_estimator_b.h`  
  **Function**: estimate tempo candidates (stable B version).  
  **Input**: onset list + `sampleRate`.  
  **Output**: `std::vector<TempoResult>` (`bpm`, `fitness`), sorted by confidence.

- `src/OffsetDetector.cpp` + `include/OffsetDetector.h`  
  **Function**: estimate beat phase offset relative to audio start.  
  **Input**: tempo candidates + onset list + `sampleRate`.  
  **Output**: `offset` in milliseconds.

- `examples/main.cpp`  
  **Function**: example CLI entry for full pipeline.  
  **Input**: audio path (or default search path).  
  **Output**: printed `BPM` and `Offset(ms)`.

### End-to-end Pipeline

1. Decode audio to mono PCM (`loadAudio`)  
2. Detect onsets (`DetectOnsetsFFT`)  
3. Estimate tempo candidates (`TempoEstimatorB::estimateTempo`)  
4. Estimate offset using best tempo/onset support (`CalculateOffset`)  
5. Print top BPM + offset

### Algorithm Summary

- **Onset**: STFT-based novelty fusion (`spectral flux + HFC + phase deviation`) with adaptive peak picking.
- **Tempo (B)**: onset-envelope autocorrelation, harmonic enhancement, peak interpolation, candidate merge/rerank by phase support.
- **Offset**: beat-phase histogram + local quadratic refinement, then beat/offbeat support comparison.

### Key Assumptions

- `audio` is mono float PCM.
- `Onset.pos` is **sample index** (not ms, not STFT frame index).
- `sampleRate` is in **Hz**.
- `bpm` is in **beats per minute**.
- `offset` is in **milliseconds**, interpreted as phase offset from `t=0`.

### Public API (current stable path)

- `loadAudio(...)`
- `Vortex::DetectOnsetsFFT(...)`
- `TempoEstimatorB::estimateTempo(...)`
- `Vortex::CalculateOffset(...)`

### Units and assumptions

- `Onset.pos`: sample index (not ms, not frame index)
- `sampleRate`: Hz
- `BPM`: beats per minute
- `offset`: milliseconds

### Test

Unit/integration tests exist under `tests/` and can be built with the same source set plus test files.

---

## License / Third-party

This project uses `miniaudio` (single-header dependency in `third_party/miniaudio.h`).
