#include <juce_audio_basics/juce_audio_basics.h>

#include "Core/Sequencer/SequenceMath.h"
#include "Core/Sequencer/Sequencer.h"
#include "Core/Timing/TransportEngine.h"

namespace kinetic
{

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr double bpm = 120.0;

    struct TimedEvent
    {
        juce::MidiMessage message;
        int samplePosition = 0;
    };

    /**
        Events in playback order.

        Blocks are appended in time, not merged by block-relative sample position, so tests can
        assert musical order (note-on before its note-off) across block boundaries.
    */
    using EventList = juce::Array<TimedEvent>;

    /** Drives the real TransportEngine + Sequencer the way processBlock does. */
    struct Rig
    {
        TransportEngine engine;
        double ppq = 0.0;

        Rig()
        {
            engine.prepare (sampleRate, 512);
        }

        Pattern& pattern() { return engine.sequencer().pattern(); }

        EventList block (int numSamples = 512)
        {
            TransportInput in;
            in.hasPosition = true;
            in.isPlaying = true;
            in.bpm = bpm;
            in.ppqPosition = ppq;

            const auto& timing = engine.beginBlock (in, numSamples);

            juce::MidiBuffer buffer;
            engine.flushGeneratedMidi (buffer);
            ppq = timing.blockEndPpq;

            EventList events;
            for (const auto metadata : buffer)
                events.add ({ metadata.getMessage(), metadata.samplePosition });

            return events;
        }

        EventList run (double durationPpq, int numSamples = 512)
        {
            EventList collected;
            const auto target = ppq + durationPpq;

            while (ppq < target)
                collected.addArray (block (numSamples));

            return collected;
        }
    };

    /** Enables a lane and fills `length` steps with plain triggers. */
    Lane& setupLane (Pattern& pattern, int index, int length,
                     ClockRatio clock, int channel = 1, int rootNote = 60)
    {
        auto& lane = pattern.lanes[static_cast<std::size_t> (index)];
        lane.settings.enabled = true;
        lane.settings.midiChannel = channel;
        lane.settings.rootNote = rootNote;
        lane.settings.clock = clock;
        lane.settings.gateLength = length;
        lane.settings.pitchLength = length;
        lane.settings.velocityLength = length;
        lane.settings.modLength = length;

        for (int i = 0; i < length; ++i)
        {
            lane.steps[static_cast<std::size_t> (i)] = {};
            lane.steps[static_cast<std::size_t> (i)].trigger = true;
            lane.steps[static_cast<std::size_t> (i)].velocity = 100;
            lane.steps[static_cast<std::size_t> (i)].gatePercent = 50;
            lane.steps[static_cast<std::size_t> (i)].probability = 100;
            lane.steps[static_cast<std::size_t> (i)].ratchets = 1;
        }

        return lane;
    }

    int countNoteOns (const EventList& events)
    {
        int count = 0;
        for (const auto& event : events)
            if (event.message.isNoteOn())
                ++count;
        return count;
    }

    int countNoteOffs (const EventList& events)
    {
        int count = 0;
        for (const auto& event : events)
            if (event.message.isNoteOff())
                ++count;
        return count;
    }

    int countController (const EventList& events, int channel, int controller)
    {
        int count = 0;
        for (const auto& event : events)
            if (event.message.isController() && event.message.getChannel() == channel
                && event.message.getControllerNumber() == controller)
                ++count;
        return count;
    }

    int countPitchBends (const EventList& events)
    {
        int count = 0;
        for (const auto& event : events)
            if (event.message.isPitchWheel())
                ++count;
        return count;
    }

    juce::Array<int> noteOnPitches (const EventList& events)
    {
        juce::Array<int> pitches;
        for (const auto& event : events)
            if (event.message.isNoteOn())
                pitches.add (event.message.getNoteNumber());
        return pitches;
    }

    juce::Array<float> noteOnVelocities (const EventList& events)
    {
        juce::Array<float> velocities;
        for (const auto& event : events)
            if (event.message.isNoteOn())
                velocities.add (event.message.getFloatVelocity());
        return velocities;
    }

    juce::Array<int> noteOnPositions (const EventList& events)
    {
        juce::Array<int> positions;
        for (const auto& event : events)
            if (event.message.isNoteOn())
                positions.add (event.samplePosition);
        return positions;
    }

