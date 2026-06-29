#pragma once

#include "PluginProcessor.h"
#include "dsp/Pitch.h"
#include "ui/TunerPalette.h"
#include "ui/SkinManager.h"

//==============================================================================
/** Flat LookAndFeel — dark theme defaults (overridden per‑skin by applySkinByName). */
struct TunerLookAndFeel : public juce::LookAndFeel_V4
{
    TunerLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId,  juce::Colour (0xff1a1a1e));
        setColour (juce::Label::textColourId,                  juce::Colour (0xffa0a0a8));
        setColour (juce::Label::textWhenEditingColourId,       juce::Colour (0xffe0743b));
        setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff24242a));
        setColour (juce::Label::outlineWhenEditingColourId,    juce::Colour (0xffe0743b));
    }
};

//==============================================================================
/** Plugin editor — timer‑driven UI for the tuner.

    Runs a 25 Hz timer on the JUCE message thread.  Every tick:
      1. Reads the atomics (frequency + aperiodicity) from the processor.
      2. Runs the 3‑state display machine (tracking / holding / listening).
      3. Eases the hold‑fade alpha for smooth visual transitions.
      4. Checks for host‑initiated skin changes.
      5. Calls repaint().

    The skin system is fully runtime‑driven — bundled defaults + imported user
    skins (see SkinLibrary).  The active skin is persisted by name in plugin
    state, not as a host parameter.
*/
class ModfingerTunerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    ModfingerTunerAudioProcessorEditor (ModfingerTunerAudioProcessor&);
    ~ModfingerTunerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    // ── Timer ─────────────────────────────────────────────────────
    void timerCallback() override;

    /** Measure the advance width of a single-line string using GlyphArrangement
        (replaces the deprecated Font::getStringWidthFloat). */
    static float textWidth (const juce::Font&, const juce::String&);

    // ── Members ────────────────────────────────────────────────────

    ModfingerTunerAudioProcessor& processorRef_;
    TunerLookAndFeel tunerLAF_;

    juce::Label referenceLabel_;                // click-to-edit reference pitch

    SkinManager skinManager_;                   // runtime skin library, button, menu, import

    TunerPalette palette_;                      // active skin's colour slots (synced from skinManager_)

    // ── Display state ──────────────────────────────────────────────

    float smoothedFreq_ = 0.0f;                 // 0 = no signal / unvoiced

    /** Three readout states driven by the pYIN output:
        - tracking  — pitch present, full brightness
        - holding   — pitch just stopped, dimmed for ~5 s
        - listening — hold expired, "listening…" placeholder  */
    enum class DisplayState { tracking, holding, listening };
    DisplayState displayState_ = DisplayState::listening;

    int   holdTicksRemaining_ = 0;              // countdown from kHoldTicks (≈5 s @ 25 Hz)
    float holdFadeAlpha_      = 1.0f;           // eased between 1.0 and kHoldFadeAlpha

    // ── Cached display values (derived on the message thread) ──────

    pitch::NoteInfo cachedNote_   { "A", 4 };   // default is A4
    double          cachedCents_  = 0.0;
    float           cachedAperiodicity_ = 1.0f; // starts undecided
    float           cachedRefHz_        = 440.0f;

    // ── Tuning constants ───────────────────────────────────────────

    static constexpr float kConfidentAperiodicity = 0.2f;  // below this → confident note colour
    static constexpr int   kHoldTicks             = 125;   // ≈5 s at 25 Hz
    static constexpr float kHoldFadeAlpha         = 0.35f; // dimmed alpha during holding

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModfingerTunerAudioProcessorEditor)
};
