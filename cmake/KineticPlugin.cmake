# Shared configuration for KINETIC's plug-in targets.
#
# Both formats compile the same source tree; only the metadata and the
# per-target feature switches differ. Anything that must behave differently
# between the aumi AU and the VST3 build is driven by the compile definitions
# set here (KINETIC_IS_AU_MIDI_FX / KINETIC_FORMAT_NAME).

# Host-independent timing core: no JUCE GUI, no AudioPlayHead, so the test
# binary can compile exactly the code the plug-ins run.
set (KINETIC_CORE_SOURCES
    "${PROJECT_SOURCE_DIR}/Source/Core/KineticConfig.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Concurrency/SpscQueue.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Concurrency/UiCommand.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Timing/HostClock.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Timing/HostClock.cpp"
    "${PROJECT_SOURCE_DIR}/Source/Core/Timing/MidiEventSink.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Timing/NoteTracker.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Timing/TransportEngine.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Sequencer/SequencerTypes.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Sequencer/SequenceMath.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Sequencer/Sequencer.h"
    "${PROJECT_SOURCE_DIR}/Source/Core/Sequencer/Sequencer.cpp"
    "${PROJECT_SOURCE_DIR}/Source/Core/Sequencer/SharedPattern.h")

set (KINETIC_PLUGIN_SOURCES
    "${PROJECT_SOURCE_DIR}/Source/Core/Timing/TransportEngine.h"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/PluginProcessor.h"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/PluginProcessor.cpp"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/PluginEditor.h"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/PluginEditor.cpp"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/PatternEditor.h"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/PatternEditor.cpp"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/StepGrid.h"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/StepGrid.cpp"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/Theme.h"
    "${PROJECT_SOURCE_DIR}/Source/Plugin/PluginEntry.cpp")

# kinetic_add_plugin_target (
#     TARGET        <cmake target name>
#     FORMAT        AU | VST3                 (a single format per target)
#     FORMAT_LABEL  "<human readable format>"  (shown in the editor badge)
#     BUNDLE_ID     "<reverse dns id>"
#     MIDI_FX       0 | 1                      (1 == aumi, no audio I/O)
#     EXTRA_ARGUMENTS <KEY value>...           (forwarded to juce_add_plugin)
# )
function (kinetic_add_plugin_target)
    cmake_parse_arguments (PARSE_ARGV 0 ARG
        ""
        "TARGET;FORMAT;FORMAT_LABEL;BUNDLE_ID;MIDI_FX"
        "EXTRA_ARGUMENTS")

    if (NOT ARG_TARGET OR NOT ARG_FORMAT OR NOT ARG_BUNDLE_ID)
        message (FATAL_ERROR
            "kinetic_add_plugin_target() requires TARGET, FORMAT and BUNDLE_ID")
    endif()

    juce_add_plugin (${ARG_TARGET}
        PRODUCT_NAME                "KINETIC"
        COMPANY_NAME                "Kinetic MIDI"
        COMPANY_WEBSITE             "https://kineticmidi.com"
        BUNDLE_ID                   "${ARG_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE    Kntc
        PLUGIN_CODE                 Kntk
        FORMATS                     ${ARG_FORMAT}
        VERSION                     ${PROJECT_VERSION}
        DESCRIPTION                 "Sample-accurate pure MIDI FX sequencer"
        NEEDS_MIDI_INPUT            TRUE
        NEEDS_MIDI_OUTPUT           TRUE
        NEEDS_WEB_BROWSER           FALSE
        NEEDS_CURL                  FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        COPY_PLUGIN_AFTER_BUILD     ${KINETIC_COPY_PLUGINS}
        ${ARG_EXTRA_ARGUMENTS})

    target_sources (${ARG_TARGET} PRIVATE ${KINETIC_CORE_SOURCES} ${KINETIC_PLUGIN_SOURCES})

    # Source/ for "Core/..." includes, Source/Core/ for the timing core's own peers.
    target_include_directories (${ARG_TARGET} PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        "${PROJECT_SOURCE_DIR}/Source/Core")

    target_compile_definitions (${ARG_TARGET} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        KINETIC_VERSION_STRING="${PROJECT_VERSION}"
        KINETIC_FORMAT_NAME="${ARG_FORMAT_LABEL}"
        KINETIC_IS_AU_MIDI_FX=${ARG_MIDI_FX})

    target_link_libraries (${ARG_TARGET} PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endfunction()

# kinetic_add_test_target (TARGET <name> SOURCES <files>...)
#
# A console binary, not a plug-in format: it exists purely to run the timing-core
# unit tests through CTest.
function (kinetic_add_test_target)
    cmake_parse_arguments (PARSE_ARGV 0 ARG "" "TARGET" "SOURCES")

    if (NOT ARG_TARGET OR NOT ARG_SOURCES)
        message (FATAL_ERROR "kinetic_add_test_target() requires TARGET and SOURCES")
    endif()

    juce_add_console_app (${ARG_TARGET} PRODUCT_NAME "${ARG_TARGET}")

    target_sources (${ARG_TARGET} PRIVATE ${ARG_SOURCES})

    target_include_directories (${ARG_TARGET} PRIVATE
        "${PROJECT_SOURCE_DIR}/Source"
        "${PROJECT_SOURCE_DIR}/Source/Core")

    target_compile_definitions (${ARG_TARGET} PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        KINETIC_VERSION_STRING="${PROJECT_VERSION}"
        KINETIC_FORMAT_NAME="tests"
        KINETIC_IS_AU_MIDI_FX=0)

    target_link_libraries (${ARG_TARGET} PRIVATE
        juce::juce_audio_basics
        juce::juce_core
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_test (NAME ${ARG_TARGET} COMMAND ${ARG_TARGET})
endfunction()