    int controllerValue (const EventList& events, int channel, int controller)
    {
        for (const auto& event : events)
            if (event.message.isController() && event.message.getChannel() == channel
                && event.message.getControllerNumber() == controller)
                return event.message.getControllerValue();
        return -1;
    }

    int countEuclidPulses (int steps, int pulses)
    {
        int count = 0;
        for (int i = 0; i < steps; ++i)
            count += math::euclidBit (i, steps, pulses) ? 1 : 0;
        return count;
    }
} // namespace

//==============================================================================
class EuclideanTests final : public juce::UnitTest
{
public:
    EuclideanTests() : juce::UnitTest ("Euclidean", "kinetic") {}

    void runTest() override
    {
        beginTest ("Pulse count always matches the request");
        {
            expectEquals (countEuclidPulses (8, 3), 3);
            expectEquals (countEuclidPulses (16, 5), 5);
            expectEquals (countEuclidPulses (13, 7), 7);
            expectEquals (countEuclidPulses (64, 1), 1);
            expectEquals (countEuclidPulses (8, 8), 8);
            expectEquals (countEuclidPulses (8, 0), 0);
        }

        beginTest ("Pulses distribute evenly and start on the downbeat");
        {
            // E(3, 8) is the canonical x..x..x. figure.
            expect (math::euclidBit (0, 8, 3));
            expect (! math::euclidBit (1, 8, 3));
            expect (! math::euclidBit (2, 8, 3));
            expect (math::euclidBit (3, 8, 3));
            expect (! math::euclidBit (4, 8, 3));
            expect (! math::euclidBit (5, 8, 3));
            expect (math::euclidBit (6, 8, 3));
            expect (! math::euclidBit (7, 8, 3));
        }

        beginTest ("Rotation shifts the pattern without changing density");
        {
            for (int i = 0; i < 16; ++i)
                expectEquals (static_cast<int> (math::euclidBitRotated (i, 16, 5, 4)),
                              static_cast<int> (math::euclidBit ((i + 16 - 4) % 16, 16, 5)));

            int rotatedCount = 0;
            for (int i = 0; i < 16; ++i)
                rotatedCount += math::euclidBitRotated (i, 16, 5, 7) ? 1 : 0;

            expectEquals (rotatedCount, 5);
        }

        beginTest ("Out-of-range values clamp instead of misbehaving");
        {
            expectEquals (countEuclidPulses (8, 99), 8);      // more pulses than steps
            expect (math::euclidBit (0, 0, 1));               // steps clamped to 1
            expect (! math::euclidBit (0, 8, 0));             // no pulses, no hits
        }
    }
};
static EuclideanTests euclideanTests;

//==============================================================================
class ClockRatioTests final : public juce::UnitTest
{
public:
    ClockRatioTests() : juce::UnitTest ("ClockRatio", "kinetic") {}

    void runTest() override
    {
        beginTest ("Straight note values map to PPQ");
        {
            expectWithinAbsoluteError (ClockRatio { 1, 1, ClockModifier::straight }.stepPpq(), 4.0, 1.0e-12);
            expectWithinAbsoluteError (ClockRatio { 1, 4, ClockModifier::straight }.stepPpq(), 1.0, 1.0e-12);
            expectWithinAbsoluteError (ClockRatio { 1, 8, ClockModifier::straight }.stepPpq(), 0.5, 1.0e-12);
            expectWithinAbsoluteError (ClockRatio { 1, 16, ClockModifier::straight }.stepPpq(), 0.25, 1.0e-12);
            expectWithinAbsoluteError (ClockRatio { 1, 64, ClockModifier::straight }.stepPpq(), 0.0625, 1.0e-12);
        }

        beginTest ("Dotted and triplet modifiers scale the step");
        {
            expectWithinAbsoluteError (ClockRatio { 1, 8, ClockModifier::dotted }.stepPpq(), 0.75, 1.0e-12);
            expectWithinAbsoluteError (ClockRatio { 1, 8, ClockModifier::triplet }.stepPpq(),
                                       1.0 / 3.0, 1.0e-12);
            expectWithinAbsoluteError (ClockRatio { 1, 16, ClockModifier::triplet }.stepPpq(),
                                       1.0 / 6.0, 1.0e-12);
        }

        beginTest ("Custom ratios are honoured");
        {
            expectWithinAbsoluteError (ClockRatio { 3, 16, ClockModifier::straight }.stepPpq(), 0.75, 1.0e-12);
            expectWithinAbsoluteError (ClockRatio { 5, 32, ClockModifier::straight }.stepPpq(), 0.625, 1.0e-12);
            expectWithinAbsoluteError (ClockRatio { 1, 0, ClockModifier::straight }.stepPpq(), 4.0, 1.0e-12);
        }
    }
};
static ClockRatioTests clockRatioTests;

