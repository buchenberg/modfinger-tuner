#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/PyinDetector.h"

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::vector<float> makeSine (double freqHz, double sampleRate, int numSamples)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * freqHz * i / sampleRate));
    return out;
}

void feed (PyinDetector& det, const std::vector<float>& samples, int chunkSize)
{
    const auto total = static_cast<int> (samples.size());
    for (int i = 0; i < total; i += chunkSize)
        det.processBlock (samples.data() + i, std::min (chunkSize, total - i));
}
}

TEST_CASE ("pYIN detects a pure 440 Hz sine after Viterbi warm-up", "[pyin]")
{
    PyinDetector det;
    det.prepare (44100.0);

    // ~16 frames fills the Viterbi window (12) with margin
    feed (det, makeSine (440.0, 44100.0, 4096 * 4), 512);

    REQUIRE (det.getFrequency() > 0.0f);
    REQUIRE_THAT (det.getFrequency(), Catch::Matchers::WithinAbs (440.0, 2.0));
    REQUIRE (det.getAperiodicity() < 0.2f);
}

TEST_CASE ("pYIN detects 220 Hz", "[pyin]")
{
    PyinDetector det;
    det.prepare (44100.0);

    feed (det, makeSine (220.0, 44100.0, 4096 * 4), 512);

    REQUIRE (det.getFrequency() > 0.0f);
    REQUIRE_THAT (det.getFrequency(), Catch::Matchers::WithinAbs (220.0, 2.0));
}

TEST_CASE ("pYIN reports silence as unvoiced", "[pyin]")
{
    PyinDetector det;
    det.prepare (44100.0);

    std::vector<float> silence (4096 * 4, 0.0f);
    feed (det, silence, 512);

    // After many frames of silence, should stay unvoiced (freq=0 or high aperiodicity)
    REQUIRE (det.getAperiodicity() >= 0.3f);
}

TEST_CASE ("pYIN tracks a frequency step change", "[pyin]")
{
    PyinDetector det;
    det.prepare (44100.0);

    auto sine440 = makeSine (440.0, 44100.0, 4096 * 3);    // ~12 frames, fills Viterbi window
    auto sine880 = makeSine (880.0, 44100.0, 4096 * 4);    // ~16 frames, pushes 440 Hz out of window

    feed (det, sine440, 512);    // warm up on 440 Hz
    REQUIRE (det.getFrequency() > 0.0f);

    feed (det, sine880, 512);    // switch to 880 Hz — Viterbi needs enough frames to override the prior

    REQUIRE (det.getFrequency() > 0.0f);
    // Allow generous tolerance during the transition window
    REQUIRE_THAT (det.getFrequency(), Catch::Matchers::WithinAbs (880.0, 20.0));
}
