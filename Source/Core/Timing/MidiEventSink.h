#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace kinetic
{

/**
    Allocation-free, block-local MIDI event list.

    Everything the engine emits is a 1-3 byte channel message, so events are stored as packed
    bytes rather than juce::MidiMessage. Events are collected in whatever order the generator
    produces them and sorted by (sample offset, insertion order) on flush, which keeps output
    deterministic without allocating inside the audio callback.
*/
class MidiEventSink
{
public:
    MidiEventSink() = default;

    static constexpr int maxEvents = 1024;
    static constexpr int maxMessageBytes = 3;

    /** Starts a new block. `numSamples` bounds the sample offsets accepted by add(). */
    void reset (int numSamples) noexcept
    {
        numEvents = 0;
        dropped = 0;
        nextSequence = 0;
        blockSize = juce::jmax (1, numSamples);
    }

    bool add (const juce::MidiMessage& message, int sampleOffset) noexcept
    {
        const auto numBytes = message.getRawDataSize();

        if (numBytes <= 0 || numBytes > maxMessageBytes || numEvents >= maxEvents)
        {
            ++dropped;
            return false;
        }

        auto& event = events[static_cast<std::size_t> (numEvents++)];
        event.offset = juce::jlimit (0, blockSize - 1, sampleOffset);
        event.sequence = nextSequence++;
        event.numBytes = numBytes;
        std::memcpy (event.bytes, message.getRawData(), static_cast<std::size_t> (numBytes));
        return true;
    }

    bool addNoteOn (int channel, int note, float velocity, int sampleOffset) noexcept
    {
        return add (juce::MidiMessage::noteOn (channel, note, velocity), sampleOffset);
    }

    bool addNoteOff (int channel, int note, int sampleOffset) noexcept
    {
        return add (juce::MidiMessage::noteOff (channel, note), sampleOffset);
    }

    bool addController (int channel, int controller, int value, int sampleOffset) noexcept
    {
        return add (juce::MidiMessage::controllerEvent (channel, controller, value), sampleOffset);
    }

    int size() const noexcept { return numEvents; }
    int droppedCount() const noexcept { return dropped; }

    /** Appends the block's events to `destination` in ascending sample order. */
    void flushTo (juce::MidiBuffer& destination) noexcept
    {
        std::sort (events.begin(), events.begin() + numEvents,
                   [] (const Event& a, const Event& b) noexcept
                   {
                       if (a.offset != b.offset)
                           return a.offset < b.offset;

                       return a.sequence < b.sequence;
                   });

        for (int i = 0; i < numEvents; ++i)
        {
            const auto& event = events[static_cast<std::size_t> (i)];
            destination.addEvent (event.bytes, event.numBytes, event.offset);
        }
    }

private:
    struct Event
    {
        int offset = 0;
        int sequence = 0;
        int numBytes = 0;
        juce::uint8 bytes[maxMessageBytes] {};
    };

    std::array<Event, maxEvents> events {};
    int numEvents = 0;
    int dropped = 0;
    int nextSequence = 0;
    int blockSize = 1;

    JUCE_DECLARE_NON_COPYABLE (MidiEventSink)
};

} // namespace kinetic
