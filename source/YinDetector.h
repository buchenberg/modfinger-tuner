#pragma once

#include <vector>

//==============================================================================
/** YIN-based monophonic pitch detector.
    Based on: De Cheveigne & Kawahara (2002), "YIN, a fundamental frequency
    estimator for speech and music."

    Pure DSP unit (no JUCE dependency) so it can be unit-tested in isolation.
*/
class YinDetector
{
public:
    void prepare (double sampleRate);
    void processBlock (const float* mono, int numSamples);

    float getFrequency()    const { return detectedFreq_; }
    float getAperiodicity() const { return aperiodicity_; }

private:
    void runDetection();

    double fs_ = 44100.0;

    static constexpr int bufferSize_      = 4096;
    static constexpr int minPeriod_       = 22;   // ~2000 Hz max
    static constexpr int maxPeriod_       = 735;  // ~60 Hz min
    static constexpr float yinThreshold_  = 0.15f;
    static constexpr int detectInterval_  = 512;  // samples between YIN runs

    std::vector<float> ringBuffer_;
    int writePos_             = 0;
    int samplesSinceDetection_ = 0;

    std::vector<float> diffFunc_;  // difference function, size maxPeriod_ + 2

    float detectedFreq_  = 0.0f;
    float aperiodicity_  = 1.0f;
};