//==============================================================================
class DirectionTests final : public juce::UnitTest
{
public:
    DirectionTests() : juce::UnitTest ("StepWalker", "kinetic") {}

    void runTest() override
    {
        const auto sequence = [] (PlayDirection mode, int startIndex, int length, int count)
        {
            math::StepWalker walker;
            math::Rng rng (12345u);
            walker.reset (startIndex);

            juce::Array<int> result;
            for (int i = 0; i < count; ++i)
            {
                result.add (walker.current());
                walker.advance (length, mode, rng);
            }
            return result;
        };

        beginTest ("Forward and reverse");
        {
            expect (sequence (PlayDirection::forward, 0, 4, 6) == juce::Array<int> { 0, 1, 2, 3, 0, 1 });
            expect (sequence (PlayDirection::reverse, 3, 4, 6) == juce::Array<int> { 3, 2, 1, 0, 3, 2 });
        }

        beginTest ("Ping-pong bounces without repeating the ends");
        {
            expect (sequence (PlayDirection::pingPong, 0, 4, 8)
                    == juce::Array<int> { 0, 1, 2, 3, 2, 1, 0, 1 });
        }

        beginTest ("Pendulum repeats the end steps");
        {
            expect (sequence (PlayDirection::pendulum, 0, 4, 9)
                    == juce::Array<int> { 0, 1, 2, 3, 3, 2, 1, 0, 0 });
        }

        beginTest ("Random walk stays in range and moves one step at a time");
        {
            math::StepWalker walker;
            math::Rng rng (9876u);
            walker.reset (0);

            for (int i = 0; i < 200; ++i)
            {
                const auto before = walker.current();
                const auto after = walker.advance (5, PlayDirection::randomWalk, rng);

                expect (after >= 0 && after < 5);

                const auto delta = (after - before + 5) % 5;
                expect (delta == 1 || delta == 4);
            }
        }

        beginTest ("Single-step lanes never leave index 0");
        {
            for (const auto mode : { PlayDirection::forward, PlayDirection::reverse,
                                     PlayDirection::pingPong, PlayDirection::pendulum,
                                     PlayDirection::randomWalk })
            {
                const auto seq = sequence (mode, 0, 1, 5);
                expectEquals (seq.getUnchecked (4), 0);
            }
        }

        beginTest ("peekNext agrees with advance");
        {
            for (int length = 1; length <= 6; ++length)
            {
                for (int index = 0; index < length; ++index)
                {
                    for (const auto mode : { PlayDirection::forward, PlayDirection::reverse,
                                             PlayDirection::pingPong, PlayDirection::pendulum })
                    {
                        math::StepWalker a;
                        math::StepWalker b;
                        math::Rng rng (42u);
                        a.reset (index);
                        b.reset (index);

                        const auto peeked = a.peekNext (length, mode);
                        const auto advanced = b.advance (length, mode, rng);

                        expectEquals (peeked, advanced);
                    }
                }
            }
        }
    }
};
static DirectionTests directionTests;

//==============================================================================
class ConditionTests final : public juce::UnitTest
{
public:
    ConditionTests() : juce::UnitTest ("CycleConditions", "kinetic") {}

