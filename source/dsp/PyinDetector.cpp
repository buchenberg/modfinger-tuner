//==============================================================================
//  PyinDetector  — probabilistic YIN pitch detector
//  Mauch & Dixon (2012)  http://www.eecs.qmul.ac.uk/~simond/pub/2012/MauchDixon-PyIN.pdf
//
//  Three-stage pipeline per detection (every 1024 samples ≈ 43 Hz @ 44.1 kHz):
//    1.  Classic YIN — difference function → cumulative-mean normalised difference
//    2.  Candidate extraction — up to 5 voiced-period hypotheses ranked by score
//    3.  Viterbi decoding — 12-frame sliding-window HMM picks the most likely pitch
//        sequence, smoothing continuity and rejecting spurious octave candidates.
//==============================================================================

#include "PyinDetector.h"
#include <cmath>
#include <algorithm>

//==============================================================================
//  prepare  — initialise buffers and reset state.
//  Called once when the host changes sample rate.
//==============================================================================
void PyinDetector::prepare (double sampleRate)
{
    fs_ = sampleRate;

    // The ring buffer accumulates incoming samples in a circular window.
    ringBuffer_.assign (bufferSize_, 0.0f);

    // `linear_` is a contiguous copy of the ring buffer made once per detection so
    // the YIN difference-function sweep can run over flat indices (cache-friendly and
    // amenable to auto-vectorisation).
    linear_.assign (bufferSize_, 0.0f);

    // The difference function d(τ) and its normalised version (CMND) share this
    // vector.  Element 0 is overwritten with 1.0 during CMND normalisation; the
    // extra trailing slot (+2) gives safe access for parabolic interpolation at
    // τ = maxPeriod_.
    diffFunc_.assign (maxPeriod_ + 2, 0.0f);

    writePos_              = 0;
    samplesSinceDetection_ = 0;
    detectedFreq_ = 0.0f;
    detectedAp_   = 1.0f;        // start unvoiced — no pitch yet

    // Circular Viterbi frame buffer (12 frames of HMM history).
    viterbiFrames_.assign (kViterbiWindow, Frame{});
    viterbiWritePos_  = 0;
    viterbiFrameCount_ = 0;

    // Reserve the candidate working buffer up front (prepareToPlay runs off
    // the audio thread) so extractCandidates never reallocates during
    // processBlock.  Collection is capped at kCandidateCapacity to honour this.
    candidates_.clear();
    candidates_.reserve (static_cast<size_t> (kCandidateCapacity));

    // Zero the Viterbi scratch once; the forward pass overwrites every cell
    // it reads each detection.
    logObs_.fill (0.0);
    logDelta_.fill (0.0);
    stateCounts_.fill (0);
}

//==============================================================================
//  processBlock  — accumulate mono samples into the ring buffer and trigger
//  detection approximately every `detectInterval_` samples (1024 at 44.1 kHz
//  ≈ 43 Hz).  The host calls this on the audio thread every buffer cycle.
//==============================================================================
void PyinDetector::processBlock (const float* mono, int numSamples)
{
    // ── Ring-buffer accumulation ─────────────────────────────────
    // writePos_ always points to the next free slot, i.e. the oldest sample.
    for (int i = 0; i < numSamples; ++i)
    {
        ringBuffer_[static_cast<size_t> (writePos_)] = mono[i];
        writePos_ = (writePos_ + 1) % bufferSize_;
    }

    // ── Throttle detection to ~43 Hz ─────────────────────────────
    // Running YIN at the full audio rate would burn CPU for no benefit —
    // the tuner display updates at 25 Hz, so ~43 Hz is more than fast enough.
    samplesSinceDetection_ += numSamples;
    if (samplesSinceDetection_ >= detectInterval_)
    {
        samplesSinceDetection_ = 0;
        runDetection();
    }
}

