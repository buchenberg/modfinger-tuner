#pragma once

#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "dsp/YinDetector.h"

//==============================================================================
class ModfingerTunerAudioProcessor : public juce::AudioProcessor
{
public:
    ModfingerTunerAudioProcessor();
    ~ModfingerTunerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    //==============================================================================
    juce::AudioProcessorValueTreeState apvts;

    // Thread-safe readouts for the editor (called on the message thread).
    // Only plain atomics cross the audio/UI boundary; note & cents are derived
    // on the UI side from the frequency via the pure pitch helpers.
    float getDisplayFrequency()    const { return displayFreq_.load (std::memory_order_relaxed); }
    float getDisplayAperiodicity() const { return displayAperiodicity_.load (std::memory_order_relaxed); }

    // Active skin name, stored in plugin state (not a host parameter) so the
    // runtime skin set can be dynamic. Defaults to "80s Neon".
    juce::String getSkinName() const { return apvts.state.getProperty ("skinName", "80s Neon").toString(); }
    void setSkinName (const juce::String& name) { apvts.state.setProperty ("skinName", name, nullptr); }

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    YinDetector yinDetector_;

    // Mono accumulation buffer (averages stereo → mono per block)
    std::vector<float> monoBuffer_;

    // Written on the audio thread, read on the message thread.
    std::atomic<float> displayFreq_        { 0.0f };
    std::atomic<float> displayAperiodicity_ { 1.0f };

    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModfingerTunerAudioProcessor)
};
