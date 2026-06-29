#pragma once

#include <vector>
#include <cmath>

//==============================================================================
/** pYIN  — probabilistic YIN pitch detector  (Mauch & Dixon, 2012).

    Three-stage pipeline per detection:
      1. Classic YIN difference function & cumulative-mean normalised difference
      2. Candidate extraction — up to 5 voiced-period hypotheses per frame, ranked
      3. HMM / Viterbi decoder — a 12‑frame sliding window picks the most likely
         pitch sequence, smoothing continuity and rejecting spurious octave candidates.

    The class is intentionally JUCE-free (uses only `<vector>` and `<cmath>`) so it
    slots directly into a Catch2 unit-test binary without linking any JUCE modules.

    Thread-safety: not thread-safe.  Called exclusively from the audio thread via
    `processBlock`; the resulting atomic floats are read on the message thread.

    Detection timing:  YIN runs every `detectInterval_` = 1024 samples ≈ every
    23 ms at 44.1 kHz (≈43 Hz), well above the editor's 25 Hz UI refresh rate.
*/
class PyinDetector
{
public:
    /** Reset buffers for a sample rate. Call from `prepareToPlay`. */
    void prepare (double sampleRate);

    /** Feed a mono block. Detection runs automatically every ~1024 samples. */
    void processBlock (const float* mono, int numSamples);

    /** Viterbi-decoded frequency in Hz (0.0 = unvoiced / no pitch). */
    float getFrequency()    const { return detectedFreq_; }

    /** Periodicity score of the winning candidate (0 = perfect, 1 = noise). */
    float getAperiodicity() const { return detectedAp_;   }

private:
    // ── Private pipeline ──────────────────────────────────────────
    void runDetection();        // YIN steps 1–2 → extract → Viterbi
    void extractCandidates();   // find & rank voiced period hypotheses
    void runViterbi();          // HMM forward pass over the frame window

    // ── YIN core ─────────────────────────────────────────────────
    double fs_ = 44100.0;                                 // sample rate

    static constexpr int bufferSize_      = 4096;         // ring-buffer length
    static constexpr int minPeriod_       = 22;           // ≈ 2000 Hz max detectable
    static constexpr int maxPeriod_       = 735;          // ≈  60 Hz min detectable
    static constexpr float yinThreshold_  = 0.15f;        // absolute-threshold for CMND
    static constexpr int detectInterval_  = 1024;         // samples between YIN runs
    static constexpr int analysisWindow_  = bufferSize_ / 2;  // diff-function window

    std::vector<float> ringBuffer_;        // length bufferSize_
    std::vector<float> linear_;            // contiguous unrolled copy of ringBuffer_
    int writePos_              = 0;        // next ring-buffer write index
    int samplesSinceDetection_ = 0;        // throttle counter

    /** The cumulative-mean normalised difference (CMND), d'(τ).
        Allocated as maxPeriod_ + 2 so parabolic interpolation at τ = maxPeriod_
        can safely read its right neighbour.  Element 0 is forced to 1.0. */
    std::vector<float> diffFunc_;

    // ── Candidate extraction ──────────────────────────────────────
    struct Candidate
    {
        double period;          // interpolated period in samples
        double aperiodicity;    // CMND value at the dip (0 = perfect match)
    };
    std::vector<Candidate> candidates_;   // up to 5, sorted best-first

    // ── Viterbi state ─────────────────────────────────────────────
    /** One detection frame as seen by the HMM. */
    struct Frame
    {
        std::vector<Candidate> candidates;   // voiced period hypotheses
        double unvoicedWeight = 0.0;          // observation weight of the
    };                                        //   unvoiced (noise) state

    static constexpr int kViterbiWindow = 12; // frames in the sliding window

    std::vector<Frame> viterbiFrames_;        // circular buffer, length kViterbiWindow
    int viterbiWritePos_   = 0;               // next write slot
    int viterbiFrameCount_ = 0;               // frames filled so far (≤ kViterbiWindow)

    // ── Output ────────────────────────────────────────────────────
    float detectedFreq_ = 0.0f;     // Hz; 0.0 means unvoiced / no pitch
    float detectedAp_   = 1.0f;     // periodicity score
};

//==============================================================================
