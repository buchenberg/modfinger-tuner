cmake_minimum_required(VERSION 3.22)
project(modfinger-tuner VERSION 1.0.0)

# ── Auto-initialize the JUCE submodule ────────────────────────────
# Lets a plain `git clone` (no --recurse-submodules) still configure. Only runs
# when git is available and JUCE/ hasn't been populated yet.
option(MODFINGER_AUTOINIT_SUBMODULES "Initialize git submodules at configure time" ON)
if (MODFINGER_AUTOINIT_SUBMODULES)
    find_package(Git QUIET)
    if (GIT_FOUND
            AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.gitmodules"
            AND NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
        message(STATUS "Initializing JUCE submodule...")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive -- JUCE
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _submod_result)
        if (NOT _submod_result EQUAL 0)
            message(FATAL_ERROR
                "Initializing the JUCE submodule failed (exit ${_submod_result}). "
                "Run 'git submodule update --init --recursive' manually.")
        endif()
    endif()
endif()

add_subdirectory(JUCE ${CMAKE_BINARY_DIR}/JUCE)

# ── Bundled default skins (compiled into the binary) ─────────────
juce_add_binary_data(ModfingerSkinsData
    SOURCES skins/dark.json skins/eighties_neon.json)

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
        source/dsp/PyinDetector.cpp
        source/ui/SkinManager.cpp
        source/ui/SkinLibrary.cpp)

target_include_directories(ModfingerTuner PRIVATE source)

target_compile_definitions(ModfingerTuner
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(ModfingerTuner
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ModfingerSkinsData
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ── Tests ─────────────────────────────────────────────────────────
option(MODFINGER_BUILD_TESTS      "Build Catch2 unit tests (pure DSP/pitch logic)" ON)
option(MODFINGER_BUILD_JUCE_TESTS "Build JUCE UnitTest target (parameter & state coverage)" ON)

if (MODFINGER_BUILD_TESTS OR MODFINGER_BUILD_JUCE_TESTS)
    include(CTest)
endif()

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
        tests/PyinDetectorTests.cpp
        source/dsp/PyinDetector.cpp)

    set_target_properties(ModfingerTunerTests PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON)

    target_include_directories(ModfingerTunerTests PRIVATE source)
    target_link_libraries(ModfingerTunerTests PRIVATE Catch2::Catch2WithMain)

    add_test(NAME ModfingerTunerTests COMMAND ModfingerTunerTests)
endif()

if (MODFINGER_BUILD_JUCE_TESTS)
    # Covers the JUCE-coupled seams (parameter range/default, state round-trip).
    # Only needs JUCE — no network dependency — so it builds offline too.
    juce_add_console_app(ModfingerTunerJuceTests)

    target_sources(ModfingerTunerJuceTests PRIVATE
        tests/ProcessorTests.cpp
        source/PluginProcessor.cpp
        source/PluginEditor.cpp
        source/dsp/PyinDetector.cpp
        source/ui/SkinManager.cpp
        source/ui/SkinLibrary.cpp)

    target_compile_definitions(ModfingerTunerJuceTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_include_directories(ModfingerTunerJuceTests PRIVATE source)

    target_link_libraries(ModfingerTunerJuceTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        ModfingerSkinsData)

    add_test(NAME ModfingerTunerJuceTests COMMAND ModfingerTunerJuceTests)
endif()
