#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "dsp/PyinDetector.h"

//==============================================================================
/** Top-level JUCE AudioProcessor for the Modfinger Tuner.

    Audio path (runs on the audio thread):
      1.  Stereo input → mono sum (L+R)/2
      2.  PyinDetector::processBlock  → ring buffer accumulation
      3.  Every 1024 samples:  YIN diff func → CMND → candidates → Viterbi
      4.  Two atomic floats (frequency + aperiodicity) stored for the UI

    UI path (runs on the message thread):
      1.  25 Hz timer reads the atomics
      2.  Exponential smoothing of frequency
      3.  Display-state machine (tracking / holding / listening)
      4.  Note/octave/cents derived via the pure `pitch::*` helpers
      5.  `repaint()` draws the current state

    Only the `reference` frequency (415–466 Hz) is a host parameter.
    The active skin is stored by name in `apvts.state` (not a host param),
    so the runtime skin set can be dynamic.
*/
class ModfingerTunerAudioProcessor : public juce::AudioProcessor
{
public:
    ModfingerTunerAudioProcessor();
    ~ModfingerTunerAudioProcessor() override;

    // ── Audio callbacks ───────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    /** Main audio callback.  Sums stereo to mono, feeds pYIN, publishes
        atomics for the UI.  Runs on the audio thread — keep it fast. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // ── Editor ────────────────────────────────────────────────────
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // ── Plugin metadata ───────────────────────────────────────────
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // ── Program (preset) management — we only have one ─────────────
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    // ── Plugin state persistence ──────────────────────────────────
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── Bus layout — pass-through: same in and out ────────────────
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // ── Value tree / parameters ───────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;

    // ════════════════════════════════════════════════════════════════
    //  Cross-thread readouts  (message thread → editor)
    //
    //  Only these two atomic floats leave the audio thread.  Everything
    //  else (note name, octave, cents) is derived on the message thread
    //  from the atomic frequency using pure helpers — no String races.
    // ════════════════════════════════════════════════════════════════
    float getDisplayFrequency()    const { return displayFreq_.load (std::memory_order_relaxed); }
    float getDisplayAperiodicity() const { return displayAperiodicity_.load (std::memory_order_relaxed); }

    // ── Runtime skin state ────────────────────────────────────────
    // Stored by name in the APVTS ValueTree (not a host parameter) so
    // the skin set can be dynamic — imported skins appear instantly.
    juce::String getSkinName() const { return apvts.state.getProperty ("skinName", "80s Neon").toString(); }
    void setSkinName (const juce::String& name) { apvts.state.setProperty ("skinName", name, nullptr); }

private:
    /** Define the host-exposed parameters (just `reference`). */
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── pYIN detector ─────────────────────────────────────────────
    PyinDetector pyinDetector_;

    // ── Mono accumulation buffer ──────────────────────────────────
    // Resized per block to match the host buffer size (cheap — O(1) if
    // the size hasn't changed).
    std::vector<float> monoBuffer_;

    // ── Cross-thread atomics ──────────────────────────────────────
    // Written on the audio thread in processBlock, read on the message
    // thread in the editor's timerCallback.  std::memory_order_relaxed
    // is correct for a single-reader, single-writer pattern.
    std::atomic<float> displayFreq_        { 0.0f };
    std::atomic<float> displayAperiodicity_ { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModfingerTunerAudioProcessor)
};
