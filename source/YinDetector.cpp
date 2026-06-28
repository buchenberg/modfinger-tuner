#include "YinDetector.h"
#include <cmath>

//==============================================================================
void YinDetector::prepare (double sampleRate)
{
    fs_ = sampleRate;
    ringBuffer_.assign (bufferSize_, 0.0f);
    diffFunc_.assign (maxPeriod_ + 2, 0.0f);
    writePos_ = 0;
    samplesSinceDetection_ = 0;
    detectedFreq_ = 0.0f;
    aperiodicity_ = 1.0f;
}

void YinDetector::processBlock (const float* mono, int numSamples)
{
    // Accumulate into ring buffer
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

void YinDetector::runDetection()
{
    const int N = bufferSize_;

    // ── Step 1: Difference function d(τ) = Σ(x_j - x_{j+τ})² ──────
    for (int tau = 0; tau <= maxPeriod_; ++tau)
    {
        float sum = 0.0f;
        for (int j = 0; j < N - maxPeriod_; ++j)
        {
            const int idx1 = (writePos_ + j)       % N;
            const int idx2 = (writePos_ + j + tau) % N;
            const float diff = ringBuffer_[static_cast<size_t> (idx1)]
                             - ringBuffer_[static_cast<size_t> (idx2)];
            sum += diff * diff;
        }
        diffFunc_[static_cast<size_t> (tau)] = sum;
    }

    // ── Step 2: Cumulative mean normalised difference ──────────────
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

    // ── Step 3: Absolute threshold ─────────────────────────────────
    int tauEstimate = -1;
    for (int tau = minPeriod_; tau <= maxPeriod_; ++tau)
    {
        if (diffFunc_[static_cast<size_t> (tau)] < yinThreshold_)
        {
            // Walk forward to find the local minimum
            while (tau + 1 <= maxPeriod_
                   && diffFunc_[static_cast<size_t> (tau + 1)] < diffFunc_[static_cast<size_t> (tau)])
                ++tau;
            tauEstimate = tau;
            break;
        }
    }

    if (tauEstimate == -1)
    {
        // No value below threshold — find global minimum
        float minVal = 1.0f;
        for (int tau = minPeriod_; tau <= maxPeriod_; ++tau)
        {
            if (diffFunc_[static_cast<size_t> (tau)] < minVal)
            {
                minVal = diffFunc_[static_cast<size_t> (tau)];
                tauEstimate = tau;
            }
        }
        aperiodicity_ = 1.0f;
    }
    else
    {
        aperiodicity_ = diffFunc_[static_cast<size_t> (tauEstimate)];
    }

    // ── Step 4: Parabolic interpolation ────────────────────────────
    float betterTau = static_cast<float> (tauEstimate);
    if (tauEstimate > minPeriod_ && tauEstimate < maxPeriod_)
    {
        const float s0 = diffFunc_[static_cast<size_t> (tauEstimate - 1)];
        const float s1 = diffFunc_[static_cast<size_t> (tauEstimate)];
        const float s2 = diffFunc_[static_cast<size_t> (tauEstimate + 1)];
        const float denom = 2.0f * (2.0f * s1 - s0 - s2);
        if (std::abs (denom) > 1e-10f)
            betterTau += (s2 - s0) / denom;
    }

    // ── Step 5: Frequency from period ──────────────────────────────
    detectedFreq_ = betterTau > 0.0f ? static_cast<float> (fs_) / betterTau : 0.0f;
}
