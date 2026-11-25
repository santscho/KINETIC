#pragma once

#include "SequenceMath.h"
#include "SequencerTypes.h"

#include "Timing/HostClock.h"
#include "Timing/MidiEventSink.h"
#include "Timing/NoteTracker.h"

#include <array>

namespace kinetic
{

/**
    Multi-lane sequencer engine.

    Runs entirely inside the audio callback. Each lane merges three timelines by PPQ - the next
    step boundary, queued note-ons (ratchet sub-triggers can land in later blocks) and scheduled
    note-offs - so nothing is ever emitted with a clamped or guessed sample offset.

    Every sub-lane owns its own walker, so trigger, pitch, velocity and mod lengths decouple
    completely (a 4-step gate under a 3-step pitch repeats only after 12 steps), while the
    lane's play direction applies to all of them.

    Every note the sequencer starts is registered with the NoteTracker and carries an absolute
    PPQ off time, so gates, slides and ratchets cannot strand a note across a block boundary.
*/
class Sequencer
{
public:
    static constexpr int maxPendingOffs = 64;
    static constexpr int maxPendingOns = 64;

    Sequencer() = default;

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    Pattern& pattern() noexcept { return patternData; }
    const Pattern& pattern() const noexcept { return patternData; }

    /** Call once per block, after the transport engine has resolved its timing. */
    void processBlock (const BlockTiming& timing, MidiEventSink& sink, NoteTracker& notes) noexcept;

private:
    struct PendingOff
    {
        bool active = false;
        int channel = 1;
        int note = 0;
        double offPpq = 0.0;
    };

    struct PendingOn
    {
        bool active = false;
        int channel = 1;
        int note = 0;
        float velocity = 0.0f;
        double onPpq = 0.0;
        double offPpq = 0.0;
    };

    struct LaneRuntime
    {
        math::StepWalker gateWalk;
        math::StepWalker pitchWalk;
        math::StepWalker velocityWalk;
        math::StepWalker modWalk;

        long long tick = 0;
        long long cycle = 0;
        double nextStepPpq = 0.0;

        std::array<PendingOff, maxPendingOffs> offs {};
        std::array<PendingOn, maxPendingOns> ons {};

        int lastNote = -1;      // last note the lane started (slide source)
        int baseNote = -1;      // note actually held while a bend ramp is active
        int currentBend = 8192;

        bool bendActive = false;
        double bendStartPpq = 0.0;
        double bendEndPpq = 0.0;
        double bendEmittedPpq = 0.0;
        int bendFrom = 8192;
        int bendTo = 8192;
    };

    void resetLanePhases (const BlockTiming& timing) noexcept;
    void clearPending() noexcept;

    double earliestOffPpq (const LaneRuntime& runtime) const noexcept;
    double earliestOnPpq (const LaneRuntime& runtime) const noexcept;
    PendingOff* findEarliestOff (LaneRuntime& runtime) noexcept;
    PendingOn* findEarliestOn (LaneRuntime& runtime) noexcept;

    void emitOff (PendingOff& off, const BlockTiming& timing,
                  MidiEventSink& sink, NoteTracker& notes) noexcept;
    void emitOn (LaneRuntime& runtime, PendingOn& on, const BlockTiming& timing,
                 MidiEventSink& sink, NoteTracker& notes) noexcept;

    void scheduleOff (LaneRuntime& runtime, int channel, int note, double offPpq) noexcept;

    void queueNoteOn (LaneRuntime& runtime, int channel, int note, float velocity,
                      double onPpq, double offPpq, const BlockTiming& timing,
                      MidiEventSink& sink, NoteTracker& notes) noexcept;

    void extendPendingOff (LaneRuntime& runtime, int channel, int note, double offPpq) noexcept;

    void fireStep (int laneIndex, double stepStartPpq, const BlockTiming& timing,
                   MidiEventSink& sink, NoteTracker& notes) noexcept;

    void emitStep (int laneIndex, int channel, double startPpq, int sampleOffset,
                   double stepPpq, const Step& triggerStep, const Step& pitchStep,
                   const Step& velocityStep, const Step& modStep,
                   const BlockTiming& timing, MidiEventSink& sink,
                   NoteTracker& notes) noexcept;

    void emitBendRamps (const BlockTiming& timing, MidiEventSink& sink) noexcept;

    double sampleRate = 48000.0;
    Pattern patternData {};
    LaneRuntime runtime[maxLanes];
    math::Rng rng;

    JUCE_DECLARE_NON_COPYABLE (Sequencer)
};

} // namespace kinetic
