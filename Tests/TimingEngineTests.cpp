#include <atomic>
#include <thread>

#include <juce_audio_basics/juce_audio_basics.h>

#include "Core/Concurrency/SpscQueue.h"
#include "Core/Timing/HostClock.h"
#include "Core/Timing/MidiEventSink.h"
#include "Core/Timing/NoteTracker.h"
#include "Core/Timing/TransportEngine.h"

namespace kinetic
{

namespace
{
    constexpr double sampleRate = 48000.0;

    bool hasNoteOn (const juce::MidiBuffer& buffer, int channel, int note)
    {
        for (const auto metadata : buffer)
            if (const auto message = metadata.getMessage();
                message.isNoteOn() && message.getChannel() == channel
                    && message.getNoteNumber() == note)
                return true;

        return false;
    }

    bool hasNoteOff (const juce::MidiBuffer& buffer, int channel, int note)
    {
        for (const auto metadata : buffer)
            if (const auto message = metadata.getMessage();
                message.isNoteOff() && message.getChannel() == channel
                    && message.getNoteNumber() == note)
                return true;

        return false;
    }

    int countController (const juce::MidiBuffer& buffer, int channel, int controller)
    {
        int count = 0;

        for (const auto metadata : buffer)
            if (const auto message = metadata.getMessage();
                message.isController() && message.getChannel() == channel
                    && message.getControllerNumber() == controller)
                ++count;

        return count;
    }
} // namespace

//==============================================================================
class HostClockTests final : public juce::UnitTest
{
public:
    HostClockTests() : juce::UnitTest ("HostClock", "kinetic") {}

    void runTest() override
    {
        beginTest ("PPQ maps to block-relative sample offsets");
        {
            HostClock clock;
            clock.prepare (sampleRate, 512);

            TransportInput in;
            in.hasPosition = true;
            in.isPlaying = true;
            in.bpm = 120.0;
            in.ppqPosition = 0.0;

            const auto& timing = clock.beginBlock (in, 512);

            expectWithinAbsoluteError (timing.ppqPerSample, 120.0 / (60.0 * sampleRate), 1.0e-18);
            expectEquals (timing.sampleOffsetForPpq (0.0), 0);
            expectEquals (timing.sampleOffsetForPpq (-2.0), 0);
            expectEquals (timing.sampleOffsetForPpq (timing.ppqPerSample * 4096.0), 511);

            // One sample of slack: PPQ values are floating point, so the mapping rounds down.
            expectWithinAbsoluteError (static_cast<double> (timing.sampleOffsetForPpq (timing.ppqPerSample * 128.0)),
                                       128.0, 1.0);
            expectWithinAbsoluteError (timing.ppqForSampleOffset (256), timing.ppqPerSample * 256.0, 1.0e-15);
            expectWithinAbsoluteError (timing.blockEndPpq, timing.ppqPerSample * 512.0, 1.0e-15);
        }

        beginTest ("Tempo changes rescale offsets per block");
        {
            HostClock clock;
            clock.prepare (sampleRate, 256);

            TransportInput in;
            in.hasPosition = true;
            in.isPlaying = true;
            in.bpm = 60.0;
            in.ppqPosition = 0.0;

            const auto& slow = clock.beginBlock (in, 256);
            expectWithinAbsoluteError (slow.ppqPerSample, 60.0 / (60.0 * sampleRate), 1.0e-18);

            in.bpm = 180.0;
            in.ppqPosition = slow.blockEndPpq;
            const auto& fast = clock.beginBlock (in, 256);
            expectWithinAbsoluteError (fast.ppqPerSample, 180.0 / (60.0 * sampleRate), 1.0e-18);
            expect (! fast.didJump);
        }

        beginTest ("Transport discontinuities are detected");
        {
            HostClock clock;
            clock.prepare (sampleRate, 256);

            TransportInput in;
            in.hasPosition = true;
            in.isPlaying = true;
            in.bpm = 128.0;
            in.ppqPosition = 4.0;

            const auto first = clock.beginBlock (in, 256);
            expect (! first.didJump);
            // A fresh instance joining a running transport is a start, so the sequencer can
            // align its phase on the first block it ever sees.
            expect (first.didStart);
            expect (! first.didStop);
            expectWithinAbsoluteError (first.blockStartPpq, 4.0, 1.0e-12);

            in.ppqPosition = first.blockEndPpq;
            const auto second = clock.beginBlock (in, 256);
            expect (! second.didJump);
            expect (! second.didStart);

            in.ppqPosition = 1.0;                       // loop wrap / backwards locate
            const auto& jumped = clock.beginBlock (in, 256);
            expect (jumped.didJump);

            in.ppqPosition = jumped.blockEndPpq;
            in.isPlaying = false;
            const auto& stopped = clock.beginBlock (in, 256);
            expect (stopped.didStop);

            in.isPlaying = true;
            const auto& restarted = clock.beginBlock (in, 256);
            expect (restarted.didStart);
        }

        beginTest ("Free-runs when the host exposes no transport");
        {
            HostClock clock;
            clock.prepare (sampleRate, 480);

            TransportInput in;
            in.hasPosition = false;
            in.isPlaying = true;
            in.bpm = 100.0;

            const auto first = clock.beginBlock (in, 480);
            expect (first.isFreeRunning);
            expect (first.didStart);
            expectWithinAbsoluteError (first.blockStartPpq, 0.0, 1.0e-12);

            const auto firstEnd = first.blockEndPpq;
            const auto second = clock.beginBlock (in, 480);
            expect (! second.didStart);
            expectWithinAbsoluteError (second.blockStartPpq, firstEnd, 1.0e-12);
            expectWithinAbsoluteError (second.blockEndPpq - second.blockStartPpq,
                                       first.blockEndPpq - first.blockStartPpq, 1.0e-12);
        }
    }
};
static HostClockTests hostClockTests;

//==============================================================================
class NoteTrackerTests final : public juce::UnitTest
{
public:
    NoteTrackerTests() : juce::UnitTest ("NoteTracker", "kinetic") {}

