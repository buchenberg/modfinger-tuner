#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cstring>

//==============================================================================
//  ModfingerTunerAudioProcessor
//==============================================================================

ModfingerTunerAudioProcessor::ModfingerTunerAudioProcessor()
    : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
}

ModfingerTunerAudioProcessor::~ModfingerTunerAudioProcessor() = default;

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ModfingerTunerAudioProcessor::createParameterLayout()
{
    using namespace juce;

    return {
        std::make_unique<AudioParameterFloat> (
            ParameterID { "reference", 1 }, "Reference",
            NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f,
            AudioParameterFloatAttributes{}.withLabel ("Hz"))
    };
}

//==============================================================================
const juce::String ModfingerTunerAudioProcessor::getName() const   { return "Modfinger Tuner"; }
bool ModfingerTunerAudioProcessor::acceptsMidi() const             { return false; }
bool ModfingerTunerAudioProcessor::producesMidi() const            { return false; }
bool ModfingerTunerAudioProcessor::isMidiEffect() const            { return false; }
double ModfingerTunerAudioProcessor::getTailLengthSeconds() const  { return 0.0; }

int ModfingerTunerAudioProcessor::getNumPrograms()                  { return 1; }
int ModfingerTunerAudioProcessor::getCurrentProgram()               { return 0; }
void ModfingerTunerAudioProcessor::setCurrentProgram (int)          {}
const juce::String ModfingerTunerAudioProcessor::getProgramName (int) { return {}; }
void ModfingerTunerAudioProcessor::changeProgramName (int, const juce::String&) {}

//==============================================================================
void ModfingerTunerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);
    pyinDetector_.prepare (sampleRate);
    monoBuffer_.resize (static_cast<size_t> (samplesPerBlock));
}

void ModfingerTunerAudioProcessor::releaseResources()
{
}

bool ModfingerTunerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getChannelSet (true,  0);
    const auto& mainOut = layouts.getChannelSet (false, 0);
    return mainIn == mainOut && ! mainIn.isDisabled();
}

//==============================================================================
void ModfingerTunerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();

    // Passthrough: copy input to output
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    if (numChannels < 1)
    {
        displayFreq_.store (0.0f, std::memory_order_relaxed);
        displayAperiodicity_.store (1.0f, std::memory_order_relaxed);
        return;
    }

    // Sum to mono for pitch detection
    monoBuffer_.resize (static_cast<size_t> (numSamples));
    if (numChannels == 1)
    {
        std::memcpy (monoBuffer_.data(), buffer.getReadPointer (0),
                     static_cast<size_t> (numSamples) * sizeof (float));
    }
    else
    {
        const auto* left  = buffer.getReadPointer (0);
        const auto* right = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
            monoBuffer_[static_cast<size_t> (i)] = (left[i] + right[i]) * 0.5f;
    }

    pyinDetector_.processBlock (monoBuffer_.data(), numSamples);

    // Push lightweight readouts to the UI thread.
    displayFreq_.store (pyinDetector_.getFrequency(), std::memory_order_relaxed);
    displayAperiodicity_.store (pyinDetector_.getAperiodicity(), std::memory_order_relaxed);
}

//==============================================================================
bool ModfingerTunerAudioProcessor::hasEditor() const  { return true; }

juce::AudioProcessorEditor* ModfingerTunerAudioProcessor::createEditor()
{
    return new ModfingerTunerAudioProcessorEditor (*this);
}

//==============================================================================
void ModfingerTunerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ModfingerTunerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ModfingerTunerAudioProcessor();
}
