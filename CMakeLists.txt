cmake_minimum_required(VERSION 3.22)

# macOS: build universal binaries. (FL Studio's plugin scanner has been reported to reject
# arm64-only JUCE builds, so we always include x86_64 as well.)
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "")

project(VoxMidi VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(VOXMIDI_BUILD_PLUGINS "Build the JUCE plugins (needs JUCE, fetched automatically)" ON)
option(VOXMIDI_BUILD_TESTS   "Build the DSP engine tests" ON)

# ------------------------------------------------------------------ engine tests (no JUCE needed)
if(VOXMIDI_BUILD_TESTS)
    enable_testing()
    add_executable(voxmidi_tests tests/engine_tests.cpp)
    target_include_directories(voxmidi_tests PRIVATE engine)
    if(NOT MSVC)
        target_compile_options(voxmidi_tests PRIVATE -O2)
    endif()
    add_test(NAME engine COMMAND voxmidi_tests)
endif()

if(NOT VOXMIDI_BUILD_PLUGINS)
    return()
endif()

# ------------------------------------------------------------------ JUCE (open source, AGPLv3)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(juce)
endif()

# ------------------------------------------------------------------ the plugins
# Two flavours share all sources:
#   VoxMidi     - an INSTRUMENT. FL Studio documents MIDI routing out of instruments (generators), and an
#                 instrument gets no audio input there, so this one reads the microphone by itself.
#   VoxMidi FX  - an EFFECT. Takes audio from the host (e.g. a mixer insert). It only helps in hosts that
#                 pass MIDI out of effect plugins; whether FL Studio does is unconfirmed - try it.
function(voxmidi_add_plugin target product code is_synth is_fx default_direct)
    set(formats VST3 Standalone)
    if(APPLE AND NOT is_fx)
        list(APPEND formats AU)
    endif()

    if(is_synth)
        set(vst3_cat "Instrument")
    else()
        set(vst3_cat "Fx")
    endif()

    juce_add_plugin(${target}
        COMPANY_NAME                "VoxMidi"
        BUNDLE_ID                   "com.voxmidi.${target}"
        PLUGIN_MANUFACTURER_CODE    Vxmi
        PLUGIN_CODE                 ${code}
        FORMATS                     ${formats}
        PRODUCT_NAME                "${product}"
        IS_SYNTH                    ${is_synth}
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           TRUE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        VST3_CATEGORIES             ${vst3_cat}
        AU_MAIN_TYPE                kAudioUnitType_MusicDevice
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT  "VoxMidi listens to your microphone to turn your voice into MIDI.")

    target_sources(${target} PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp)

    target_include_directories(${target} PRIVATE engine plugin)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        VOXMIDI_FX=${is_fx}
        VOXMIDI_DEFAULT_DIRECT=${default_direct})

    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags)
endfunction()

voxmidi_add_plugin(VoxMidi   "VoxMidi"    Vxm1 TRUE  0 1)
voxmidi_add_plugin(VoxMidiFX "VoxMidi FX" Vxm2 FALSE 1 0)
