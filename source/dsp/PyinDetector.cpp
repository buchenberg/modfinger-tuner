#include "PyinDetector.h"
#include <cmath>
#include <algorithm>

//==============================================================================
void PyinDetector::prepare (double sampleRate)
{
    fs_ = sampleRate;
    ringBuffer_.assign (bufferSize_, 0.0f);
    linear_.assign     (bufferSize_, 0.0f);
    diffFunc_.assign   (maxPeriod_ + 2, 0.0f);
    writePos_ = 0;
    samplesSinceDetection_ = 0;
    detectedFreq_ = 0.0f;
    detectedAp_   = 1.0f;

    viterbiFrames_.assign (kViterbiWindow, Frame{});
    viterbiWritePos_  = 0;
    viterbiFrameCount_ = 0;
}

//==============================================================================
void PyinDetector::processBlock (const float* mono, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        ringBuffer_[static_cast<size_t> (writePos_)] = mono[i];
        writePos_ = (writePos_ + 1) % bufferSize_;
    }

    samplesSinceDetection_ += numSamples;
    if (samplesSinceDetection_ >= detectInterval_)
    {
        samplesSinceDetection_ = 0;
        runDetection();
    }
}

//==============================================================================
void PyinDetector::runDetection()
{
    const int N = bufferSize_;
    const int W = analysisWindow_;

    // ── Linearize ring buffer ────────────────────────────────────
    for (int i = 0; i < N; ++i)
        linear_[static_cast<size_t> (i)] = ringBuffer_[static_cast<size_t> ((writePos_ + i) % N)];

    const float* const data = linear_.data();

    // ── Step 1: Difference function d(τ), 4-accumulator ILP ──────
    for (int tau = 0; tau <= maxPeriod_; ++tau)
    {
        const float* a = data;
        const float* b = data + tau;

        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        int j = 0;
        for (; j + 4 <= W; j += 4)
        {
            const float d0 = a[j]     - b[j];
            const float d1 = a[j + 1] - b[j + 1];
            const float d2 = a[j + 2] - b[j + 2];
            const float d3 = a[j + 3] - b[j + 3];
            s0 += d0 * d0; s1 += d1 * d1;
            s2 += d2 * d2; s3 += d3 * d3;
        }
        for (; j < W; ++j)
        { const float d = a[j] - b[j]; s0 += d * d; }
        diffFunc_[static_cast<size_t> (tau)] = (s0 + s1) + (s2 + s3);
    }

    // ── Step 2: Cumulative-mean normalised difference ────────────
    diffFunc_[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau <= maxPeriod_; ++tau)
    {
        runningSum += diffFunc_[static_cast<size_t> (tau)];
        if (runningSum > 1e-10f)
            diffFunc_[static_cast<size_t> (tau)] *= static_cast<float> (tau) / runningSum;
        else
            diffFunc_[static_cast<size_t> (tau)] = 1.0f;
    }

    // ── pYIN additions ───────────────────────────────────────────
    extractCandidates();
    runViterbi();
}

//==============================================================================
void PyinDetector::extractCandidates()
{
    candidates_.clear();

    // Local minima of the CMND below the YIN threshold → voiced candidates with
    // parabolic-interpolated periods. Use strict > on neighbours so true minima
    // survive when the CMND underflows to 0 at the dip for pure tones.
    for (int tau = minPeriod_; tau <= maxPeriod_; ++tau)
    {
        const float cmnd = diffFunc_[static_cast<size_t> (tau)];
        if (cmnd >= yinThreshold_)
            continue;
        if (cmnd > diffFunc_[static_cast<size_t> (tau - 1)])
            continue;
        if (cmnd > diffFunc_[static_cast<size_t> (tau + 1)])
            continue;

        // Parabolic interpolation of the period
        const float s0 = diffFunc_[static_cast<size_t> (tau - 1)];
        const float s1 = cmnd;
        const float s2 = diffFunc_[static_cast<size_t> (tau + 1)];
        const float denom = 2.0f * (2.0f * s1 - s0 - s2);
        float betterTau = static_cast<float> (tau);
        if (std::abs (denom) > 1e-10f)
            betterTau += (s2 - s0) / denom;

        Candidate c;
        c.period       = static_cast<double> (betterTau);
        c.aperiodicity = static_cast<double> (s1);
        candidates_.push_back (c);
    }

    // Keep the 5 best candidates, using a tiny bias toward shorter periods so
    // the fundamental breaks ties when all octave aperiodicities ≈ 0.
    constexpr int kMaxCandidates = 5;
    const auto rank = [] (const Candidate& x) { return x.aperiodicity + 1e-4 * x.period; };

    if (static_cast<int> (candidates_.size()) > kMaxCandidates)
    {
        std::nth_element (candidates_.begin(),
                          candidates_.begin() + kMaxCandidates,
                          candidates_.end(),
                          [&] (const Candidate& a, const Candidate& b) { return rank (a) < rank (b); });
        candidates_.resize (kMaxCandidates);
    }

    std::sort (candidates_.begin(), candidates_.end(),
               [&] (const Candidate& a, const Candidate& b) { return rank (a) < rank (b); });
}