    void runTest() override
    {
        beginTest ("Ratios gate on the expected cycles");
        {
            expect (math::conditionPasses (CycleCondition::always, 0));
            expect (math::conditionPasses (CycleCondition::always, 9));

            expect (math::conditionPasses (CycleCondition::first, 0));
            expect (! math::conditionPasses (CycleCondition::first, 1));

            expect (! math::conditionPasses (CycleCondition::notFirst, 0));
            expect (math::conditionPasses (CycleCondition::notFirst, 4));

            expect (math::conditionPasses (CycleCondition::oneOfTwo, 0));
            expect (! math::conditionPasses (CycleCondition::oneOfTwo, 1));
            expect (math::conditionPasses (CycleCondition::oneOfTwo, 2));

            expect (! math::conditionPasses (CycleCondition::twoOfTwo, 0));
            expect (math::conditionPasses (CycleCondition::twoOfTwo, 1));

            expect (math::conditionPasses (CycleCondition::oneOfFour, 0));
            expect (math::conditionPasses (CycleCondition::threeOfFour, 2));
            expect (math::conditionPasses (CycleCondition::fourOfFour, 3));

            expect (math::conditionPasses (CycleCondition::fill, 3));
            expect (! math::conditionPasses (CycleCondition::fill, 2));
        }
    }
};
static ConditionTests conditionTests;

//==============================================================================
class ScaleTests final : public juce::UnitTest
{
public:
    ScaleTests() : juce::UnitTest ("ScaleQuantiser", "kinetic") {}

    void runTest() override
    {
        beginTest ("Major scale snaps chromatic notes onto the scale");
        {
            const auto major = math::scaleMask (ScaleType::major, 0);

            // Chromatic 60..67 snapped onto C major, ties resolved upward.
            const int expected[] = { 60, 62, 62, 64, 64, 65, 67, 67 };

            for (int i = 0; i < 8; ++i)
                expectEquals (math::quantise (60 + i, major, 0), expected[i]);

            const int scaleDegrees[] = { 60, 62, 64, 65, 67, 69, 71, 72 };
            for (const auto note : scaleDegrees)
                expectEquals (math::quantise (note, major, 0), note);   // in-scale notes untouched
        }

        beginTest ("Root shifting rotates the allowed pitch classes");
        {
            const auto major = math::scaleMask (ScaleType::major, 0);

            // D major contains C#, so the note is left alone.
            expectEquals (math::quantise (61, major, 2), 61);
            expectEquals (math::quantise (62, major, 2), 62);
            expectEquals (math::quantise (63, major, 2), 64);   // D# -> E
        }

        beginTest ("Minor, pentatonic and chromatic masks");
        {
            const auto minor = math::scaleMask (ScaleType::minor, 0);
            expectEquals (math::quantise (62, minor, 0), 62);   // D
            expectEquals (math::quantise (63, minor, 0), 63);   // Eb

            const auto pentatonic = math::scaleMask (ScaleType::pentatonic, 0);
            expectEquals (math::quantise (65, pentatonic, 0), 64);   // F is absent -> E
            expectEquals (math::quantise (66, pentatonic, 0), 67);   // F# -> G

            const auto chromatic = math::scaleMask (ScaleType::chromatic, 0);
            for (int note = 0; note < 128; ++note)
                expectEquals (math::quantise (note, chromatic, 0), note);
        }

        beginTest ("Each named mode uses its own pitch classes");
        {
            // Phrygian dominant is the fifth mode of harmonic minor: 0 1 4 5 7 8 10.
            const auto phrygianDominant = math::scaleMask (ScaleType::phrygianDominant, 0);

            for (int note = 0; note < 12; ++note)
            {
                const int expected = (note == 0 || note == 1 || note == 4 || note == 5
                                        || note == 7 || note == 8 || note == 10) ? 1 : 0;

                expectEquals (static_cast<int> ((phrygianDominant >> note) & 1), expected);
            }

            expectEquals (math::quantise (63, phrygianDominant, 0), 64);   // D# -> E
            expectEquals (math::quantise (66, phrygianDominant, 0), 67);   // F# ties between F and G, up
        }

        beginTest ("Custom mask is respected");
        {
            const auto custom = math::scaleMask (ScaleType::custom, 0x0011);   // C and E only
            expectEquals (math::quantise (61, custom, 0), 60);
            expectEquals (math::quantise (62, custom, 0), 64);
        }
    }
};
static ScaleTests scaleTests;

//==============================================================================
class SequencerRenderTests final : public juce::UnitTest
{
public:
    SequencerRenderTests() : juce::UnitTest ("Sequencer", "kinetic") {}

    void runTest() override
    {
        beginTest ("A four-step lane renders note-on/off pairs on the grid");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 4, { 1, 8, ClockModifier::straight });

