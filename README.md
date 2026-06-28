# Modfinger Tuner

A real-time, monophonic **chromatic tuner** audio plugin built with [JUCE](https://juce.com/).
It uses the **YIN** fundamental-frequency estimator (De Cheveigné & Kawahara, 2002) to detect
pitch from an incoming audio signal and displays the note, octave, and a cents-tuning meter.

Available as **VST3**, **AU**, and a **Standalone** app (macOS).

---

## Features

- **YIN pitch detection** — accurate monophonic fundamental-frequency estimation with
  parabolic interpolation and an aperiodicity (confidence) measure.
- **Tuning meter** — note name + octave and a ±50 cents needle with an in-tune green zone.
- **Confidence-aware UI** — dimmed readouts when the signal is aperiodic/noisy; a
  "listening…" state when no pitch is present.
- **Editable reference pitch** — click the Hz label at the bottom to retune A4
  (415–466 Hz).
- **Low-overhead audio path** — detection runs every 512 samples; only two floats cross
  the audio→UI thread boundary (via atomics).

---

## Requirements

| Dependency | Notes |
|------------|-------|
| **CMake ≥ 3.22** | Build system. |
| **C++17 compiler** | Apple Clang (Xcode Command Line Tools) on macOS. |
| **JUCE** | Must be checked out as a **sibling directory** at `../Juce` (see below). |
| **Network access** | Only on first configure if tests are enabled (Catch2 is fetched via CMake `FetchContent`). Pass `-DMODFINGER_BUILD_TESTS=OFF` to build offline. |

### JUCE as a sibling

`CMakeLists.txt` references JUCE out-of-tree:

```cmake
add_subdirectory(../Juce ${CMAKE_BINARY_DIR}/JUCE)
```

So JUCE is **not** vendored here. Clone it next to this project:

```sh
cd ..                # parent of modfinger-tuner/
git clone https://github.com/juce-framework/JUCE.git Juce
```

---

## Project structure

```
modfinger-tuner/
├── CMakeLists.txt
├── source/
│   ├── PluginProcessor.{h,cpp}   # AudioProcessor: mono sum, drives YIN, pushes atomics
│   ├── PluginEditor.{h,cpp}      # Editor: smoothing, UI rendering, reference label
│   ├── YinDetector.{h,cpp}       # Pure DSP: YIN pitch detector (JUCE-free, testable)
│   └── Pitch.h                   # Pure 12-TET helpers: note/octave/cents (JUCE-free, testable)
└── tests/
    ├── PitchTests.cpp            # Catch2 unit tests for pitch math
    └── YinDetectorTests.cpp      # Catch2 unit tests for the detector on generated sines
```

The DSP (`YinDetector`) and music-theory math (`Pitch`) are intentionally kept free of
JUCE dependencies so they can be unit-tested in isolation.

---

## Building

```sh
# Configure (from the project root)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build a specific format (or use ModfingerTuner_All for everything)
cmake --build build --target ModfingerTuner_VST3 -j      # VST3
cmake --build build --target ModfingerTuner_AU -j        # AU
cmake --build build --target ModfingerTuner_Standalone -j
```

Built artefacts land under `build/ModfingerTuner_artefacts/<Format>/`.

### Installing plugins

`COPY_PLUGIN_AFTER_BUILD` is `OFF`, so copy the bundles manually (macOS):

```sh
# VST3
cp -R "build/ModfingerTuner_artefacts/VST3/Modfinger Tuner.vst3" \
      ~/Library/Audio/Plug-Ins/VST3/

# AU
cp -R "build/ModfingerTuner_artefacts/AU/Modfinger Tuner.component" \
      ~/Library/Audio/Plug-Ins/Components/
```

Then **fully restart your DAW** so it rescans. (AU components may also need `auval`
validation before the host registers them.)

---

## Tests

```sh
cmake --build build --target ModfingerTunerTests -j
ctest --test-dir build --output-on-failure
```

Tests are built by default. Disable them with `-DMODFINGER_BUILD_TESTS=OFF`
(e.g. for offline CI or minimal builds).

---

## How it works

1. **Mono sum** — stereo input is averaged to mono in `processBlock`.
2. **YIN detection** — a 4096-sample ring buffer is analyzed every 512 samples.
   The difference function → cumulative mean normalized difference → absolute threshold
   → parabolic interpolation yields the fundamental frequency and an aperiodicity score.
3. **Cross-thread hand-off** — only `frequency` and `aperiodicity` (both
   `std::atomic<float>`) are written on the audio thread.
4. **UI** — a 25 Hz timer smooths the frequency on the message thread and derives the note
   name, octave, and cents via the pure `pitch` helpers, then repaints.

> **Note name / octave / cents are computed on the UI thread** from the atomic frequency.
> This keeps the audio→UI interface lock-free and race-free (no `juce::String` crosses
> threads).

---

## Configuration

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| `reference` | 415–466 Hz | 440 Hz | A4 reference frequency used for note/cents mapping. Editable via the Hz label. |

Plugin state (parameter values) is saved/restored via the host.

---

## Troubleshooting

- **DAW shows an old UI after rebuilding** — fully quit and reopen the DAW (or remove and
  re-add the plugin). Hosts cache plugin instances.
- **"ad-hoc signature" warnings on macOS** — builds are ad-hoc signed; they run locally but
  are not notarized. Adjust signing in `CMakeLists.txt` (`JUCE_SIGN_INTERNALLY`/notarization)
  for distribution.
- **CMake can't find JUCE** — ensure JUCE is cloned at `../Juce` (see *JUCE as a sibling*).
- **First configure needs network** — Catch2 is fetched for tests. Use
  `-DMODFINGER_BUILD_TESTS=OFF` to skip.

---

## License

Licensed under the [MIT License](LICENSE). Copyright © 2026 Gregory Buchenberger.