    void runTest() override
    {
        beginTest ("Note-ons and note-offs stay paired");
        {
            MidiEventSink sink;
            sink.reset (512);

            NoteTracker tracker;
            tracker.prepare (sampleRate);

            expect (tracker.noteOn (sink, 1, 60, 0.8f, 0));
            expectEquals (sink.size(), 1);
            expect (tracker.isActive (1, 60));
            expectEquals (tracker.activeCount(), 1);

            expect (! tracker.noteOn (sink, 1, 60, 0.5f, 0));   // retrigger suppressed
            expectEquals (sink.size(), 1);

            tracker.noteOff (sink, 1, 60, 100);
            expectEquals (sink.size(), 2);
            expect (! tracker.isActive (1, 60));
            expectEquals (tracker.activeCount(), 0);

            tracker.noteOff (sink, 1, 60, 200);                 // orphaned off suppressed
            expectEquals (sink.size(), 2);
        }

        beginTest ("Panic releases every held note and resets used channels only");
        {
            MidiEventSink sink;
            sink.reset (512);

            NoteTracker tracker;
            tracker.prepare (sampleRate);

            tracker.noteOn (sink, 3, 48, 1.0f, 0);
            tracker.noteOn (sink, 3, 55, 1.0f, 0);
            tracker.noteOn (sink, 9, 36, 1.0f, 0);
            expectEquals (tracker.activeCount(), 3);

            sink.reset (512);
            tracker.panic (sink, 0);

            expectEquals (tracker.activeCount(), 0);
            expectEquals (sink.size(), 3 + 6);   // 3 offs + (CC64, CC123, CC121) on 2 channels

            juce::MidiBuffer buffer;
            sink.flushTo (buffer);

            expect (hasNoteOff (buffer, 3, 48));
            expect (hasNoteOff (buffer, 3, 55));
            expect (hasNoteOff (buffer, 9, 36));
            expectEquals (countController (buffer, 3, 123), 1);
            expectEquals (countController (buffer, 9, 123), 1);
            expectEquals (countController (buffer, 1, 123), 0);

            sink.reset (512);
            tracker.panic (sink, 0);
            expectEquals (sink.size(), 0);        // nothing left to panic about
        }

        beginTest ("Channels are independent");
        {
            MidiEventSink sink;
            sink.reset (512);

            NoteTracker tracker;
            tracker.prepare (sampleRate);

            tracker.noteOn (sink, 1, 60, 1.0f, 0);
            tracker.noteOn (sink, 2, 60, 1.0f, 0);
            expectEquals (tracker.activeCount(), 2);

            tracker.releaseChannel (sink, 1, 0);

            expect (! tracker.isActive (1, 60));
            expect (tracker.isActive (2, 60));
            expectEquals (tracker.activeOnChannel (2), 1);
        }

        beginTest ("All sixteen channels are addressable");
        {
            MidiEventSink sink;
            sink.reset (512);

            NoteTracker tracker;
            tracker.prepare (sampleRate);

            for (int channel = 1; channel <= 16; ++channel)
                expect (tracker.noteOn (sink, channel, 60, 1.0f, 0));

            expectEquals (tracker.activeCount(), 16);

            juce::MidiBuffer ons;
            sink.flushTo (ons);

            for (int channel = 1; channel <= 16; ++channel)
                expect (hasNoteOn (ons, channel, 60));

            sink.reset (512);
            tracker.panic (sink, 0);

            juce::MidiBuffer offs;
            sink.flushTo (offs);

            for (int channel = 1; channel <= 16; ++channel)
                expect (hasNoteOff (offs, channel, 60));

            expectEquals (tracker.activeCount(), 0);
        }

        beginTest ("Out-of-range channels are clamped, never dropped");
        {
            MidiEventSink sink;
            sink.reset (512);

            NoteTracker tracker;
            tracker.prepare (sampleRate);

            expect (tracker.noteOn (sink, 0, 60, 1.0f, 0));      // clamped to channel 1
            expect (tracker.isActive (1, 60));

            expect (tracker.noteOn (sink, 99, 61, 1.0f, 0));     // clamped to channel 16
            expect (tracker.isActive (16, 61));
        }
    }
};
static NoteTrackerTests noteTrackerTests;

//==============================================================================
class MidiEventSinkTests final : public juce::UnitTest
{
public:
    MidiEventSinkTests() : juce::UnitTest ("MidiEventSink", "kinetic") {}

