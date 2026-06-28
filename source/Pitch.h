#pragma once

// Pure, dependency-free 12-TET pitch helpers.
// Convention: MIDI note 69 == A4.  Middle C (MIDI 60) == C4.
// All functions require freqHz > 0 (callers must guard silence).

#include <cmath>

namespace pitch
{

struct NoteInfo
{
    const char* name;   // "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    int         octave; // octave number in scientific pitch notation
};

// MIDI note as a continuous value (pre-rounding) for a frequency.
inline double midiFloatFromFrequency (double freqHz, double refHz) noexcept
{
    return 69.0 + 12.0 * std::log2 (freqHz / refHz);
}

// Nearest MIDI note number for a frequency.
inline int midiNoteFromFrequency (double freqHz, double refHz) noexcept
{
    return static_cast<int> (std::lround (midiFloatFromFrequency (freqHz, refHz)));
}

// Cents offset from the nearest MIDI note, roughly in [-50, +50].
inline double centsFromFrequency (double freqHz, double refHz) noexcept
{
    const double m = midiFloatFromFrequency (freqHz, refHz);
    return 100.0 * (m - std::round (m));
}

inline NoteInfo noteInfoFromMidi (int midiNote) noexcept
{
    static constexpr const char* names[12] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    int idx = midiNote % 12;
    if (idx < 0)
        idx += 12;
    return { names[idx], (midiNote / 12) - 1 };
}

inline NoteInfo noteInfoFromFrequency (double freqHz, double refHz) noexcept
{
    return noteInfoFromMidi (midiNoteFromFrequency (freqHz, refHz));
}

} // namespace pitch
