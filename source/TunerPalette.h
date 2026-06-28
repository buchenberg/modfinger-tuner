#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
/** Colour theme for the tuner UI as semantic slots, so paint() and the
    LookAndFeel stay skin-agnostic. Add new presets below and expose them via
    the "skin" parameter in the processor.
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

    static TunerPalette darkOrange()
    {
        return { juce::Colour (0xff1a1a1e), juce::Colour (0xff2a2a30), juce::Colour (0xffe0743b), juce::Colour (0xffa0a0a8),
                 juce::Colour (0xff5a5a62), juce::Colour (0xff3fd66b), juce::Colour (0xff22aa44), juce::Colour (0xff888890) };
    }

    static TunerPalette eightiesNeon()
    {
        return { juce::Colour (0xff0a0a12), juce::Colour (0xff15151f), juce::Colour (0xffeaff00), juce::Colour (0xffff2db3),
                 juce::Colour (0xff8a5a9a), juce::Colour (0xff39ff14), juce::Colour (0xff1a7a14), juce::Colour (0xff8a8a96) };
    }
};
