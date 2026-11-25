#pragma once

#include <juce_core/juce_core.h>

#include "Concurrency/UiCommand.h"

namespace kinetic
{

/**
    Host transport handed to the clock once per block.

    Deliberately a plain struct rather than juce::AudioPlayHead::PositionInfo: the timing core
    stays independent of the host API, which keeps it testable and keeps processBlock the only
    place that knows about AudioPlayHead.
*/
struct TransportInput
{
    bool   hasPosition = false;   // host reported a valid PPQ position
    bool   isPlaying = false;
    bool   isLooping = false;
    double bpm = 120.0;
    double ppqPosition = 0.0;     // PPQ at the first sample of this block
    double loopStartPpq = 0.0;
    double loopEndPpq = 0.0;
};

/** Timing of one audio block, derived from host PPQ. */
struct BlockTiming
{
    bool   isPlaying = false;
    bool   positionValid = false;
    bool   isFreeRunning = false;   // no host position: internal clock drives the block
    bool   didStart = false;        // transport went stopped -> playing
    bool   didStop = false;         // transport went playing -> stopped
    bool   didJump = false;         // locate / loop wrap / discontinuous host position

    double bpm = 120.0;
    double ppqPerSample = 0.0;
    double blockStartPpq = 0.0;
    double blockEndPpq = 0.0;
    int    numSamples = 0;

    /** Converts a host PPQ position to a sample offset inside this block, clamped to the block. */
    int sampleOffsetForPpq (double ppq) const noexcept
    {
        if (ppqPerSample <= 0.0 || numSamples <= 0)
            return 0;

        const auto raw = (ppq - blockStartPpq) / ppqPerSample;

        if (raw <= 0.0)
            return 0;

        if (raw >= static_cast<double> (numSamples))
            return numSamples - 1;

        return static_cast<int> (raw);
    }

    double ppqForSampleOffset (int sampleOffset) const noexcept
    {
        return blockStartPpq + ppqPerSample * static_cast<double> (sampleOffset);
    }

    /** True when `ppq` falls past the end of this block (caller should defer the event). */
    bool isAfterBlock (double ppq) const noexcept
    {
        return ppq > blockEndPpq;
    }
};

/**
    Derives sample-accurate block timing from host PPQ.

    Sample offsets come from the host's PPQ position at the start of the block plus the tempo
    and sample rate, so nothing is accumulated across blocks on the host path and jitter cannot
    creep in. Continuity between blocks is used only to *detect* discontinuities - a backwards
    jump (loop wrap), a forward locate, or a transport restart - which the engine turns into a
    note flush.

    When the host provides no transport at all the clock free-runs so the sequencer still
    advances, but it never pretends the position came from the host.
*/
class HostClock
{
public:
    HostClock() = default;

    void prepare (double newSampleRate, int maximumBlockSize) noexcept;
    void reset() noexcept;

    const BlockTiming& beginBlock (const TransportInput& input, int numSamples) noexcept;

    const BlockTiming& currentBlock() const noexcept { return block; }
    double getSampleRate() const noexcept { return sampleRate; }

private:
    double sampleRate = 48000.0;
    BlockTiming block {};

    bool   haveContinuity = false;
    bool   wasPlaying = false;
    double expectedNextPpq = 0.0;
    double freeRunPpq = 0.0;

    JUCE_DECLARE_NON_COPYABLE (HostClock)
};

} // namespace kinetic
