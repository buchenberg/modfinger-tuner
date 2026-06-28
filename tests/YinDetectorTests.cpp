#include <algorithm>
#include <cmath>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "YinDetector.h"

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Generate `numSamples` of a unit-amplitude sine wave at `freqHz`.
std::vector<float> makeSine (double freqHz, double sampleRate, int numSamples)
{
    std::vector<float> out (static_cast<size_t> (numSamples));
    for (int i = 0; i < numSamples; ++i)
        out[static_cast<size_t> (i)] = static_cast<float> (std::sin (2.0 * kPi * freqHz * i / sampleRate));
    return out;
}

// Feed a buffer through the detector in fixed-size chunks.
void feed (YinDetector& det, const std::vector<float>& samples, int chunkSize)
{
    const auto total = static_cast<int> (samples.size());
    for (int i = 0; i < total; i += chunkSize)
        det.processBlock (samples.data() + i, std::min (chunkSize, total - i));
}
} // namespace

TEST_CASE("YIN detects a pure 440 Hz sine", "[yin]")
{
    YinDetector det;
    det.prepare (44100.0);

    feed (det, makeSine (440.0, 44100.0, 4096 * 4), 512);

    REQUIRE(det.getFrequency() > 0.0f);
    REQUIRE_THAT(det.getFrequency(), Catch::Matchers::WithinAbs (440.0, 1.0));
    REQUIRE(det.getAperiodicity() < 0.1f);
}

TEST_CASE("YIN tracks a different pitch", "[yin]")
{
    YinDetector det;
    det.prepare (44100.0);

    feed (det, makeSine (220.0, 44100.0, 4096 * 4), 512);  // A3

    REQUIRE(det.getFrequency() > 0.0f);
    REQUIRE_THAT(det.getFrequency(), Catch::Matchers::WithinAbs (220.0, 1.0));
}

TEST_CASE("YIN reports high aperiodicity for silence", "[yin]")
{
    YinDetector det;
    det.prepare (44100.0);

    std::vector<float> silence (4096 * 2, 0.0f);
    feed (det, silence, 512);

    REQUIRE(det.getAperiodicity() >= 0.3f);
}
