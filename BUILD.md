# Build (CMake)

In `BPM_offset_detector/`, run:

```bash
cmake --workflow --preset default
```

Run:

```bash
./build/bpm_offset_detector_example
```

On Windows multi-config generators:

```bash
.\build\Release\bpm_offset_detector_example.exe
```

Run tests:

```bash
ctest --test-dir build --output-on-failure
```

Optional integration audio override:

```bash
BPM_TEST_AUDIO=/absolute/path/to/audio.wav ctest --test-dir build --output-on-failure
```