            for (int i = 0; i < 4; ++i)
                lane.steps[static_cast<std::size_t> (i)].pitch = static_cast<juce::int8> (i);

            const auto events = rig.run (1.9);

            expectEquals (countNoteOns (events), 4);
            expectEquals (countNoteOffs (events), 4);
            expect (noteOnPitches (events) == juce::Array<int> { 60, 61, 62, 63 });
        }

        beginTest ("Pitch sub-lane runs at its own loop length");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 4, { 1, 8, ClockModifier::straight });

            lane.settings.pitchLength = 3;
            lane.steps[0].pitch = 0;
            lane.steps[1].pitch = 5;
            lane.steps[2].pitch = 7;

            const auto pitches = noteOnPitches (rig.run (5.99));

            expectEquals (pitches.size(), 12);

            for (int i = 0; i < pitches.size(); ++i)
            {
                const int expected[] = { 60, 65, 67 };
                expectEquals (pitches[i], expected[i % 3]);
            }
        }

        beginTest ("Velocity sub-lane runs at its own loop length");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 4, { 1, 8, ClockModifier::straight });

            lane.settings.velocityLength = 2;
            lane.steps[0].velocity = 100;
            lane.steps[1].velocity = 20;

            const auto velocities = noteOnVelocities (rig.run (1.9));

            expectEquals (velocities.size(), 4);
            expectWithinAbsoluteError (static_cast<double> (velocities[0]), 100.0 / 127.0, 0.01);
            expectWithinAbsoluteError (static_cast<double> (velocities[1]), 20.0 / 127.0, 0.01);
            expectWithinAbsoluteError (static_cast<double> (velocities[2]), 100.0 / 127.0, 0.01);
            expectWithinAbsoluteError (static_cast<double> (velocities[3]), 20.0 / 127.0, 0.01);
        }

        beginTest ("Euclidean mode generates the trigger pattern");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 8, { 1, 8, ClockModifier::straight });

            for (auto& step : lane.steps)
                step.trigger = false;               // nothing painted

            lane.settings.euclidEnabled = true;
            lane.settings.euclidSteps = 8;
            lane.settings.euclidPulses = 3;
            lane.settings.euclidRotation = 0;

            expectEquals (countNoteOns (rig.run (3.9)), 3);
        }

        beginTest ("Probability gates steps");
        {
            Rig never;
            auto& neverLane = setupLane (never.pattern(), 0, 4, { 1, 8, ClockModifier::straight });
            for (auto& step : neverLane.steps)
                step.probability = 0;

            expectEquals (countNoteOns (never.run (1.9)), 0);

            Rig always;
            setupLane (always.pattern(), 0, 4, { 1, 8, ClockModifier::straight });
            expectEquals (countNoteOns (always.run (1.9)), 4);
        }

        beginTest ("Cycle conditions repeat across passes");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 1, { 1, 8, ClockModifier::straight });
            lane.steps[0].condition = CycleCondition::oneOfTwo;

            // Eight steps == eight single-step passes, so exactly half should fire.
            expectEquals (countNoteOns (rig.run (3.9)), 4);
        }

        beginTest ("Ratchets subdivide the step evenly");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 1, { 1, 4, ClockModifier::straight });
            lane.steps[0].ratchets = 4;
            lane.steps[0].gatePercent = 50;

            const auto positions = noteOnPositions (rig.block (24000));

            expectEquals (positions.size(), 4);
            expectWithinAbsoluteError (static_cast<double> (positions[0]), 0.0, 2.0);
            expectWithinAbsoluteError (static_cast<double> (positions[1]), 6000.0, 2.0);
            expectWithinAbsoluteError (static_cast<double> (positions[2]), 12000.0, 2.0);
            expectWithinAbsoluteError (static_cast<double> (positions[3]), 18000.0, 2.0);
        }

        beginTest ("Ratchet velocity ramps");
        {
            Rig up;
            auto& upLane = setupLane (up.pattern(), 0, 1, { 1, 4, ClockModifier::straight });
            upLane.steps[0].ratchets = 3;
            upLane.steps[0].ratchetRamp = VelocityRamp::rampUp;

            const auto upVelocities = noteOnVelocities (up.run (0.9));
            expectEquals (upVelocities.size(), 3);
            expect (upVelocities[0] < upVelocities[1]);
            expect (upVelocities[1] < upVelocities[2]);

            Rig down;
            auto& downLane = setupLane (down.pattern(), 0, 1, { 1, 4, ClockModifier::straight });
            downLane.steps[0].ratchets = 3;
            downLane.steps[0].ratchetRamp = VelocityRamp::rampDown;

            const auto downVelocities = noteOnVelocities (down.run (0.9));
            expectEquals (downVelocities.size(), 3);
            expect (downVelocities[0] > downVelocities[1]);
            expect (downVelocities[1] > downVelocities[2]);
        }

        beginTest ("Accent boosts velocity and fires its CC spike");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 1, { 1, 4, ClockModifier::straight });
            lane.steps[0].accent = true;
            lane.steps[0].velocity = 100;

            const auto events = rig.block (24000);
            const auto velocities = noteOnVelocities (events);

            expectEquals (velocities.size(), 1);
            expect (velocities[0] > 0.9f);                 // 100 + 25 boost == 125/127
            expectEquals (controllerValue (events, 1, 74), 110);
        }

        beginTest ("Slide via CC65 emits the portamento toggle");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 2, { 1, 4, ClockModifier::straight });
            lane.settings.slideMode = SlideMode::portamentoCC65;
            lane.steps[1].slide = true;

            const auto events = rig.block (48000);

            expectEquals (countController (events, 1, 65), 2);

            int firstValue = -1;
            int secondValue = -1;
            for (const auto& event : events)
            {
                if (event.message.isController() && event.message.getControllerNumber() == 65)
                {
                    if (firstValue < 0) firstValue = event.message.getControllerValue();
                    else                secondValue = event.message.getControllerValue();
                }
            }

            expectEquals (firstValue, 0);
            expectEquals (secondValue, 127);
        }

        beginTest ("Slide via legato overlap delays the previous note-off");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 2, { 1, 4, ClockModifier::straight });
            lane.settings.slideMode = SlideMode::legatoOverlap;
            lane.settings.slideOverlapMs = 12.0;
            lane.steps[0].pitch = 0;
            lane.steps[1].pitch = 2;
            lane.steps[1].slide = true;

            const auto events = rig.block (26400);     // 1.1 PPQ

            int onSecond = -1;
            int offFirst = -1;

            for (const auto& event : events)
            {
                if (event.message.isNoteOn() && event.message.getNoteNumber() == 62)
                    onSecond = event.samplePosition;
                if (event.message.isNoteOff() && event.message.getNoteNumber() == 60)
                    offFirst = event.samplePosition;
            }

            expect (onSecond >= 0);
            expect (offFirst >= 0);
            expect (offFirst > onSecond);      // the old note is still ringing over the new one
        }

        beginTest ("Slide via pitch bend keeps one note and ramps it");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 2, { 1, 4, ClockModifier::straight });
            lane.settings.slideMode = SlideMode::pitchBendRamp;
            lane.settings.bendRangeSemitones = 12.0;
            lane.steps[0].pitch = 0;
            lane.steps[1].pitch = 2;
            lane.steps[1].slide = true;

            const auto events = rig.block (48000);     // 2 PPQ: covers the whole ramp

            expectEquals (countNoteOns (events), 1);   // not retriggered
            expect (countPitchBends (events) > 10);

            int maxBend = 0;
            for (const auto& event : events)
                if (event.message.isPitchWheel())
                    maxBend = juce::jmax (maxBend, event.message.getPitchWheelValue());

            // +2 semitones over a 12-semitone range == 8192 + 8192/6
            expectWithinAbsoluteError (static_cast<double> (maxBend), 9557.0, 2.0);
        }

        beginTest ("Lanes address independent MIDI channels");
        {
            Rig rig;
            setupLane (rig.pattern(), 0, 1, { 1, 4, ClockModifier::straight }, 1, 60);
            setupLane (rig.pattern(), 1, 1, { 1, 4, ClockModifier::straight }, 2, 72);

            const auto events = rig.block (24000);

            bool channel1 = false;
            bool channel2 = false;

            for (const auto& event : events)
            {
                if (event.message.isNoteOn() && event.message.getChannel() == 1
                    && event.message.getNoteNumber() == 60)
                    channel1 = true;

                if (event.message.isNoteOn() && event.message.getChannel() == 2
                    && event.message.getNoteNumber() == 72)
                    channel2 = true;
            }

            expect (channel1);
            expect (channel2);
        }

        beginTest ("Eight lanes run polymetrically without interfering");
        {
            Rig rig;

            for (int i = 0; i < maxLanes; ++i)
                setupLane (rig.pattern(), i, 1 + i, { 1, 16, ClockModifier::straight }, i + 1, 60);

            const auto events = rig.run (0.9);          // four sixteenths

            int onChannelThree = 0;
            for (const auto& event : events)
                if (event.message.isNoteOn() && event.message.getChannel() == 3)
                    ++onChannelThree;

            expectEquals (countNoteOns (events), 4 * maxLanes);
            expectEquals (onChannelThree, 4);
        }

        beginTest ("Disabling a lane mid-pattern releases its held notes");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 1, { 1, 1, ClockModifier::straight });
            lane.steps[0].gatePercent = 100;

            rig.block (512);
            expectEquals (countNoteOns (rig.block (512)), 0);   // one step per whole note

            lane.settings.enabled = false;

            const auto events = rig.run (4.5);
            expectEquals (countNoteOffs (events), 1);           // flushed, not stranded
        }

        beginTest ("Enabling a lane mid-playback starts at the current position");
        {
            Rig rig;
            auto& lane = setupLane (rig.pattern(), 0, 4, { 1, 16, ClockModifier::straight });
            lane.settings.enabled = false;

            // A whole quarter note with the lane off, so four steps went by unplayed.
            expectEquals (countNoteOns (rig.run (1.0)), 0);

            lane.settings.enabled = true;

            const auto events = rig.block (4800);       // 0.1 PPQ: less than one step
            expectEquals (countNoteOns (events), 1);    // joined, not the four it missed

            int firstOn = -1;
            for (const auto& event : events)
                if (event.message.isNoteOn())
                {
                    firstOn = event.samplePosition;
                    break;
                }

            expectEquals (firstOn, 0);                  // at the current position, not stacked

            // And it keeps time from there rather than carrying the backlog forward.
            expectEquals (countNoteOns (rig.run (1.0)), 4);
        }

        beginTest ("Scale aware snaps generated notes to the key");
        {
            Rig rig;
            auto& pattern = rig.pattern();
            pattern.scaleEnabled = true;
            pattern.globalScale = ScaleType::major;
            pattern.globalRoot = 0;

            auto& lane = setupLane (pattern, 0, 4, { 1, 8, ClockModifier::straight });
            lane.steps[0].pitch = 1;        // C# -> D
            lane.steps[1].pitch = 3;        // D# -> E
            lane.steps[2].pitch = 0;
            lane.steps[3].pitch = 0;

            expect (noteOnPitches (rig.run (1.9)) == juce::Array<int> { 62, 64, 60, 60 });
        }

        beginTest ("With scale aware off the painted pitch is played exactly");
        {
            Rig rig;
            auto& pattern = rig.pattern();
            pattern.scaleEnabled = false;
            pattern.globalScale = ScaleType::major;      // set, but switched off
            pattern.globalRoot = 0;

            auto& lane = setupLane (pattern, 0, 4, { 1, 8, ClockModifier::straight });
            lane.steps[0].pitch = 1;
            lane.steps[1].pitch = 3;

            const auto pitches = noteOnPitches (rig.run (0.9));
            expectEquals (pitches.size(), 2);

            if (pitches.size() == 2)
            {
                expectEquals (pitches[0], 61);
                expectEquals (pitches[1], 63);
            }
        }

        beginTest ("An adopted pattern is played by reference, not copied");
        {
            Rig rig;

            Pattern external;
            setupLane (external, 0, 4, { 1, 4, ClockModifier::straight });

            // The internal pattern has no enabled lane, so any note can only come from
            // `external`.
            rig.engine.usePattern (&external);
            expectEquals (countNoteOns (rig.run (0.9)), 1);

            // Editing the adopted pattern in place is audible at once, which is only true if
            // the sequencer reads it rather than a copy taken inside the callback.
            for (auto& step : external.lanes[0].steps)
                step.trigger = false;

            expectEquals (countNoteOns (rig.run (4.0)), 0);
        }
    }
};
static SequencerRenderTests sequencerRenderTests;

} // namespace kinetic
