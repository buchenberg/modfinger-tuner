#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/Pitch.h"

using namespace pitch;

TEST_CASE("A4 maps correctly", "[pitch]")
{
    REQUIRE(midiNoteFromFrequency (440.0, 440.0) == 69);

    const auto a4 = noteInfoFromFrequency (440.0, 440.0);
    REQUIRE(std::string (a4.name)   == "A");
    REQUIRE(a4.octave == 4);
}

TEST_CASE("Middle C is C4", "[pitch]")
{
    REQUIRE(midiNoteFromFrequency (261.6256, 440.0) == 60);

    const auto c4 = noteInfoFromFrequency (261.6256, 440.0);
    REQUIRE(std::string (c4.name) == "C");
    REQUIRE(c4.octave == 4);
}

TEST_CASE("Sharps resolve to the sharp name", "[pitch]")
{
    // A#4 / Bb4 ≈ 466.16 Hz
    const auto note = noteInfoFromFrequency (466.164, 440.0);
    REQUIRE(std::string (note.name) == "A#");
    REQUIRE(note.octave == 4);
}

TEST_CASE("Cents offset tracks detuning", "[pitch]")
{
    REQUIRE_THAT(centsFromFrequency (440.0, 440.0), Catch::Matchers::WithinAbs (0.0, 0.01));
    REQUIRE_THAT(centsFromFrequency (441.0, 440.0), Catch::Matchers::WithinAbs (3.93, 0.10));  // sharp
    REQUIRE_THAT(centsFromFrequency (438.0, 440.0), Catch::Matchers::WithinAbs (-7.88, 0.10)); // flat
}

TEST_CASE("Reference shift transposes without changing the tone", "[pitch]")
{
    // A 440 Hz tone against a 442 Hz reference reads as A4 but slightly flat.
    REQUIRE(midiNoteFromFrequency (440.0, 442.0) == 69);
    REQUIRE_THAT(centsFromFrequency (440.0, 442.0), Catch::Matchers::WithinAbs (-7.85, 0.10));
}

TEST_CASE("Note index wraps at the octave boundary", "[pitch]")
{
    const auto cMinus1 = noteInfoFromMidi (0);
    REQUIRE(std::string (cMinus1.name) == "C");
    REQUIRE(cMinus1.octave == -1);

    const auto bMinus1 = noteInfoFromMidi (11);
    REQUIRE(std::string (bMinus1.name) == "B");
    REQUIRE(bMinus1.octave == -1);

    const auto b4 = noteInfoFromMidi (71);   // A4(69) + 2
    REQUIRE(std::string (b4.name) == "B");
    REQUIRE(b4.octave == 4);
}