//==============================================================================
//  runDetection  — classic YIN difference function + CMND, exactly as in the
//  original De Cheveigné & Kawahara (2002) paper, followed by the pYIN
//  extensions (candidate extraction → Viterbi decoding).
//==============================================================================
void PyinDetector::runDetection()
{
    const int N = bufferSize_;    // 4096
    const int W = analysisWindow_; // 2048  — half the buffer, ≥2 periods at the
                                     //         60 Hz detection floor

    // ── Linearise the ring buffer ─────────────────────────────────
    // Unroll the circular buffer into a contiguous, time-ordered array (oldest
    // first) so the difference-function sweep below runs over flat indices.
    // writePos_ is the next write position ≡ the oldest sample in the buffer.
    for (int i = 0; i < N; ++i)
        linear_[static_cast<size_t> (i)] = ringBuffer_[static_cast<size_t> ((writePos_ + i) % N)];

    const float* const data = linear_.data();

    // ====================================================================
    //  Step 1 — Difference function d(τ)  (YIN eq. 7)
    //
    //  d(τ) = Σ_{j=0}^{W-1} (x[j] − x[j+τ])²
    //
    //  If the signal is periodic with period τ, x[j] ≈ x[j+τ] and d(τ) ≈ 0.
    //  We compute this for every plausible period τ in [0, maxPeriod_] using
    //  a 4-accumulator loop that breaks the serial dependency chain for better
    //  instruction-level parallelism on superscalar CPUs.
    // ====================================================================
    for (int tau = 0; tau <= maxPeriod_; ++tau)
    {
        const float* a = data;        // alias for x[j]
        const float* b = data + tau;  // alias for x[j+τ]

        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        int j = 0;

        // VECTORISED 4-way unrolled main loop.
        for (; j + 4 <= W; j += 4)
        {
            const float d0 = a[j]     - b[j];
            const float d1 = a[j + 1] - b[j + 1];
            const float d2 = a[j + 2] - b[j + 2];
            const float d3 = a[j + 3] - b[j + 3];
            s0 += d0 * d0; s1 += d1 * d1;
            s2 += d2 * d2; s3 += d3 * d3;
        }

        // Scalar tail for the remaining samples.
        for (; j < W; ++j)
        { const float d = a[j] - b[j]; s0 += d * d; }

        // Combine the four partial sums.
        diffFunc_[static_cast<size_t> (tau)] = (s0 + s1) + (s2 + s3);
    }

    // ====================================================================
    //  Step 2 — Cumulative-mean normalised difference d'(τ)  (YIN eq. 8)
    //
    //  d'(τ) =         1.0                           for τ = 0
    //        = d(τ) / [ (1/τ) · Σ_{j=1}^{τ} d(j) ]  for τ > 0
    //
    //  This normalisation makes the dip shape comparable across different τ
    //  and prevents the "octave error" where τ=0 always has d=0.
    //
    //  d'(τ) ≈ 0  →  strong periodicity at period τ
    //  d'(τ) ≈ 1  →  no periodicity (noise or aperiodic signal)
    // ====================================================================
    diffFunc_[0] = 1.0f;           // by definition
    float runningSum = 0.0f;      // cumulative sum Σ d(j)

    for (int tau = 1; tau <= maxPeriod_; ++tau)
    {
        runningSum += diffFunc_[static_cast<size_t> (tau)];

        // Guard against divide-by-zero on near-silent signals.
        if (runningSum > 1e-10f)
            diffFunc_[static_cast<size_t> (tau)] *= static_cast<float> (tau) / runningSum;
        else
            diffFunc_[static_cast<size_t> (tau)] = 1.0f;   // effectively no periodicity
    }

    // ====================================================================
    //  pYIN extensions
    //  Now diffFunc_ holds CMND values — feed them to the probabilistic stage.
    // ====================================================================
    extractCandidates();
    runViterbi();
}

