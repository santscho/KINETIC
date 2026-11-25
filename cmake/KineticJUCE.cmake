# Resolves the pinned JUCE checkout.
#
# Precedence: a vendored toolchain-less copy at external/JUCE (see
# Scripts/bootstrap.sh) wins, so day-to-day builds never touch the network.
# Otherwise the pinned tag is fetched into the build tree.

set (KINETIC_JUCE_TAG "8.0.15" CACHE STRING "Pinned JUCE release")

set (_kinetic_juce_dir "${CMAKE_CURRENT_LIST_DIR}/../external/JUCE")

if (EXISTS "${_kinetic_juce_dir}/modules/juce_core/juce_core.h")
    message (STATUS "KINETIC: using vendored JUCE at ${_kinetic_juce_dir}")
    add_subdirectory ("${_kinetic_juce_dir}" "${CMAKE_BINARY_DIR}/_juce" EXCLUDE_FROM_ALL)
else()
    include (FetchContent)

    message (STATUS "KINETIC: external/JUCE not found, fetching JUCE ${KINETIC_JUCE_TAG}")

    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${KINETIC_JUCE_TAG}
        GIT_SHALLOW    TRUE)

    FetchContent_MakeAvailable (JUCE)
endif()
