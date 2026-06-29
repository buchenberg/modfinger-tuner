#pragma once

#include <vector>
#include <cmath>

//==============================================================================
/** pYIN — probabilistic YIN pitch detector (Mauch & Dixon, 2012).

    Extends the YIN difference function with per-frame candidate extraction and a
    sliding-window HMM / Viterbi decoder for robust, smooth pitch tracking. Keeps
    the same JUCE-free, unit-testable design as YinDetector.
*/
class PyinDetector
{
public:
    void prepare (double sampleRate);
    void processBlock (const float* mono, int numSamples);

    float getFrequency()    const { return detectedFreq_; }
    float getAperiodicity() const { return detectedAp_;   }

private:
    void runDetection();
    void extractCandidates();
    void runViterbi();

    // ── YIN core (duplicated for isolation — see YinDetector) ─────
    double fs_ = 44100.0;

    static constexpr int    bufferSize_     = 4096;
    static constexpr int    minPeriod_      = 22;   // ~2000 Hz max
    static constexpr int    maxPeriod_      = 735;  // ~60 Hz min
    static constexpr float  yinThreshold_   = 0.15f;
    static constexpr int    detectInterval_ = 1024; // samples between detections
    static constexpr int    analysisWindow_ = bufferSize_ / 2;

    std::vector<float> ringBuffer_;
    std::vector<float> linear_;
    int writePos_              = 0;
    int samplesSinceDetection_ = 0;

    std::vector<float> diffFunc_;       // CMND, size maxPeriod_ + 2

    // ── Candidate extraction ──────────────────────────────────────
    struct Candidate
    {
        double period;        // samples
        double aperiodicity;  // YIN CMND at the dip, 0 = perfect periodicity
    };
    std::vector<Candidate> candidates_;

    // ── Viterbi buffer ────────────────────────────────────────────
    struct Frame
    {
        std::vector<Candidate> candidates;   // top-N voiced period candidates
        double unvoicedWeight = 0.0;          // observation weight of the unvoiced state
    };

    static constexpr int kViterbiWindow = 12;   // frames of history

    std::vector<Frame> viterbiFrames_;          // circular buffer, size kViterbiWindow
    int viterbiWritePos_  = 0;
    int viterbiFrameCount_ = 0;                 // frames actually filled

    // ── Output (set by runViterbi) ────────────────────────────────
    float detectedFreq_ = 0.0f;
    float detectedAp_   = 1.0f;
};

//==============================================================================