//==============================================================================
//  extractCandidates  — scan the CMND for local minima below the YIN threshold.
//  Each is a plausible voiced period.  Keep up to 5, ranked by aperiodicity with
//  a tiny bias toward shorter periods so the fundamental wins ties for pure tones
//  (where all octave harmonics score equally well).
//==============================================================================
void PyinDetector::extractCandidates()
{
    candidates_.clear();

    // Walk the CMND from the shortest period (highest frequency) to the longest.
    // A "local minimum" is a point where the CMND is lower than both its neighbours.
    // We use strict > (not >=) so true minima survive when the CMND underflows to 0
    // at the dip for pure sine tones (neighbours may also be ≈0 due to underflow,
    // so >= would incorrectly skip the minimum).
    for (int tau = minPeriod_; tau <= maxPeriod_; ++tau)
    {
        const float cmnd = diffFunc_[static_cast<size_t> (tau)];

        // Skip if above the YIN absolute threshold (no clear periodicity).
        if (cmnd >= yinThreshold_)
            continue;

        // Must be a local minimum.
        if (cmnd > diffFunc_[static_cast<size_t> (tau - 1)])
            continue;
        if (cmnd > diffFunc_[static_cast<size_t> (tau + 1)])
            continue;

        // ── Parabolic interpolation ─────────────────────────────────
        // The true period is unlikely to land exactly on an integer sample
        // boundary.  Fit a parabola through (τ-1, s0), (τ, s1), (τ+1, s2)
        // and find its vertex to get sub-sample precision.
        //
        //   betterTau = τ  +  (s2 − s0) / [ 2 · (2·s1 − s0 − s2) ]
        //
        const float s0 = diffFunc_[static_cast<size_t> (tau - 1)];
        const float s1 = cmnd;
        const float s2 = diffFunc_[static_cast<size_t> (tau + 1)];
        const float denom = 2.0f * (2.0f * s1 - s0 - s2);

        float betterTau = static_cast<float> (tau);
        if (std::abs (denom) > 1e-10f)        // guard near-zero denominator
            betterTau += (s2 - s0) / denom;

        // Bound collection at the reserved capacity (see prepare) so the audio
        // thread never reallocates.  Dips below the 0.15 threshold are sparse
        // on real audio, so kCandidateCapacity (32) is comfortably ample.
        if (static_cast<int> (candidates_.size()) >= kCandidateCapacity)
            break;

        Candidate c;
        c.period       = static_cast<double> (betterTau);   // sub-sample period
        c.aperiodicity = static_cast<double> (s1);           // raw CMND at the dip
        candidates_.push_back (c);
    }

    // ── Rank and keep the 5 best candidates ─────────────────────────
    // Rank = aperiodicity + 1e-4 · period
    //
    // The tiny `1e-4 · period` bias favours shorter periods (higher frequencies).
    // For a pure sine all octave candidates have aperiodicity ≈ 0, so without
    // this bias the sort would be ambiguous.  The bias of ~0.01 (period 100) vs
    // ~0.04 (period 400) is negligible next to real aperiodicity differences
    // (~0.05–0.50) but breaks ties toward the fundamental.
    const auto rank = [] (const Candidate& x) { return x.aperiodicity + 1e-4 * x.period; };

    if (static_cast<int> (candidates_.size()) > kMaxCandidates)
    {
        // nth_element partitions so the first 5 are the best — cheaper than
        // a full sort when many candidates exist.
        std::nth_element (candidates_.begin(),
                          candidates_.begin() + kMaxCandidates,
                          candidates_.end(),
                          [&] (const Candidate& a, const Candidate& b) { return rank (a) < rank (b); });
        candidates_.resize (kMaxCandidates);
    }

    // Full sort on the (at most 5) survivors so candidates_[0] is the best.
    std::sort (candidates_.begin(), candidates_.end(),
               [&] (const Candidate& a, const Candidate& b) { return rank (a) < rank (b); });
}

