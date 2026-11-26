# ==============================================================================
#  KINETIC - sample-accurate pure MIDI FX sequencer
#
#  Two sibling plug-in targets share one implementation:
#
#    KINETIC_AU    -> AU MIDI Effect, component type 'aumi'
#                     (Logic Pro / MainStage MIDI FX slot)
#    KINETIC_VST3  -> VST3 instrument-class plug-in with a silent stereo output,
#                     so hosts such as Live route MIDI between tracks without
#                     workarounds.
#
#  Deliberately absent: any Standalone target, audio device management, network
#  or telemetry code.
#
#  KINETIC is free software: licensed under the GNU General Public License
#  version 3 (GPLv3), the same terms JUCE 8 is offered under for open-source
#  projects. See LICENSE at the repository root. Building against JUCE under
#  any other JUCE licence does not change the terms that apply to KINETIC.
# ==============================================================================

cmake_minimum_required (VERSION 3.22)

project (KINETIC
    VERSION 0.6.0
    DESCRIPTION "KINETIC - sample-accurate pure MIDI FX sequencer, licensed under the GNU General Public License v3.0 (GPLv3)"
    LANGUAGES C CXX OBJC OBJCXX)

if (NOT APPLE)
    message (FATAL_ERROR "KINETIC targets macOS only (AU MIDI FX + VST3).")
endif()

#-------------------------------------------------------------------------------
# Language and platform defaults
#-------------------------------------------------------------------------------
set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)

# CMake pre-declares these cache entries as empty, so FORCE is required to give them a default.
if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum supported macOS version" FORCE)
endif()

# Native Apple Silicon is the default. Universal is opt-in, via any of:
#   -DKINETIC_UNIVERSAL=ON      cmake --preset universal-release
#   Scripts/build.sh Release universal
option (KINETIC_UNIVERSAL
        "Build a Universal binary (arm64 + x86_64) instead of the native arm64 build"
        OFF)

if (KINETIC_UNIVERSAL)
    set (_kinetic_default_architectures "arm64;x86_64")
else()
    set (_kinetic_default_architectures "arm64")
endif()

if (NOT CMAKE_OSX_ARCHITECTURES)
    set (CMAKE_OSX_ARCHITECTURES "${_kinetic_default_architectures}"
         CACHE STRING "Target architectures" FORCE)
endif()

option (KINETIC_COPY_PLUGINS
        "Copy the built plug-ins into the user plug-in folders as a post-build step"
        OFF)

option (KINETIC_INSTALL_PLUGINS
        "Sign the built AU and VST3 and install them into the user plug-in folders after every build"
        ON)

option (KINETIC_BUILD_TESTS
        "Build the timing-engine test binary"
        ON)

#-------------------------------------------------------------------------------
# Build helpers
#-------------------------------------------------------------------------------
list (APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include (KineticJUCE)     # locates (or fetches) the pinned JUCE checkout
include (KineticPlugin)   # kinetic_add_plugin_target() / kinetic_add_test_target()

#-------------------------------------------------------------------------------
# AU MIDI Effect (aumi)
#
# IS_MIDI_EFFECT makes JUCE emit component type 'aumi' into the bundle's
# AudioComponents entry, which is what puts the unit in Logic's MIDI FX slot.
# The unit still declares one output bus: the AU wrapper uses it to discover the
# host sample rate and never writes audio into it.
#-------------------------------------------------------------------------------
kinetic_add_plugin_target (
    TARGET       KINETIC_AU
    FORMAT       AU
    FORMAT_LABEL "AU MIDI FX (aumi)"
    BUNDLE_ID    "com.kineticmidi.kinetic.au"
    MIDI_FX      1
    EXTRA_ARGUMENTS
        IS_MIDI_EFFECT  TRUE
        AU_MAIN_TYPE    kAudioUnitType_MIDIProcessor
        IS_SYNTH        FALSE
        AU_EXPORT_PREFIX Kinetic
        AU_SANDBOX_SAFE TRUE
)

#-------------------------------------------------------------------------------
# VST3
#
# Instrument-class with MIDI in/out and a stereo output that stays silent. Live,
# Bitwig and Cubase will host it as a MIDI processor and let the generated notes
# be routed onwards, which a strict MIDI-effect VST3 cannot do everywhere.
#-------------------------------------------------------------------------------
kinetic_add_plugin_target (
    TARGET       KINETIC_VST3
    FORMAT       VST3
    FORMAT_LABEL "VST3"
    BUNDLE_ID    "com.kineticmidi.kinetic.vst3"
    MIDI_FX      0
    EXTRA_ARGUMENTS
        IS_SYNTH        TRUE
        IS_MIDI_EFFECT  FALSE
        VST3_CATEGORIES Instrument Synth
)

#-------------------------------------------------------------------------------
# Timing-engine tests (console binary, not a plug-in format)
#-------------------------------------------------------------------------------
if (KINETIC_BUILD_TESTS)
    enable_testing()

    kinetic_add_test_target (
        TARGET  kinetic_timing_tests
        SOURCES ${KINETIC_CORE_SOURCES}
                "${CMAKE_CURRENT_SOURCE_DIR}/Tests/TestMain.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/Tests/TimingEngineTests.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/Tests/SequencerTests.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/Tests/SharedPatternTests.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/Tests/PatternStateTests.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/Tests/PresetLibraryTests.cpp")
endif()

#-------------------------------------------------------------------------------
message (STATUS "KINETIC ${PROJECT_VERSION} | macOS >= ${CMAKE_OSX_DEPLOYMENT_TARGET} | arch: ${CMAKE_OSX_ARCHITECTURES} | universal: ${KINETIC_UNIVERSAL}")
message (STATUS "KINETIC targets: KINETIC_AU (aumi), KINETIC_VST3 | tests: ${KINETIC_BUILD_TESTS} | install plug-ins: ${KINETIC_INSTALL_PLUGINS}")
