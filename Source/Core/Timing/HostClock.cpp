#include "HostClock.h"

namespace kinetic
{

void HostClock::prepare (double newSampleRate, int /*maximumBlockSize*/) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    reset();
}

void HostClock::reset() noexcept
{
    block = {};
    haveContinuity = false;
    wasPlaying = false;
    expectedNextPpq = 0.0;
    freeRunPpq = 0.0;
}

const BlockTiming& HostClock::beginBlock (const TransportInput& input, int numSamples) noexcept
{
    BlockTiming timing;
    timing.numSamples = juce::jmax (0, numSamples);
    timing.isPlaying = input.isPlaying;
    timing.bpm = input.bpm > 0.0 ? input.bpm : 120.0;
    timing.ppqPerSample = timing.bpm / (60.0 * sampleRate);
    timing.positionValid = input.hasPosition;
    timing.isFreeRunning = ! input.hasPosition;

    double startPpq = input.ppqPosition;

    if (! input.hasPosition)
    {
        if (input.isPlaying && ! wasPlaying)
        {
            timing.didStart = true;
            freeRunPpq = 0.0;
        }

        startPpq = freeRunPpq;
    }
    else
    {
        if (! wasPlaying && input.isPlaying)
            timing.didStart = true;
        else if (wasPlaying && ! input.isPlaying)
            timing.didStop = true;

        // Only a running transport can leave notes hanging, so continuity is only judged then.
        if (input.isPlaying && wasPlaying && haveContinuity)
        {
            const auto tolerance = timing.ppqPerSample * 2.0 + 1.0e-9;

            if (std::abs (startPpq - expectedNextPpq) > tolerance)
                timing.didJump = true;
        }
    }

    timing.blockStartPpq = startPpq;
    timing.blockEndPpq = startPpq + timing.ppqPerSample * static_cast<double> (timing.numSamples);

    freeRunPpq = timing.blockEndPpq;
    expectedNextPpq = timing.blockEndPpq;
    wasPlaying = input.isPlaying;
    haveContinuity = input.hasPosition;

    block = timing;
    return block;
}

} // namespace kinetic
