cmake_minimum_required(VERSION 3.22)
project(NoiseShaper VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is fetched automatically; alternatively set JUCE_SOURCE_DIR to a
# local checkout and replace this block with add_subdirectory(${JUCE_SOURCE_DIR} JUCE).
include(FetchContent)
FetchContent_Declare(JUCE
    URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.6.tar.gz)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(NoiseShaper
    COMPANY_NAME "OnurUzun"
    PLUGIN_MANUFACTURER_CODE Ouzn
    PLUGIN_CODE Nshp
    FORMATS VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Instrument Synth
    PRODUCT_NAME "NoiseShaper")

target_sources(NoiseShaper PRIVATE
    Source/PluginProcessor.cpp)

target_include_directories(NoiseShaper PRIVATE Source)

target_compile_definitions(NoiseShaper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(NoiseShaper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Optional DSP self-tests (no audio hardware or GUI needed):
#   cmake -B build -DNOISESHAPER_BUILD_TESTS=ON && cmake --build build --target NoiseShaperTests
option(NOISESHAPER_BUILD_TESTS "Build the DSP self-test console app" OFF)
if(NOISESHAPER_BUILD_TESTS)
    juce_add_console_app(NoiseShaperTests PRODUCT_NAME "NoiseShaperTests")
    target_sources(NoiseShaperTests PRIVATE Tests/DspTests.cpp)
    target_compile_definitions(NoiseShaperTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_FLAC=0
        JUCE_USE_OGGVORBIS=0
        JUCE_USE_MP3AUDIOFORMAT=0
        JUCE_USE_WINDOWS_MEDIA_FORMAT=0)
    target_link_libraries(NoiseShaperTests PRIVATE
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
