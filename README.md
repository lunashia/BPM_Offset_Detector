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

# BPM_offset_detector v0.1.1-alpha Test Notes (2026/04/26)

It compares `BPM_offset_detector v0.1.1-alpha` with several mainstream AI systems on four audio files.

The goal is not only to check whether the top result is correct, but also whether the correct tempo appears among the detector's Top-3 candidates.
## Prompt
EN for GPT, Gemini and Claude; Simplified CN for Minimax.
```
You are an audio signal processing system. Estimate the BPM (tempo) strictly from the raw audio input. 
Constraints: 
Do NOT use any external knowledge (song title, artist, genre, or typical BPM ranges) 
Do NOT guess based on musical style or prior knowledge 
Only rely on signal-level features such as onset detection, transient peaks, and periodicity 
Please output the estimated BPM with a confidence level If the rhythm is unclear, state uncertainty instead of guessing. 
If your reasoning references genre, familiarity, or prior knowledge, discard that reasoning and recompute using only signal-derived features. 
Treat this as a blind signal processing task, as if the audio is completely unknown and has never been heard before. 
If you are uncertain, you must output a range instead of a single BPM.
```
```
你现在是一个音频信号处理系统。
请仅基于输入的音频数据本身估计该音频的 BPM（节拍速度）。
严格遵守以下规则：
不允许使用歌曲名称、艺术家、风格等任何外部信息
不允许基于经验猜测常见 BPM
只能基于音频的节奏结构进行分析（如瞬态、onset、周期性等）
请输出最终 BPM，并说明置信度。
如果音频不清晰或节奏不明显，请说明不确定性，而不是猜测。
如果你的推理过程中涉及音乐风格、熟悉度或任何先验知识，请丢弃该推理，并仅基于音频信号特征重新计算。或者：
将此视为一个盲信号处理任务，就像该音频完全未知且此前从未被听过一样。
如果你不确定，必须输出一个 BPM 范围，而不是单一数值。
如果无法在上述约束下完成任务，请直接说明“无法可靠估计”，而不是给出猜测结果。 
```

## Test Results

| Audio file | Actual BPM | Detector Rank 1 | Detector Rank 2 | Detector Rank 3 | Gemini 3.1 Pro | GPT 5.4 Extra High | GPT 5.5 Extra High | DeepSeek | Qwen 3.5 Max | Claude | Minimax M2.7 Max | GPT 5.3 |
|---|---:|---:|---:|---:|---|---|---|---|---|---|---|---|
| `test.wav` | 175 | 175.018, fitness 1.000 | 116.661, fitness 0.204 | 139.993, fitness 0.201 | [180 BPM](https://gemini.google.com/share/49de5e119d8b) | 44-87 BPM, low-medium confidence | 174-176 BPM, medium-high confidence | Does not support `.wav` | Does not support `.wav` | To be done | [87.6 BPM, high confidence](https://agent.minimax.io/share/391648142807169?chat_type=2) | [80-140 BPM, low confidence](https://chatgpt.com/share/69ed88a5-f444-83ea-8885-8c57cdf81166) |
| `test_2.mp3` | 140 | 163.366, fitness 1.000 | 122.504, fitness 0.719 | 140.017, fitness 0.532 | [140 BPM, with strong 70 BPM half-time pulse](https://gemini.google.com/share/b484d691548e) | 121-125 BPM, low confidence | 122.5-140.0 BPM, medium-low confidence | Does not support `.mp3` | Does not support `.mp3` | To be done | [150-160 BPM, most likely 155](https://agent.minimax.io/share/391548819357771?chat_type=2) | [60-65 BPM primary; 120-130 BPM alternate](https://chatgpt.com/share/69ed9ef6-a2dc-83ea-a328-6174d87c6993) |
| `test_3.mp3` | 147 | 195.981, fitness 1.000 | 146.987, fitness 0.211 | 117.610, fitness 0.177 | [120 BPM, high confidence](https://gemini.google.com/share/be9a82a90f63) | To be done | 74-148 BPM, medium confidence for the tempo family | Does not support `.mp3` | Does not support `.mp3` | To be done | [About 138 BPM](https://agent.minimax.io/share/391562647302217?chat_type=2) | To be done |
| `test_4.mp3` | 230 | 114.997, fitness 1.000 | 153.352, fitness 0.718 | 184.026, fitness 0.383 | [188 BPM, high confidence](https://gemini.google.com/share/e43d1c3b9189) | To be done | 76.0-77.1 BPM, moderate confidence | Does not support `.mp3` | Does not support `.mp3` | To be done | [132.0 BPM, 98% confidence](https://agent.minimax.io/share/391548819357774?chat_type=2) | To be done |

## Observations

The detector's Rank 1 result is not always the actual BPM.

However, the Top-3 candidates are more useful than Rank 1 alone:

- `test.wav`: Rank 1 is almost exactly correct.
- `test_2.mp3`: the correct BPM appears as Rank 3.
- `test_3.mp3`: the correct BPM appears as Rank 2.
- `test_4.mp3`: Rank 1 is almost exactly half of the actual BPM.

This means the detector is already finding meaningful periodic tempo candidates. The main weakness is candidate ranking and half/double tempo selection, not necessarily raw tempo discovery.

Also, the current `fitness` score should not be treated as a direct probability of being the true BPM. In all four files, Rank 1 has fitness `1.000`, but only one Rank 1 result directly matches the actual tempo 😅

## Half/Double Ambiguity

Both this detector and mainstream AI systems meet many half/double ambiguity cases.

It's a common problem in BPM detection because tempo is inferred from repeated audio events, such as transients, onset peaks, drum hits, or rhythmic accents. The same rhythm can produce several related periodicities:

- a fast pulse, such as 175 BPM;
- a half-speed pulse, such as 87.5 BPM;
- a quarter-speed accent cycle, such as 43.75 BPM;
- or another musically related subdivision.

Signal-level methods such as onset detection, autocorrelation, FFT, and interval histograms can detect all of these as strong periodic patterns. 
Without musical interpretation or external knowledge, it can be difficult to decide which periodicity should be called the "actual BPM".

This is why many AIs output ranges or mention alternate interpretations, such as half-time or double-time. 🤔