    void runTest() override
    {
        beginTest ("Events flush in ascending sample order");
        {
            MidiEventSink sink;
            sink.reset (256);

            sink.addNoteOn (1, 60, 1.0f, 40);
            sink.addNoteOff (1, 60, 5);
            sink.addNoteOn (2, 64, 1.0f, 200);

            juce::MidiBuffer buffer;
            sink.flushTo (buffer);

            expectEquals (buffer.getNumEvents(), 3);

            int previousOffset = -1;

            for (const auto metadata : buffer)
            {
                expect (metadata.samplePosition >= previousOffset);
                previousOffset = metadata.samplePosition;
            }
        }

        beginTest ("Offsets are clamped and overflow is counted");
        {
            MidiEventSink sink;
            sink.reset (64);

            sink.addNoteOn (1, 60, 1.0f, -10);
            sink.addNoteOn (1, 61, 1.0f, 9999);

            expectEquals (sink.size(), 2);
            expectEquals (sink.droppedCount(), 0);

            juce::MidiBuffer buffer;
            sink.flushTo (buffer);

            for (const auto metadata : buffer)
                expect (metadata.samplePosition >= 0 && metadata.samplePosition < 64);

            sink.reset (64);   // start from empty so the drop count is exact

            for (int i = 0; i < MidiEventSink::maxEvents + 10; ++i)
                sink.addNoteOn (1, 62, 1.0f, 0);

            expectEquals (sink.size(), MidiEventSink::maxEvents);
            expectEquals (sink.droppedCount(), 10);
        }
    }
};
static MidiEventSinkTests midiEventSinkTests;

//==============================================================================
class SpscQueueTests final : public juce::UnitTest
{
public:
    SpscQueueTests() : juce::UnitTest ("SpscQueue", "kinetic") {}

    void runTest() override
    {
        beginTest ("FIFO ordering and capacity");
        {
            SpscQueue<int, 8> queue;

            for (int i = 0; i < 10; ++i)
                expect (queue.push (i) == (i < 8));

            expectEquals (queue.size(), 8);

            for (int i = 0; i < 8; ++i)
            {
                int value = -1;
                expect (queue.pop (value));
                expectEquals (value, i);
            }

            int value = -1;
            expect (! queue.pop (value));
            expect (queue.isEmpty());

            // Space freed by the reader is reusable.
            for (int i = 0; i < 8; ++i)
                expect (queue.push (100 + i));
        }

        beginTest ("Single producer / single consumer under load");
        {
            SpscQueue<int, 1024> queue;
            std::atomic<bool> producerFinished { false };

            std::thread producer ([&queue, &producerFinished]
            {
                for (int i = 0; i < 200000; ++i)
                    while (! queue.push (i))
                        std::this_thread::yield();

                producerFinished.store (true, std::memory_order_release);
            });

            int expected = 0;
            int received = 0;
            bool inOrder = true;

            while (! producerFinished.load (std::memory_order_acquire) || ! queue.isEmpty())
            {
                int value = -1;

                if (queue.pop (value))
                {
                    if (value != expected)
                        inOrder = false;

                    ++expected;
                    ++received;
                }
            }

            producer.join();

            expect (inOrder);
            expectEquals (received, 200000);
        }
    }
};
static SpscQueueTests spscQueueTests;

//==============================================================================
class TransportEngineTests final : public juce::UnitTest
{
public:
    TransportEngineTests() : juce::UnitTest ("TransportEngine", "kinetic") {}

