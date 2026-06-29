#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** Semantic colour slots for a tuner skin.

    Instances are produced at runtime from JSON skin files by SkinLibrary.
    The slot names are fixed — each corresponds to one visual role in paint():

      background  — plugin fill
      panel       — cents-bar track, label edit background, skin button
      primary     — note letter, needle, cents number, label edit outline
      secondary   — frequency readout, reference label text
      muted       — "listening…", markings, non‑confident note
      zoneLit     — in‑tune band when the needle is inside  (±5¢, α=0.85)
      zoneIdle    — in‑tune band otherwise                   (α=0.22)
      marker      — center tick on the cents bar
*/
struct TunerPalette
{
    juce::Colour background;
    juce::Colour panel;
    juce::Colour primary;
    juce::Colour secondary;
    juce::Colour muted;
    juce::Colour zoneLit;
    juce::Colour zoneIdle;
    juce::Colour marker;
};