//==============================================================================
void PyinDetector::runViterbi()
{
    // ── Build the current frame ───────────────────────────────────
    Frame frame;

    double totalVoicedW = 0.0;
    for (const auto& c : candidates_)
    {
        totalVoicedW += std::max (0.0, 1.0 - c.aperiodicity);
        frame.candidates.push_back (c);
    }

    frame.unvoicedWeight = 0.0;
    if (! candidates_.empty())
    {
        const double bestAp = candidates_.front().aperiodicity;
        frame.unvoicedWeight = std::max (0.0, std::min (1.0, (bestAp - 0.1) / 0.4));
    }
    else
    {
        frame.unvoicedWeight = 1.0;
        detectedFreq_ = 0.0f;
        detectedAp_   = 1.0f;
        return;
    }

    // Write into circular buffer
    viterbiFrames_[static_cast<size_t> (viterbiWritePos_)] = std::move (frame);
    viterbiWritePos_ = (viterbiWritePos_ + 1) % kViterbiWindow;
    if (viterbiFrameCount_ < kViterbiWindow)
        ++viterbiFrameCount_;

    const int N = viterbiFrameCount_;
    const int latestIdx = (viterbiWritePos_ - 1 + kViterbiWindow) % kViterbiWindow;

    // Temporal-order index helper (oldest first)
    auto bufIdx = [&] (int t) { return (viterbiWritePos_ - N + t + kViterbiWindow) % kViterbiWindow; };

    const auto& latestFrame = viterbiFrames_[static_cast<size_t> (latestIdx)];

    // ── Log-observation probabilities ─────────────────────────────
    std::vector<std::vector<double>> logObs (N);
    std::vector<int> stateCounts (N);

    for (int i = 0; i < N; ++i)
    {
        const auto& f = viterbiFrames_[static_cast<size_t> (bufIdx (i))];
        const int S = static_cast<int> (f.candidates.size()) + 1;  // + unvoiced
        stateCounts[i] = S;

        double totalW = 0.0;
        for (const auto& c : f.candidates)
            totalW += std::max (0.0, 1.0 - c.aperiodicity);
        totalW += f.unvoicedWeight;
        const double scale = 1.0 / std::max (totalW, 1e-12);

        logObs[i].resize (S);
        for (int s = 0; s < S - 1; ++s)
        {
            const double w = std::max (0.0, 1.0 - f.candidates[size_t(s)].aperiodicity);
            logObs[i][s] = std::log (std::max (w * scale, 1e-12))
                        - 0.02 * (f.candidates[size_t(s)].period / static_cast<double> (maxPeriod_));
        }
        logObs[i][S - 1] = std::log (std::max (f.unvoicedWeight * scale, 1e-12));
    }

    // ── Log-transition constants ──────────────────────────────────
    constexpr double kSigma         = 0.08;
    constexpr double kTransVToU     = -2.996;  // log(0.05)
    constexpr double kTransUToV     = -3.912;  // log(0.02)
    constexpr double kTransUToU     = -0.105;  // log(0.90)
    constexpr double kInvTwoSigmaSq = 1.0 / (2.0 * kSigma * kSigma);

    // ── Viterbi forward pass ──────────────────────────────────────
    std::vector<std::vector<double>> logDelta (N);
    std::vector<std::vector<int>>    psi       (N);

    // Frame 0
    logDelta[0].assign (stateCounts[0], 0.0);
    psi      [0].assign (stateCounts[0], 0);
    for (int s = 0; s < stateCounts[0]; ++s)
        logDelta[0][s] = logObs[0][s];

    for (int i = 1; i < N; ++i)
    {
        const int S  = stateCounts[i];
        const int Sp = stateCounts[i - 1];
        logDelta[i].resize (S);
        psi[i].resize (S);

        const auto& prev = viterbiFrames_[static_cast<size_t> (bufIdx (i - 1))];
        const auto& curr = viterbiFrames_[static_cast<size_t> (bufIdx (i))];

        for (int s = 0; s < S; ++s)
        {
            double best = -1e30;
            int    bestP = 0;
            const bool sUnv = (s == S - 1);

            for (int sp = 0; sp < Sp; ++sp)
            {
                const bool spUnv = (sp == Sp - 1);
                double trans;

                if (sUnv && spUnv)      trans = kTransUToU;
                else if (sUnv)           trans = kTransVToU;
                else if (spUnv)          trans = kTransUToV;
                else {
                    const double d = std::log (curr.candidates[size_t(s)].period)
                                   - std::log (prev.candidates[size_t(sp)].period);
                    trans = -d * d * kInvTwoSigmaSq;
                }

                const double val = logDelta[i - 1][sp] + trans;
                if (val > best) { best = val; bestP = sp; }
            }
            logDelta[i][s] = best + logObs[i][s];
            psi[i][s] = bestP;
        }
    }

    // ── Backtrack best state at latest frame ──────────────────────
    const int SL = stateCounts[N - 1];
    int bestS = 0;
    double bestVal = -1e30;
    for (int s = 0; s < SL; ++s)
    {
        if (logDelta[N - 1][s] > bestVal)
        { bestVal = logDelta[N - 1][s]; bestS = s; }
    }

    const bool unvoiced = (bestS == SL - 1);
    if (unvoiced)
    {
        detectedFreq_ = 0.0f;
        detectedAp_   = 1.0f;
    }
    else
    {
        const auto& c = latestFrame.candidates[size_t(bestS)];
        detectedFreq_ = static_cast<float> (fs_ / c.period);
        detectedAp_   = static_cast<float> (c.aperiodicity);
    }
}