    void runTest() override
    {
        beginTest ("Transport stop flushes held notes at the top of the block");
        {
            TransportEngine engine;
            engine.prepare (sampleRate, 256);

            TransportInput in;
            in.hasPosition = true;
            in.isPlaying = true;
            in.bpm = 120.0;
            in.ppqPosition = 0.0;

            const auto& first = engine.beginBlock (in, 256);
            engine.notes().noteOn (engine.events(), 1, 64, 1.0f, 0);

            juce::MidiBuffer playing;
            engine.flushGeneratedMidi (playing);
            expectEquals (playing.getNumEvents(), 1);
            expect (hasNoteOn (playing, 1, 64));
            expectEquals (engine.activeNoteCount(), 1);

            in.isPlaying = false;
            in.ppqPosition = first.blockEndPpq;
            engine.beginBlock (in, 256);

            juce::MidiBuffer stopped;
            engine.flushGeneratedMidi (stopped);

            expectEquals (engine.activeNoteCount(), 0);
            expect (hasNoteOff (stopped, 1, 64));

            for (const auto metadata : stopped)
                expectEquals (metadata.samplePosition, 0);
        }

        beginTest ("Loop wrap flushes held notes");
        {
            TransportEngine engine;
            engine.prepare (sampleRate, 128);

            TransportInput in;
            in.hasPosition = true;
            in.isPlaying = true;
            in.isLooping = true;
            in.loopStartPpq = 0.0;
            in.loopEndPpq = 4.0;
            in.bpm = 120.0;
            in.ppqPosition = 3.9;

            engine.beginBlock (in, 128);
            engine.notes().noteOn (engine.events(), 5, 72, 1.0f, 0);
            expectEquals (engine.activeNoteCount(), 1);

            in.ppqPosition = 0.0;                    // host wrapped the loop
            const auto& wrapped = engine.beginBlock (in, 128);
            expect (wrapped.didJump);

            juce::MidiBuffer buffer;
            engine.flushGeneratedMidi (buffer);

            expectEquals (engine.activeNoteCount(), 0);
            expect (hasNoteOff (buffer, 5, 72));
        }

        beginTest ("UI panic command reaches the audio thread");
        {
            TransportEngine engine;
            engine.prepare (sampleRate, 256);

            TransportInput in;
            in.hasPosition = true;
            in.isPlaying = true;
            in.bpm = 120.0;
            in.ppqPosition = 0.0;

            engine.beginBlock (in, 256);
            engine.notes().noteOn (engine.events(), 2, 40, 1.0f, 0);
            engine.notes().noteOn (engine.events(), 3, 41, 1.0f, 0);
            expectEquals (engine.activeNoteCount(), 2);

            expect (engine.postCommand ({ UiCommandType::allNotesOff, 0 }));

            in.ppqPosition = engine.currentBlock().blockEndPpq;
            engine.beginBlock (in, 256);

            juce::MidiBuffer buffer;
            engine.flushGeneratedMidi (buffer);

            expectEquals (engine.activeNoteCount(), 0);
            expectEquals (countController (buffer, 2, 123), 1);
            expectEquals (countController (buffer, 3, 123), 1);
        }

        beginTest ("Generated events land at the offset the PPQ implies");
        {
            TransportEngine engine;
            engine.prepare (sampleRate, 256);

            TransportInput in;
            in.hasPosition = true;
            in.isPlaying = true;
            in.bpm = 120.0;
            in.ppqPosition = 8.0;

            const auto& timing = engine.beginBlock (in, 256);
            const auto targetPpq = timing.blockStartPpq + timing.ppqPerSample * 64.0;
            engine.notes().noteOn (engine.events(), 1, 60, 1.0f,
                                   timing.sampleOffsetForPpq (targetPpq));

            juce::MidiBuffer buffer;
            engine.flushGeneratedMidi (buffer);

            expect (hasNoteOn (buffer, 1, 60));

            for (const auto metadata : buffer)
                expectWithinAbsoluteError (static_cast<double> (metadata.samplePosition), 64.0, 1.0);
        }
    }
};
static TransportEngineTests transportEngineTests;

} // namespace kinetic
