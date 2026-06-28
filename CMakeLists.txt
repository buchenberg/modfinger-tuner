cmake_minimum_required(VERSION 3.22)
project(modfinger-tuner VERSION 1.0.0)

add_subdirectory(../Juce ${CMAKE_BINARY_DIR}/JUCE)

juce_add_plugin(ModfingerTuner
    COMPANY_NAME "Modfinger"
    PLUGIN_MANUFACTURER_CODE Mdfg
    PLUGIN_CODE Md01
    FORMATS AU VST3 Standalone
    PRODUCT_NAME "Modfinger Tuner"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_NAME "Modfinger Tuner")

target_sources(ModfingerTuner
    PRIVATE
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/YinDetector.cpp)

target_compile_definitions(ModfingerTuner
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ModfingerTuner
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ── Unit tests (Catch2) ───────────────────────────────────────────
option(MODFINGER_BUILD_TESTS "Build Modfinger Tuner unit tests" ON)

if (MODFINGER_BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.5.0)
    FetchContent_MakeAvailable(Catch2)

    # Tests cover the pure DSP/logic units only — no JUCE dependency needed.
    add_executable(ModfingerTunerTests
        tests/PitchTests.cpp
        tests/YinDetectorTests.cpp
        source/YinDetector.cpp)

    set_target_properties(ModfingerTunerTests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)

    target_include_directories(ModfingerTunerTests PRIVATE source)
    target_link_libraries(ModfingerTunerTests PRIVATE Catch2::Catch2WithMain)

    include(CTest)
    add_test(NAME ModfingerTunerTests COMMAND ModfingerTunerTests)
endif()