//==============================================================================
//  runViterbi  — Hidden Markov Model / Viterbi decoder.
//
//  Model structure (per frame):
//
//    States  = { voiced candidate 0, voiced candidate 1, …, unvoiced }
//    The voiced states correspond to the periods found in extractCandidates().
//    The unvoiced state absorbs frames with no clear periodicity.
//
//  Observation probabilities:
//
//    P(observation | state = voiced candidate i)  ∝  1 − aperiodicity_i
//    P(observation | state = unvoiced)             ∝  unvoicedWeight
//
//    where unvoicedWeight grows linearly with the best candidate's aperiodicity
//    (an aperiodic signal is more likely unvoiced).  A tiny fundamental-period
//    prior (−0.02 · period/maxPeriod_) is added to the log-observation of
//    voiced states so that the shortest period wins ties for pure tones.
//
//  Transition probabilities (log domain, held constant):
//
//    voiced → voiced (same pitch)                — cost 0 (the period stayed the same)
//    voiced → voiced (different pitch)           — Gaussian penalty on
//                                                    Δ log(period), σ = 0.08
//    voiced → unvoiced                           — fixed cost log(0.05)
//    unvoiced → voiced                           — fixed cost log(0.02)
//    unvoiced → unvoiced                         — fixed cost log(0.90)
//
//    The Gaussian penalty makes the model *prefer* smooth pitch trajectories:
//    staying at the same note is free, a small vibrato costs a little, and
//    a wild octave jump costs a lot.  The asymmetric voiced↔unvoiced costs
//    make it easier to stay silent than to hear a note from nowhere, but
//    easy to keep hearing a note once one is present.
//
//  Viterbi algorithm:
//
//    Given a sliding window of the last N=12 frames, Viterbi finds the single
//    most probable state path (the pitch sequence that best explains the
//    observations).  It is a dynamic-programming forward pass that, for each
//    frame and each state, computes:
//
//      logDelta[i][s] = logObs[i][s]  +  max_{sp} { logDelta[i-1][sp] + transLog(sp,s) }
//
//    After the forward pass, we pick the state with the highest logDelta at
//    the latest frame and output its frequency.  The path is not backtracked
//    — only the final state decision matters for the tuner readout.
//==============================================================================
void PyinDetector::runViterbi()
{
    // ── Build the current frame (from this detection's candidates) ────
    // Copy into the frame's fixed-size array (no allocation).
    Frame frame;
    const int numC = static_cast<int> (candidates_.size());
    for (int i = 0; i < numC; ++i)
        frame.candidates[i] = candidates_[static_cast<size_t> (i)];
    frame.numCandidates = numC;

    // ── Unvoiced weight ──────────────────────────────────────────────
    // Computed from the best candidate's aperiodicity.  Maps 0.1 → 0.0
    // (very periodic → definitely voiced), 0.5 → 1.0 (noisy → unvoiced).
    frame.unvoicedWeight = 0.0;
    if (numC > 0)
    {
        const double bestAp = candidates_.front().aperiodicity;
        frame.unvoicedWeight = std::max (0.0, std::min (1.0, (bestAp - 0.1) / 0.4));
    }
    else
    {
        // No candidates at all — the frame is certainly unvoiced.  Short-
        // circuit the Viterbi (the output is already 0 Hz / ap=1).  The frame
        // is intentionally NOT pushed into the window, so a silent stretch
        // leaves the HMM history intact for a clean resume when sound returns.
        frame.unvoicedWeight = 1.0;
        detectedFreq_ = 0.0f;
        detectedAp_   = 1.0f;
        return;
    }

    // ── Push frame into the circular Viterbi buffer ──────────────────
    viterbiFrames_[static_cast<size_t> (viterbiWritePos_)] = frame;
    viterbiWritePos_ = (viterbiWritePos_ + 1) % kViterbiWindow;
    if (viterbiFrameCount_ < kViterbiWindow)
        ++viterbiFrameCount_;

    const int N = viterbiFrameCount_;                             // frames available
    const int latestIdx = (viterbiWritePos_ - 1 + kViterbiWindow) % kViterbiWindow;

    // Helper: map a temporal frame index t (0 = oldest, N−1 = newest) to
    // the physical buffer index, accounting for the circular wrap.
    auto bufIdx = [&] (int t) { return (viterbiWritePos_ - N + t + kViterbiWindow) % kViterbiWindow; };

    const auto& latestFrame = viterbiFrames_[static_cast<size_t> (latestIdx)];

    // ====================================================================
    //  Log-observation probabilities  (pre-allocated scratch)
    //  logObs_[i][s] = log( P(observation | state) ), flattened via cellIndex.
    // ====================================================================
    for (int i = 0; i < N; ++i)
    {
        const auto& f = viterbiFrames_[static_cast<size_t> (bufIdx (i))];
        const int S = f.numCandidates + 1;                       // voiced + unvoiced
        stateCounts_[static_cast<size_t> (i)] = S;

        // Total weight across all states (for normalisation).
        double totalW = 0.0;
        for (int s = 0; s < f.numCandidates; ++s)
            totalW += std::max (0.0, 1.0 - f.candidates[s].aperiodicity);
        totalW += f.unvoicedWeight;
        const double scale = 1.0 / std::max (totalW, 1e-12);

        // Voiced states (s = 0 … S−2).
        for (int s = 0; s < S - 1; ++s)
        {
            const double w = std::max (0.0, 1.0 - f.candidates[s].aperiodicity);
            logObs_[cellIndex (i, s)] = std::log (std::max (w * scale, 1e-12))
                                        // ↓ fundamental-period prior: shorter periods win ties
                                      - 0.02 * (f.candidates[s].period / static_cast<double> (maxPeriod_));
        }

        // Unvoiced state (s = S−1).
        logObs_[cellIndex (i, S - 1)] = std::log (std::max (f.unvoicedWeight * scale, 1e-12));
    }

    // ====================================================================
    //  Log-transition constants
    //
    //  kSigma = 0.08  — log-period standard deviation of the Gaussian
    //                    transition kernel (~1.4 semitones at 1σ).
    //
    //  kTransVToU = log(0.05)   voiced → unvoiced is expensive
    //  kTransUToV = log(0.02)   unvoiced → voiced is very expensive
    //  kTransUToU = log(0.90)   staying unvoiced is cheap
    // ====================================================================
    constexpr double kSigma         = 0.08;
    constexpr double kTransVToU     = -2.996;  // log(0.05)
    constexpr double kTransUToV     = -3.912;  // log(0.02)
    constexpr double kTransUToU     = -0.105;  // log(0.90)
    constexpr double kInvTwoSigmaSq = 1.0 / (2.0 * kSigma * kSigma);

    // ====================================================================
    //  Viterbi forward pass  (pre-allocated scratch: logDelta_[i][s])
    //
    //  logDelta_[i][s] = best log-probability of being in state s at frame i.
    //  No backtracking pointers are stored — only the winning state at the
    //  latest frame is needed, so the path is never reconstructed.
    // ====================================================================

    // ── Frame 0: initialise with observation log-probs only ──────────
    // No transition from a previous frame.
    for (int s = 0; s < stateCounts_[0]; ++s)
        logDelta_[cellIndex (0, s)] = logObs_[cellIndex (0, s)];

    // ── Frames 1 … N−1: dynamic programming recursion ───────────────
    for (int i = 1; i < N; ++i)
    {
        const int S  = stateCounts_[i];       // states in this frame
        const int Sp = stateCounts_[i - 1];   // states in the previous frame

        const auto& prev = viterbiFrames_[static_cast<size_t> (bufIdx (i - 1))];
        const auto& curr = viterbiFrames_[static_cast<size_t> (bufIdx (i))];

        for (int s = 0; s < S; ++s)
        {
            double best = -1e30;     // log(0) — impossibly bad
            const bool sUnv = (s == S - 1);     // is current state unvoiced?

            // Try every previous state as a predecessor.
            for (int sp = 0; sp < Sp; ++sp)
            {
                const bool spUnv = (sp == Sp - 1);
                double trans;

                // ── Transition log-probability ────────────────────────
                if (sUnv && spUnv)
                    trans = kTransUToU;               // stay unvoiced
                else if (sUnv)
                    trans = kTransVToU;               // voiced → unvoiced
                else if (spUnv)
                    trans = kTransUToV;               // unvoiced → voiced
                else
                {
                    // Both voiced — Gaussian penalty on log-period change
                    const double d = std::log (curr.candidates[s].period)
                                   - std::log (prev.candidates[sp].period);
                    trans = -d * d * kInvTwoSigmaSq;
                }

                const double val = logDelta_[cellIndex (i - 1, sp)] + trans;
                if (val > best) best = val;
            }

            // Best predecessor found — store the DP cell.
            logDelta_[cellIndex (i, s)] = best + logObs_[cellIndex (i, s)];
        }
    }

    // ====================================================================
    //  Decision — pick the best state at the latest frame.
    //  Only the winning state matters; the full path is irrelevant because
    //  Viterbi re-runs from scratch every detection.
    // ====================================================================
    const int SL = stateCounts_[N - 1];
    int bestS = 0;
    double bestVal = -1e30;
    for (int s = 0; s < SL; ++s)
    {
        if (logDelta_[cellIndex (N - 1, s)] > bestVal)
        { bestVal = logDelta_[cellIndex (N - 1, s)]; bestS = s; }
    }

    // ── Produce the output ─────────────────────────────────────────
    const bool unvoiced = (bestS == SL - 1);
    if (unvoiced)
    {
        detectedFreq_ = 0.0f;      // 0 Hz ≡ "no pitch"
        detectedAp_   = 1.0f;      // maximum aperiodicity
    }
    else
    {
        const auto& c = latestFrame.candidates[bestS];
        detectedFreq_ = static_cast<float> (fs_ / c.period);
        detectedAp_   = static_cast<float> (c.aperiodicity);
    }
}
