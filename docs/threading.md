# Threading Model

The plugin runs on two threads:

| Thread | JUCE name | Responsibility |
|--------|-----------|----------------|
| Audio thread | `processBlock` callback | Mono sum, pYIN detection, store atomics |
| Message thread | `timerCallback` (25 Hz), `paint` | Smoothing, display state machine, repaint, skin logic |

## Cross-thread data

Only two `std::atomic<float>` values are written on the audio thread and read on the
message thread:

```cpp
std::atomic<float> displayFreq_        { 0.0f };
std::atomic<float> displayAperiodicity_ { 1.0f };
```

They are stored in `processBlock` after pYIN runs and read in `timerCallback`. The stores
explicitly use `std::memory_order_relaxed` (correct for a single-reader, single-writer
pattern with no ordering requirements between the two values).

### What does NOT cross threads

Note name, octave, and cents are **computed on the message thread** from the already-atomic
frequency using the pure `pitch::*` helpers (`source/dsp/Pitch.h`). No `juce::String` or
complex object leaves the audio thread. This is an intentional design choice:

- Eliminates data-race UB on `juce::String` (its ref-count is atomic, but assignment
  threading is not defined).
- Makes `Pitch.h` unit-testable without JUCE (it depends only on `<cmath>`).
- Keeps the audio thread light (only `PyinDetector` + two atomic stores).

## Message-thread state machine

The display state machine (`DisplayState::tracking / holding / listening`) runs entirely
on the message thread (in `timerCallback`). It drives which UI elements are visible and
at what opacity. See [architecture.md](architecture.md#display-state-machine).

## `processBlock` guarantees

- JUCE guarantees `processBlock` is called sequentially (not re-entrant) on the audio
  thread for a given plugin instance.
- `PyinDetector` is NOT thread-safe — it is called only from `processBlock`.
- The mono accumulation buffer (`monoBuffer_`) is resized per block (O(1) if size
  unchanged) — fine for the audio thread.
- `displayFreq_` and `displayAperiodicity_` stores are the only point of cross-thread
  communication.

## Editor timer

- 25 Hz timer on the message thread.
- Reads atomics, runs the display state machine, updates the skin if the host changed
  `skinName`, then calls `repaint()`.
- `paint()` runs synchronously on the message thread after `repaint()`. All paint logic
  reads the editor's cached values (set in the timer) — no thread-safety issues there.

## FileChooser

The "Import skin…" async file chooser launches a native file dialog. The callback
(provided via `launchAsync`) runs on the message thread, so copying the selected file,
reloading the skin library, and calling `selectSkin` / `applySkinByName` are all safe.
