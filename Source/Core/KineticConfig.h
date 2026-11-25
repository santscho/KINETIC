#pragma once

#include <juce_core/juce_core.h>

// These are supplied by the CMake target that is compiling this translation unit, so that the
// single source tree can produce both the AU MIDI FX (aumi) and the VST3 plug-ins with
// format-appropriate metadata.
#ifndef KINETIC_VERSION_STRING
 #define KINETIC_VERSION_STRING "0.0.0"
#endif

#ifndef KINETIC_FORMAT_NAME
 #define KINETIC_FORMAT_NAME "unspecified"
#endif

#ifndef KINETIC_IS_AU_MIDI_FX
 #define KINETIC_IS_AU_MIDI_FX 0
#endif

namespace kinetic
{

inline constexpr const char* productName        = "KINETIC";
inline constexpr const char* companyName        = "Kinetic MIDI";
inline constexpr const char* manufacturerCode   = "Kntc";   // Audio Unit manufacturer ID
inline constexpr const char* pluginCode         = "Knt1";   // Subtype (also the VST3 code seed)
inline constexpr const char* auComponentType    = "aumi";   // Audio Unit MIDI Effect

inline constexpr const char* version    = KINETIC_VERSION_STRING;
inline constexpr const char* formatName = KINETIC_FORMAT_NAME;

// True only for the AU target: such a unit declares no audio input and a single, never-written
// output bus that the host uses to hand the plug-in its sample rate.
inline constexpr bool isAuMidiFx = KINETIC_IS_AU_MIDI_FX != 0;

} // namespace kinetic
