cmake_minimum_required(VERSION 3.22)

# These must be set before project() on macOS.
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build universal binaries")
endif()

project(RhythmForge VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(RHYTHMFORGE_COPY_PLUGIN "Copy the built VST3 into the system/user VST3 folder after building" OFF)
option(RHYTHMFORGE_BUILD_TESTS  "Build the headless smoke test (RhythmForgeSmokeTest)" OFF)
set(RHYTHMFORGE_JUCE_DIR "" CACHE PATH "Path to an existing JUCE checkout (optional)")
set(RHYTHMFORGE_JUCE_TAG "8.0.6" CACHE STRING "JUCE git tag to download when no local JUCE is given")

# ---- get JUCE ---------------------------------------------------------------------------------
if(RHYTHMFORGE_JUCE_DIR)
    add_subdirectory("${RHYTHMFORGE_JUCE_DIR}" JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${RHYTHMFORGE_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- sources ----------------------------------------------------------------------------------
set(RHYTHMFORGE_SOURCES
    Source/RhythmTypes.h
    Source/Pattern.h
    Source/PatternGenerator.h
    Source/PatternGenerator.cpp
    Source/DrumSynth.h
    Source/RhythmEngine.h
    Source/RhythmEngine.cpp
    Source/WavExporter.h
    Source/WavExporter.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp)

# ---- the plugin -------------------------------------------------------------------------------
juce_add_plugin(RhythmForge
    COMPANY_NAME                "RhythmForge Audio"
    PLUGIN_MANUFACTURER_CODE    Rfrg
    PLUGIN_CODE                 Rfg1
    PRODUCT_NAME                "RhythmForge"
    FORMATS                     VST3 Standalone
    VST3_CATEGORIES             Instrument Drum
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${RHYTHMFORGE_COPY_PLUGIN})

target_sources(RhythmForge PRIVATE ${RHYTHMFORGE_SOURCES})
target_include_directories(RhythmForge PRIVATE Source)

target_compile_definitions(RhythmForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(RhythmForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---- optional headless smoke test ---------------------------------------------------------------
if(RHYTHMFORGE_BUILD_TESTS)
    juce_add_console_app(RhythmForgeSmokeTest PRODUCT_NAME "RhythmForgeSmokeTest")
    target_sources(RhythmForgeSmokeTest PRIVATE ${RHYTHMFORGE_SOURCES} Tests/SmokeTest.cpp)
    target_include_directories(RhythmForgeSmokeTest PRIVATE Source)
    target_compile_definitions(RhythmForgeSmokeTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(RhythmForgeSmokeTest
        PRIVATE juce::juce_audio_utils juce::juce_audio_processors
        PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
