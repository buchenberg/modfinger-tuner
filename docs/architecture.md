# Architecture Overview

Modfinger Tuner is a monophonic chromatic tuner plugin built with [JUCE 8](https://juce.com/).
It produces **VST3**, **AU**, and **Standalone** binaries from a single CMake project.

The pitch detector is **pYIN** (Mauch & Dixon, 2012) — a probabilistic upgrade of the
classic YIN fundamental-frequency estimator. YIN decides pitch once per detection frame
by picking a single "best guess." pYIN keeps *several* viable guesses per frame and uses
a statistical model across time to pick the most likely sequence. This makes it
dramatically more robust against octave errors, noise, and momentary drop-outs while
using the same raw YIN math as its foundation.

---

## How pYIN works

A non-DSP mental model: imagine a tuner that overhears a note. At any instant, the
signal could be A4 (440 Hz), A3 (220 Hz — an octave lower), or just noise. pYIN's job is
to decide which one it is, *and* to stay consistent over time.

### Step 1 — YIN difference function (same as the original)

The 4096-sample ring buffer is copied into a contiguous array. For every plausible
period τ (22 to 735 samples, ≈ 60 Hz to 2000 Hz), pYIN computes how well the signal
matches itself when shifted by τ:

> **d(τ)** = sum of (sample[j] − sample[j+τ])² over a 2048‑sample window.

A small d(τ) means the signal repeats well at period τ — it's a good candidate. The raw
d(τ) is then turned into a **cumulative-mean normalised difference** (CMND) that makes
values comparable across different τ.

### Step 2 — Candidate extraction

Instead of grabbing just the single best τ, pYIN finds **every local minimum** of the
CMND (up to 5 per frame). Each minimum is a distinct period hypothesis — fundamental,
octave below, fourth below, etc. — and the CMND value at the dip serves as its
*a periodicity* score: 0.0 = perfectly periodic, higher = noisier.

The five candidates are ranked by aperiodicity with a tiny bias toward shorter periods
(higher frequencies), because the fundamental is naturally the shortest period that
produces a good match.

### Step 3 — HMM / Viterbi decoding

This is the "probabilistic" upgrade from YIN. A **Hidden Markov Model** (HMM) models the
tuner's internal state across time:

- **States** are pitch periods. There are 5 voiced candidates per frame, plus an
  "unvoiced" state that means "no pitch / noise."
- **Observations** are the per‑frame candidate scores — if a candidate has a low
  aperiodicity (clean match), the model thinks that pitch is likely.
- **Transitions** encode how believable it is for the pitch to change from one frame to
  the next. A **Gaussian penalty** on the log‑period difference keeps the pitch smooth:
  staying at the same note costs nothing, a small vibrato shift costs a little, and a
  wild octave jump costs a lot. Voiced ↔ unvoiced transitions have fixed penalties
  (harder to enter unvoiced than to leave it — the model prefers to "hear" a note).

The **Viterbi algorithm** then looks at a sliding window of the most recent 12 detection
frames and finds the single most probable path through the states — the pitch sequence
that best explains the observed signal. This is why pYIN doesn't flicker between octaves
the way plain YIN can: even if two candidates score equally in one frame, the transition
model strongly prefers the sequence that stayed consistent.

The detector outputs the last frame's Viterbi‑decoded frequency (0 Hz if unvoiced) and
the corresponding aperiodicity score.

---

## Audio pipeline

```
Stereo input
  │
  ├─ Passthrough: copied to output (dry, no DSP)
  └─ Mono sum: (L + R) / 2
       │
       └─ PyinDetector::processBlock → ring buffer (4096 samples)
            │
            Every 1024 samples (≈ 43 Hz detection rate):
            │
            └─ PyinDetector::runDetection()
                 ├─ Difference function d(τ) — window 2048, 4‑accumulator ILP
                 ├─ CMND normalisation
                 ├─ Candidate extraction — ≤5 voiced-period minima, ranked
                 ├─ Viterbi decoding — 12‑frame sliding HMM
                 └─ Stores { frequency, aperiodicity } as atomics
```

## Cross-thread hand-off

Only two values cross the audio→UI boundary, both `std::atomic<float>`:

| Value | Writer | Reader | Meaning |
|-------|--------|--------|---------|
| `displayFreq_` | audio thread (`processBlock`) | message thread (`timerCallback`) | raw detected Hz (Viterbi output) |
| `displayAperiodicity_` | audio thread | message thread | periodicity score (0 = clean, 1 = noise) |

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
| `source/dsp/PyinDetector.{h,cpp}` | pYIN detector — YIN core + candidate extraction + Viterbi; JUCE‑free, Catch2‑tested |
| `source/ui/TunerPalette.h` | Semantic colour-slot struct |
| `source/ui/SkinLibrary.{h,cpp}` | Runtime JSON skin loader (bundled + user folder) |
| `skins/dark.json`, `skins/eighties_neon.json` | Bundled default skins (BinaryData) |
| `tests/` | Catch2 (pitch + pYIN) + JUCE UnitTest (processor/state) |
| `CMakeLists.txt` | Plugin target + binary data + test targets |

### Parameters

Only **one host parameter** is exposed: `reference` (415–466 Hz, default 440 Hz).
The active skin is stored **by name** in `apvts.state` (plugin state, not a host
parameter), so the skin set can be dynamic at runtime.
