#pragma once

#include <cstdint>

#include "SpscQueue.h"

namespace kinetic
{

enum class UiCommandType : std::uint8_t
{
    allNotesOff,   // panic: release every tracked note and reset controllers
    resetClock     // drop timing continuity and release notes (e.g. after a bad locate)
};

struct UiCommand
{
    UiCommandType type = UiCommandType::allNotesOff;

    /** 0 == every channel, otherwise 1-16. */
    std::uint8_t channel = 0;
};

/** UI thread produces, audio thread drains once per block. */
using UiCommandQueue = SpscQueue<UiCommand, 256>;

} // namespace kinetic
