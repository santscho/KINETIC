#include <juce_core/juce_core.h>

#include "Core/Sequencer/SharedPattern.h"

#include <atomic>
#include <thread>

namespace kinetic
{

class SharedPatternTests final : public juce::UnitTest
{
public:
    SharedPatternTests() : juce::UnitTest ("SharedPattern", "kinetic") {}

    void runTest() override
    {
        testNothingBeforeFirstPublish();
        testPublicationArrivesOnce();
        testEditsArePrivateUntilPublished();
        testNewestPublicationWins();
        testRepeatedPublicationsStayOrdered();
        testConcurrentReadsNeverSeeHalfAnUpdate();
    }

private:
    /** Paints a recognisable marker into every step of one lane. */
    static void stampLane (Pattern& pattern, int laneIndex, int value)
    {
        auto& lane = pattern.lanes[static_cast<std::size_t> (laneIndex)];
        lane.settings.enabled = true;

        for (auto& step : lane.steps)
        {
            step.trigger = true;
            step.pitch = static_cast<juce::int8> (value);
        }
    }

    void testNothingBeforeFirstPublish()
    {
        beginTest ("Nothing reaches the audio thread before the first publish");

        SharedPattern shared;

        expect (shared.takeLatest() == nullptr);
        expect (shared.takeLatest() == nullptr);
    }

    void testPublicationArrivesOnce()
    {
        beginTest ("A publication arrives exactly once");

        SharedPattern shared;
        stampLane (shared.edit(), 2, 7);
        shared.publish();

        const auto* latest = shared.takeLatest();
        expect (latest != nullptr);

        if (latest != nullptr)
        {
            expect (latest->lanes[2].settings.enabled);
            expectEquals (static_cast<int> (latest->lanes[2].steps[0].pitch), 7);
            expect (latest->lanes[2].steps[9].trigger);
        }

        expect (shared.takeLatest() == nullptr);
    }

    void testEditsArePrivateUntilPublished()
    {
        beginTest ("Editing the pattern stays private until it is published");

        SharedPattern shared;
        shared.edit().lanes[0].steps[0].trigger = true;

        expect (shared.takeLatest() == nullptr);
        expect (shared.edit().lanes[0].steps[0].trigger);

        shared.publish();

        const auto* latest = shared.takeLatest();
        expect (latest != nullptr);

        // ...and later edits do not retroactively change what was published.
        shared.edit().lanes[0].steps[0].trigger = false;
        shared.edit().lanes[1].settings.gateLength = 3;

        if (latest != nullptr)
        {
            expect (latest->lanes[0].steps[0].trigger);
            expectEquals (latest->lanes[1].settings.gateLength, 16);
        }
    }

    void testNewestPublicationWins()
    {
        beginTest ("The newest publication wins");

        SharedPattern shared;

        stampLane (shared.edit(), 1, 3);
        shared.publish();
        stampLane (shared.edit(), 1, 11);
        shared.publish();

        const auto* latest = shared.takeLatest();
        expect (latest != nullptr);

        if (latest != nullptr)
            expectEquals (static_cast<int> (latest->lanes[1].steps[0].pitch), 11);
    }

    void testRepeatedPublicationsStayOrdered()
    {
        beginTest ("Reads never go backwards across many publications");

        SharedPattern shared;
        int lastSeen = -1;

        for (int i = 0; i < 512; ++i)
        {
            stampLane (shared.edit(), 0, i % 100);
            shared.publish();

            if (const auto* latest = shared.takeLatest())
            {
                const auto seen = static_cast<int> (latest->lanes[0].steps[0].pitch);

                // Published immediately before reading, so the newest value must be the one read.
                expectEquals (seen, i % 100);
                lastSeen = seen;
            }
        }

        expect (lastSeen == (511 % 100));
    }

    void testConcurrentReadsNeverSeeHalfAnUpdate()
    {
        beginTest ("A reader never sees a half written pattern");

        SharedPattern shared;
        std::atomic<bool> stop { false };
        std::atomic<int> reads { 0 };
        std::atomic<int> torn { 0 };

        std::thread writer ([&]
        {
            for (int i = 0; i < 20000; ++i)
            {
                stampLane (shared.edit(), 0, i % 60);
                shared.publish();
            }

            stop.store (true, std::memory_order_release);
        });

        std::thread reader ([&]
        {
            while (! stop.load (std::memory_order_acquire))
            {
                const auto* latest = shared.takeLatest();

                if (latest == nullptr)
                    continue;

                // Every step of the stamped lane was written with the same value, so a
                // snapshot holding two different values was read mid-write.
                const auto expected = static_cast<int> (latest->lanes[0].steps[0].pitch);
                bool consistent = true;

                for (const auto& step : latest->lanes[0].steps)
                    consistent = consistent && static_cast<int> (step.pitch) == expected;

                if (! consistent)
                    torn.fetch_add (1);

                reads.fetch_add (1);
            }
        });

        writer.join();
        reader.join();

        expectEquals (torn.load(), 0);
        expect (reads.load() > 0);
    }
};

static SharedPatternTests sharedPatternTests;

} // namespace kinetic
