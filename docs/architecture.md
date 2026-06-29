# Architecture Overview

Modfinger Tuner is a monophonic chromatic tuner plugin built with [JUCE 8](https://juce.com/).
It uses the **YIN** fundamental-frequency estimator (De Cheveigné & Kawahara, 2002) and
produces **VST3**, **AU**, and **Standalone** binaries from a single CMake project.

## Audio pipeline

```
Stereo input
  │
  ├─ Passthrough: copied to output (dry, no DSP)
  └─ Mono sum: (L + R) / 2
       │
       └─ PyinDetector::processBlock → ring buffer (4096 samples)
            │
            Every 1024 samples (≈43 Hz detection rate):
            │
            └─ PyinDetector::runDetection()
                 ├─ Difference function d(τ) over analysis window 2048
                 ├─ Cumulative-mean normalised difference
                 ├─ Candidate extraction (≤5 voiced periods, sorted)
                 ├─ HMM / Viterbi decoding (12-frame sliding window)
                 └─ Stores { frequency, aperiodicity } as atomics
```

## Cross-thread hand-off

Only two values cross the audio→UI boundary, both `std::atomic<float>`:

| Value | Writer | Reader | Meaning |
|-------|--------|--------|---------|
| `displayFreq_` | audio thread (`processBlock`) | message thread (`timerCallback`) | raw detected Hz |
| `displayAperiodicity_` | audio thread | message thread | YIN confidence (0 = clean, 1 = noise) |

Note/octave/cents are **derived on the UI thread** from the smoothed frequency via the
pure `pitch::*` helpers (`source/dsp/Pitch.h`). No `juce::String` or complex object
crosses threads — this is deliberate, keeps things lock-free, and makes the
pitch-math helpers unit-testable without JUCE.

## UI pipeline

```
Timer (25 Hz)
  │
  ├─ Reads displayFreq_ / displayAperiodicity_ (atomics)
  ├─ Exponential smoothing (attack α=0.25, release α=0.1)
  │
  ├─ displayState_ machine:
  │     tracking  ←  rawFreq > 0  (update smoothed, cache note/cents/aperiodicity)
  │     holding   ←  rawFreq ≤ 0 AND holdTicksRemaining_ > 0  (freeze, fade dim)
  │     listening ←  hold expired  (smoothedFreq_ = 0)
  │
  └─ repaint() → paint()
       ├─ Note + octave (or "listening…"), dimmed by holdFadeAlpha_ while holding
       ├─ Cents numeric (just below note/octave, dimmed while holding)
       ├─ Cents bar (always drawn; green zone lights up on inTune)
       ├─ Needle + triangle (shown while tracking/holding, dimmed while holding)
       └─ Frequency readout (shown while tracking/holding)
```

## Display state machine

| State | Trigger | Smoothed freq | Note/octave/cents shown? | Needle/freq shown? | Fade |
|-------|---------|---------------|--------------------------|---------------------|------|
| **tracking** | `rawFreq > 0` | tracking raw | yes, full alpha | yes, full alpha | 1.0 |
| **holding** | `rawFreq ≤ 0` and `holdTicksRemaining_ > 0` | frozen at last value | yes, dimmed | yes, dimmed | eased to 0.35 |
| **listening** | `holdTicksRemaining_ == 0` | `0.0` | no — "listening…" | no | N/A |

Hold lasts `kHoldTicks` = 125 ticks ≈ 5 s at 25 Hz.

## File map

| Path | Role |
|------|------|
| `source/PluginProcessor.{h,cpp}` | `AudioProcessor`: bus config, processBlock, atomics, state |
| `source/PluginEditor.{h,cpp}` | Timer, smoothing, displayState, paint, skin selector |
| `source/dsp/Pitch.h` | Pure 12-TET helpers: midiNote, cents, NoteInfo (no JUCE) |
| `source/dsp/PyinDetector.{h,cpp}` | pYIN detector (YIN core + Viterbi decoder, no JUCE), unit-tested with Catch2 |
| `source/ui/TunerPalette.h` | Semantic colour-slot struct |
| `source/ui/SkinLibrary.{h,cpp}` | Runtime JSON skin loader (bundled + user folder) |
| `skins/dark.json`, `skins/eighties_neon.json` | Bundled default skins (BinaryData) |
| `tests/` | Catch2 (pitch + YIN) + JUCE UnitTest (processor/state) |
| `CMakeLists.txt` | Plugin target + binary data + test targets |

### Parameters

Only **one host parameter** is exposed: `reference` (415–466 Hz, default 440 Hz).
The active skin is stored **by name** in `apvts.state` (plugin state, not a host
parameter), so the skin set can be dynamic at runtime.
