#include <cstdio>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "PluginProcessor.h"

//==============================================================================
// JUCE-side coverage for the JUCE-coupled seams of the processor: the parameter
// range/default and the state save/restore round-trip. The pure DSP and pitch
// math are covered separately by the Catch2 target.
class ProcessorTests : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("ModfingerTunerAudioProcessor", "processor") {}

    void runTest() override
    {
        using namespace juce;

        beginTest ("Reference parameter range and default");
        {
            ModfingerTunerAudioProcessor proc;
            auto* param = dynamic_cast<AudioParameterFloat*> (proc.apvts.getParameter ("reference"));

            expect (param != nullptr, "reference parameter missing");
            if (param != nullptr)
            {
                const auto range = param->getNormalisableRange();
                expectWithinAbsoluteError (range.start, 415.0f, 1.0e-4f, "range start");
                expectWithinAbsoluteError (range.end,   466.0f, 1.0e-4f, "range end");
                expectWithinAbsoluteError (param->get(), 440.0f, 1.0e-4f, "default value");
            }
        }

        beginTest ("State save/restore round-trip preserves the reference value");
        {
            ModfingerTunerAudioProcessor original;
            const float target = 442.0f;
            if (auto* p = original.apvts.getParameter ("reference"))
                p->setValueNotifyingHost (p->convertTo0to1 (target));

            const float before = original.apvts.getRawParameterValue ("reference")->load();

            MemoryBlock block;
            original.getStateInformation (block);

            ModfingerTunerAudioProcessor restored;
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));

            const float after = restored.apvts.getRawParameterValue ("reference")->load();
            expectWithinAbsoluteError (after, before, 0.15f, "round-trip value");
        }

        beginTest ("Skin name persists across state save/restore");
        {
            ModfingerTunerAudioProcessor proc;
            expectEquals (proc.getSkinName(), juce::String ("80s Neon"), "default skin name");

            proc.setSkinName ("Dark");

            MemoryBlock block;
            proc.getStateInformation (block);

            ModfingerTunerAudioProcessor restored;
            restored.setStateInformation (block.getData(), static_cast<int> (block.getSize()));
            expectEquals (restored.getSkinName(), juce::String ("Dark"), "restored skin name");
        }
    }
};

// Auto-registers with the JUCE UnitTest runner.
static ProcessorTests processorTestsInstance;

//==============================================================================
int main()
{
    juce::ScopedJuceInitialiser_GUI initialiser;   // provides the MessageManager the runner needs

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        if (auto* r = runner.getResult (i))
            failures += r->failures;

    if (failures > 0)
        std::printf ("JUCE unit tests: %d failure(s)\n", failures);
    else
        std::printf ("JUCE unit tests: all passed\n");

    return failures == 0 ? 0 : 1;
}
