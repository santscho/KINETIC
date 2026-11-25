#pragma once

#include "HostClock.h"
#include "MidiEventSink.h"
#include "NoteTracker.h"

#include "Sequencer/Sequencer.h"

#include <atomic>

namespace kinetic
{

/**
    The timing engine driven from processBlock().

    One call to beginBlock() per audio block:
      - drains UI commands from the lock-free queue,
      - derives sample-accurate block timing from host PPQ,
      - flushes every held note whenever the timeline breaks (stop, locate, loop wrap).

    The sequencer (Step 3) then schedules its own events into events(), and
    flushGeneratedMidi() appends them to the host buffer in sample order.
*/
class TransportEngine
{
public:
    TransportEngine() = default;

    void prepare (double sampleRate, int maximumBlockSize) noexcept
    {
        clock.prepare (sampleRate, maximumBlockSize);
        noteTracker.prepare (sampleRate);
        sequencerEngine.prepare (sampleRate);
        sink.reset (maximumBlockSize);
        commands.clear();
    }

    void reset() noexcept
    {
        clock.reset();
        noteTracker.reset();
        sequencerEngine.reset();
        sink.reset (1);
        commands.clear();
    }

    /** Safe from the message thread. Returns false if the queue is momentarily full. */
    bool postCommand (const UiCommand& command) noexcept { return commands.push (command); }

    /** Call once at the top of processBlock, before any generation. */
    const BlockTiming& beginBlock (const TransportInput& input, int numSamples) noexcept
    {
        sink.reset (numSamples);

        UiCommand command;

        while (commands.pop (command))
        {
            switch (command.type)
            {
                case UiCommandType::allNotesOff:
                    noteTracker.panic (sink, 0, command.channel);
                    break;

                case UiCommandType::resetClock:
                    noteTracker.panic (sink, 0, 0);
                    clock.reset();
                    break;
            }
        }

        block = clock.beginBlock (input, numSamples);

        // Any break in the host timeline means notes we started may never be stopped by the
        // sequencer's own schedule, so flush them at the very start of the block.
        if (block.didStop || block.didJump)
            noteTracker.panic (sink, 0);

        // Generation happens after the flush, so a stop or locate can never leave a lane
        // pointing at stale note-off state.
        sequencerEngine.processBlock (block, sink, noteTracker);

        return block;
    }

    const BlockTiming& currentBlock() const noexcept { return block; }
    MidiEventSink& events() noexcept { return sink; }
    NoteTracker& notes() noexcept { return noteTracker; }
    Sequencer& sequencer() noexcept { return sequencerEngine; }
    const Sequencer& sequencer() const noexcept { return sequencerEngine; }

    /** Audio thread only: adopt the pattern the editor published, by reference. */
    void usePattern (const Pattern* published) noexcept { sequencerEngine.usePattern (published); }

    void flushGeneratedMidi (juce::MidiBuffer& destination) noexcept
    {
        sink.flushTo (destination);
    }

    int activeNoteCount() const noexcept { return noteTracker.activeCount(); }

private:
    HostClock clock;
    NoteTracker noteTracker;
    MidiEventSink sink;
    Sequencer sequencerEngine;
    UiCommandQueue commands;
    BlockTiming block {};

    JUCE_DECLARE_NON_COPYABLE (TransportEngine)
};

} // namespace kinetic
