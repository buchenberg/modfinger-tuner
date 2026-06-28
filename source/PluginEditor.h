#pragma once

#include "PluginProcessor.h"
#include "dsp/Pitch.h"
#include "ui/TunerPalette.h"
#include "ui/SkinLibrary.h"
#include <memory>

//==============================================================================
/** Flat modern LookAndFeel for Modfinger Tuner — dark + orange theme. */
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
class ModfingerTunerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    ModfingerTunerAudioProcessorEditor (ModfingerTunerAudioProcessor&);
    ~ModfingerTunerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    // Apply a skin by name (looks it up in the runtime library).
    void applySkinByName (const juce::String& name);

    // Show the in-UI skin selector popup (reloads the library first).
    void showSkinMenu();

    // Select a skin by name: persist it and apply it.
    void selectSkin (const juce::String& name);

    // Open a file chooser to import a skin JSON into the user folder.
    void importSkin();

    // Advance width of a single-line string (replaces deprecated Font::getStringWidthFloat).
    static float textWidth (const juce::Font&, const juce::String&);

    ModfingerTunerAudioProcessor& processorRef_;
    TunerLookAndFeel tunerLAF_;

    // Reference pitch label (click to edit)
    juce::Label referenceLabel_;

    // Skin selector (opens a themed popup listing runtime skins)
    juce::TextButton skinButton_;

    // Runtime skin library (bundled defaults + imported user skins)
    SkinLibrary skinLibrary_;
    juce::String activeSkinName_;
    std::unique_ptr<juce::FileChooser> skinFileChooser_;

    // Active colour skin.
    TunerPalette palette_;

    // Smoothed detected frequency; 0.0f means "no signal".
    float smoothedFreq_ = 0.0f;

    // Current readout state: tracking a live pitch, holding (dimmed) the last
    // note, or idle ("listening…").
    enum class DisplayState { tracking, holding, listening };
    DisplayState displayState_ = DisplayState::listening;

    // Ticks left to keep showing the last note after the signal drops, before
    // reverting to "listening…".
    int holdTicksRemaining_ = 0;

    // Eased alpha applied to the readout while holding (1.0 → kHoldFadeAlpha).
    float holdFadeAlpha_ = 1.0f;

    // Display values derived on the message thread (from the atomic frequency).
    pitch::NoteInfo cachedNote_   { "A", 4 };
    double          cachedCents_  = 0.0;
    float           cachedAperiodicity_ = 1.0f;
    float           cachedRefHz_        = 440.0f;

    // Tuning constants
    static constexpr float kConfidentAperiodicity = 0.2f;
    static constexpr int   kHoldTicks             = 125;   // ~5 s @ 25 Hz: how long the last note lingers
    static constexpr float kHoldFadeAlpha         = 0.35f; // dimmed level while holding the last note

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModfingerTunerAudioProcessorEditor)
};
