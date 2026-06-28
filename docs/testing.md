# Testing

Two test suites run via CTest. Both are built alongside the plugin.

| Target | Framework | What it covers | Builds without | Source |
|--------|-----------|----------------|----------------|--------|
| `ModfingerTunerTests` | Catch2 (v3.5, fetched) | Pure units: `pitch` math and `YinDetector` on generated sines. **JUCE-free.** | JUCE (links only `Catch2::Catch2WithMain` + `YinDetector.cpp`) | `tests/PitchTests.cpp`, `tests/YinDetectorTests.cpp` |
| `ModfingerTunerJuceTests` | JUCE `UnitTest` | JUCE-coupled seams: `reference` parameter range/default, skin-name state round-trip. | Network (no FetchContent — uses JUCE's built-in runner) | `tests/ProcessorTests.cpp` |

## Build and run

```sh
# Build both suites
cmake --build build --target ModfingerTunerTests ModfingerTunerJuceTests -j

# Run everything
ctest --test-dir build --output-on-failure

# Run one suite directly
./build/ModfingerTunerTests_artefacts/Release/ModfingerTunerTests
./build/ModfingerTunerJuceTests_artefacts/Release/ModfingerTunerJuceTests
```

### Options

- `-DMODFINGER_BUILD_TESTS=OFF` — skip Catch2 (avoids the network fetch for Catch2, useful offline).
- `-DMODFINGER_BUILD_JUCE_TESTS=OFF` — skip the JUCE suite (needs only JUCE, builds offline).

## What the Catch2 tests cover

### `PitchTests.cpp`
- `A4 at 440 Hz` → midi 69, name "A", octave 4.
- `Middle C` → midi 60, name "C", octave 4.
- Sharps resolve correctly (`A#4` ≈ 466 Hz).
- Cents offset tracks detuning (±0.1 tolerance).
- Reference shift transposes without changing the tone (440 Hz against 442 Hz ref = still A4, slightly flat).
- Note index wraps at octave boundaries (MIDI 0 = C-1, MIDI 11 = B-1, MIDI 71 = B4).

### `YinDetectorTests.cpp`
- Pure 440 Hz sine → detected within ±1 Hz, aperiodicity < 0.1.
- 220 Hz sine → detected within ±1 Hz.
- Silence → aperiodicity ≥ 0.3 (no false detection).

The YIN tests feed a generated sine wave through `processBlock` in 512-sample chunks,
filling the ring buffer over 4 full rotations before checking the output.

## What the JUCE UnitTests cover

### Parameter range + default
Creates a processor, checks that `reference` has the expected NormalisableRange
(415–466 Hz) and default value (440 Hz).

### State save/restore round-trip
Sets `reference` to 442 Hz, calls `getStateInformation` → `setStateInformation` on a
new processor, verifies the restored value matches.

### Skin name round-trip
Checks the default skin name is "80s Neon", sets it to "Dark", round-trips
via get/setStateInformation, verifies the restored processor reports "Dark".

## Adding a new test

### Catch2 (pure unit)
1. Add a `.cpp` in `tests/` with `#include <catch2/catch_test_macros.hpp>` and your
   unit under test (e.g. `#include "dsp/Pitch.h"`).
2. Add the file to `target_sources` in the Catch2 block in `CMakeLists.txt`.
3. Rebuild and run.

### JUCE UnitTest (plugin-coupled)
1. Add a `beginTest("…") { … }` block in `tests/ProcessorTests.cpp` (or a new file).
2. If a new file, add it to `target_sources` for `ModfingerTunerJuceTests`.
3. Rebuild and run.
