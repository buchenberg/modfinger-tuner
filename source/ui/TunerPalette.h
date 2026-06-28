#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** Colour theme for the tuner UI as semantic slots, so paint() and the
    LookAndFeel stay skin-agnostic. Instances are produced at runtime from
    JSON skin files (see SkinLibrary).
*/
struct TunerPalette
{
    juce::Colour background;   // plugin background
    juce::Colour panel;        // cents-bar track, label edit background
    juce::Colour primary;      // note letter, needle, cents number, label edit outline
    juce::Colour secondary;    // frequency readout, reference label text
    juce::Colour muted;        // "listening…", markings, non-confident note
    juce::Colour zoneLit;      // in-tune band when the needle is inside
    juce::Colour zoneIdle;     // in-tune band otherwise
    juce::Colour marker;       // center tick
};
