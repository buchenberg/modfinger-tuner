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
    // apvts initialises with the `reference` parameter and its default of 440 Hz.
    // The skin state (`skinName` property) lives on apvts.state directly and is
    // read/written by the editor via getSkinName/setSkinName.
}

ModfingerTunerAudioProcessor::~ModfingerTunerAudioProcessor() = default;

//==============================================================================
//  Parameters
//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout
ModfingerTunerAudioProcessor::createParameterLayout()
{
    using namespace juce;

    // Only one host-exposed parameter: the A4 reference pitch.
    //   range  415–466 Hz  (covers most orchestral/historical tunings)
    //   step   0.1 Hz
    //   default 440 Hz
    //
    // The skin is NOT a host parameter — it lives as a plain property
    // (`skinName`) on apvts.state so the skin set can be dynamic at runtime.
    return {
        std::make_unique<AudioParameterFloat> (
            ParameterID { "reference", 1 }, "Reference",
            NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f,
            AudioParameterFloatAttributes{}.withLabel ("Hz"))
    };
}

//==============================================================================
//  Required AudioProcessor overrides (boilerplate)
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
//  Audio lifecycle
//==============================================================================

void ModfingerTunerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    // Initialise the pYIN ring buffers and Viterbi window for this sample rate.
    pyinDetector_.prepare (sampleRate);

    // Pre-allocate the mono buffer to the host's block size.  Subsequent
    // calls to processBlock resize() to this same size (O(1) no-op).
    monoBuffer_.resize (static_cast<size_t> (samplesPerBlock));
}

void ModfingerTunerAudioProcessor::releaseResources()
{
}

//==============================================================================
//  Bus layout
//==============================================================================

bool ModfingerTunerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // We accept any layout where the main input and output channel sets match.
    // Disabled buses (surround side-chains, etc.) are rejected.
    const auto& mainIn  = layouts.getChannelSet (true,  0);
    const auto& mainOut = layouts.getChannelSet (false, 0);
    return mainIn == mainOut && ! mainIn.isDisabled();
}

//==============================================================================
//  processBlock  — the audio hot path.  Runs on the audio thread.
//
//  This function must be real-time safe (no allocations, no locks, no blocking
//  I/O).  The mono sum and ring-buffer writes are O(numSamples).  pYIN runs
//  every ~1024 samples via internal throttling, so most calls are very cheap.
//==============================================================================
void ModfingerTunerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                  juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;  // prevent FPU slowdown on denormals
    const auto numChannels = buffer.getNumChannels();
    const auto numSamples  = buffer.getNumSamples();

    // Passthrough: copy input channels to corresponding output channels.
    // Extra output channels (e.g. side-chain) are cleared.
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, numSamples);

    // No channels at all → publish silence and bail out.
    if (numChannels < 1)
    {
        displayFreq_.store (0.0f, std::memory_order_relaxed);
        displayAperiodicity_.store (1.0f, std::memory_order_relaxed);
        return;
    }

    // ── Mono sum ──────────────────────────────────────────────────
    // pYIN expects a single channel.  Down-mix to mono.
    monoBuffer_.resize (static_cast<size_t> (numSamples));
    if (numChannels == 1)
    {
        // Single channel — just copy (avoids the loop below).
        std::memcpy (monoBuffer_.data(), buffer.getReadPointer (0),
                     static_cast<size_t> (numSamples) * sizeof (float));
    }
    else
    {
        // Multi-channel — average all input channels to mono.
        for (int i = 0; i < numSamples; ++i)
        {
            float sum = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                sum += buffer.getSample (ch, i);
            monoBuffer_[static_cast<size_t> (i)] = sum / static_cast<float> (numChannels);
        }
    }

    // ── Feed pYIN ─────────────────────────────────────────────────
    pyinDetector_.processBlock (monoBuffer_.data(), numSamples);

    // ── Publish readouts for the UI thread ────────────────────────
    // Only two atomic stores — the entire cross-thread interface fits in
    // two floats.  memory_order_relaxed is sufficient because:
    //   - there is a single writer (this thread) and single reader (editor)
    //   - the two values are independent (no ordering between them)
    displayFreq_.store (pyinDetector_.getFrequency(), std::memory_order_relaxed);
    displayAperiodicity_.store (pyinDetector_.getAperiodicity(), std::memory_order_relaxed);
}

//==============================================================================
//  Editor
//==============================================================================

bool ModfingerTunerAudioProcessor::hasEditor() const  { return true; }

juce::AudioProcessorEditor* ModfingerTunerAudioProcessor::createEditor()
{
    return new ModfingerTunerAudioProcessorEditor (*this);
}

//==============================================================================
//  Plugin state save / restore
//
//  Serialises the whole APVTS ValueTree (parameters + the skinName property
//  stored directly on the state root) to/from an XML binary blob.  Called by
//  the host when saving/loading presets or projects.
//==============================================================================

void ModfingerTunerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // copyState() clones the entire ValueTree — parameter children + the
    // skinName property are included.
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void ModfingerTunerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Parse the XML and replace the entire state ValueTree.  Any extra
    // properties (skinName) from the saved state are restored automatically.
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
//  Plugin entry point  — mandatory JUCE factory function.
//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ModfingerTunerAudioProcessor();
}
