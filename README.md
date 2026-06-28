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
| **JUCE** | Bundled as a git submodule at `JUCE/` (pinned to **8.0.12**). See below. |
| **Network access** | Only on first configure if tests are enabled (Catch2 is fetched via CMake `FetchContent`). Pass `-DMODFINGER_BUILD_TESTS=OFF` to build offline. |

### JUCE (bundled submodule)

JUCE is bundled as a git submodule at `JUCE/` (pinned to JUCE **8.0.12**), so the repo is
self-contained. Clone with submodules:

```sh
git clone --recurse-submodules <repo-url>
```

If you already cloned without them, initialize the submodule:

```sh
git submodule update --init --recursive
```

> **You usually don't need to.** CMake auto-initializes the submodule on first
> configure (it runs `git submodule update --init` when `JUCE/` is empty), so even a
> plain `git clone` works. Disable with `-DMODFINGER_AUTOINIT_SUBMODULES=OFF` (e.g. in CI
> where you've already fetched it).

`CMakeLists.txt` builds JUCE from the bundled checkout:

```cmake
add_subdirectory(JUCE ${CMAKE_BINARY_DIR}/JUCE)
```

To use a different JUCE version, check out the desired tag inside `JUCE/` and commit the
updated submodule pointer (e.g. `cd JUCE && git checkout 8.0.x && cd .. && git add JUCE`).

---

## Project structure

```
modfinger-tuner/
├── CMakeLists.txt
├── JUCE/                         # JUCE 8.0.12 (git submodule)
├── source/
│   ├── PluginProcessor.{h,cpp}   # AudioProcessor: mono sum, drives YIN, pushes atomics
│   ├── PluginEditor.{h,cpp}      # Editor: smoothing, UI rendering, reference label
│   ├── YinDetector.{h,cpp}       # Pure DSP: YIN pitch detector (JUCE-free, testable)
│   └── Pitch.h                   # Pure 12-TET helpers: note/octave/cents (JUCE-free, testable)
└── tests/
    ├── PitchTests.cpp            # Catch2 unit tests for pitch math
    ├── YinDetectorTests.cpp      # Catch2 unit tests for the detector on generated sines
    └── ProcessorTests.cpp        # JUCE UnitTests: parameter range + state round-trip
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

Built artefacts land under `build/ModfingerTuner_artefacts/Release/<Format>/`
(single-config generators like Makefiles; with a multi-config generator such as Xcode the
`Release/` segment moves to the `--config` flag).

### Installing plugins

`COPY_PLUGIN_AFTER_BUILD` is `OFF`, so copy the bundles manually (macOS):

```sh
# VST3
cp -R "build/ModfingerTuner_artefacts/Release/VST3/Modfinger Tuner.vst3" \
      ~/Library/Audio/Plug-Ins/VST3/

# AU
cp -R "build/ModfingerTuner_artefacts/Release/AU/Modfinger Tuner.component" \
      ~/Library/Audio/Plug-Ins/Components/
```

Then **fully restart your DAW** so it rescans. (AU components may also need `auval`
validation before the host registers them.)

---

## Tests

Two test suites, both run via CTest:

```sh
cmake --build build --target ModfingerTunerTests ModfingerTunerJuceTests -j
ctest --test-dir build --output-on-failure
```

| Target | Framework | Covers |
|--------|-----------|--------|
| `ModfingerTunerTests` | Catch2 (fetched) | Pure logic: `pitch` note/cents math and `YinDetector` on generated sines. JUCE-free. |
| `ModfingerTunerJuceTests` | JUCE `UnitTest` | JUCE-coupled seams: `reference` parameter range/default and the state save/restore round-trip. |

Options (both on by default):

- `-DMODFINGER_BUILD_TESTS=OFF` — skip the Catch2 suite (avoids the network fetch for offline/minimal builds).
- `-DMODFINGER_BUILD_JUCE_TESTS=OFF` — skip the JUCE suite (needs only JUCE, builds offline).

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
- **CMake can't find JUCE** — ensure the submodule is initialized: `git submodule update --init --recursive` (see *JUCE (bundled submodule)*).
- **`Modfinger Tuner.vst3: No such file` when installing** — artefacts are under
  `build/ModfingerTuner_artefacts/Release/...` with the Makefiles generator; adjust the copy
  path accordingly.
- **First configure needs network** — Catch2 is fetched for tests. Use
  `-DMODFINGER_BUILD_TESTS=OFF` to skip.

---

## License

Licensed under the [MIT License](LICENSE). Copyright © 2026 Gregory Buchenberger.
