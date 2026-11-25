#pragma once

#include "SequenceMath.h"
#include "SequencerTypes.h"

#include "Timing/HostClock.h"
#include "Timing/MidiEventSink.h"
#include "Timing/NoteTracker.h"

#include <array>
#include <utility>

namespace kinetic
{

/**
    Multi-lane sequencer engine.

    Runs entirely inside the audio callback. Each lane keeps one list of scheduled notes and
    merges it with the next step boundary by PPQ, so a step, its note-ons and their releases
    interleave across blocks without any sample offset being clamped or guessed.

    A scheduled note owns its on and its release together, so a note-on can never be emitted
    whose release was never stored - the failure that leaves a note hanging.

    Every sub-lane owns its own walker, so trigger, pitch, velocity and mod lengths decouple
    completely (a 4-step gate over a 3-step pitch repeats only after 12 steps), while the
    lane's play direction applies to all of them.
*/
class Sequencer
{
public:
    Sequencer() = default;

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;

    /** The internal pattern. Offline use and tests edit this directly. */
    Pattern& pattern() noexcept { return patternData; }
    const Pattern& pattern() const noexcept { return patternData; }

    /**
        Audio thread only: play `published` by reference from the next block on.

        Nothing is copied. `published` must stay valid until the next call, which is exactly
        what SharedPattern guarantees for the slot it most recently handed out: that slot is
        not returned to the writer until the reader exchanges again.

        Passing nullptr goes back to the internal pattern.
    */
    void usePattern (const Pattern* published) noexcept { adoptedPattern = published; }

    /** What the sequencer is reading: the adopted snapshot, or the internal pattern. */
    const Pattern& current() const noexcept
    {
        return adoptedPattern != nullptr ? *adoptedPattern : patternData;
    }

    /** Call once per block, after the transport engine has resolved its timing. */
    void processBlock (const BlockTiming& timing, MidiEventSink& sink, NoteTracker& notes) noexcept;

private:
    static constexpr int maxPendingNotes = 32;

    /** One scheduled note: its on and its release, so neither can exist without the other. */
    struct PendingNote
    {
        int channel = 0;             // 0 marks a free slot
        int note = 0;
        float velocity = 0.0f;
        double onPpq = 0.0;
        double offPpq = 0.0;
        bool onSent = false;

        bool isFree() const noexcept { return channel == 0; }

        /** The next thing this note contributes to the lane's timeline. */
        double nextPpq() const noexcept { return onSent ? offPpq : onPpq; }
    };

    struct LaneRuntime
    {
        math::StepWalker gateWalk;
        math::StepWalker pitchWalk;
        math::StepWalker velocityWalk;
        math::StepWalker modWalk;

        long long tick = 0;
        double nextStepPpq = 0.0;
        bool enabled = false;   // last enable state the audio thread saw, for joining late

        std::array<PendingNote, maxPendingNotes> pending {};

        int lastNote = -1;      // last note the lane started (slide source)
        int baseNote = -1;      // note held while a bend ramp is active
        int currentBend = 8192;

        bool bendActive = false;
        double bendStartPpq = 0.0;
        double bendEndPpq = 0.0;
        int bendFrom = 8192;
        int bendTo = 8192;
    };

    void resetLanePhases (const BlockTiming& timing) noexcept;
    void clearPending() noexcept;

    /** Step lookup that also clamps, so a stale index can never read outside the pattern. */
    static const Step& stepAt (const Lane& lane, int index) noexcept
    {
        return lane.steps[static_cast<std::size_t> (juce::jlimit (0, maxSteps - 1, index))];
    }

    /** Earliest due note in the lane; a release sorts before a note-on at the same PPQ. */
    PendingNote* nextEvent (LaneRuntime& runtime) noexcept;

    void emitNext (PendingNote& pending, const BlockTiming& timing,
                   MidiEventSink& sink, NoteTracker& notes) noexcept;

    void queueNote (LaneRuntime& runtime, int channel, int note, float velocity,
                    double onPpq, double offPpq) noexcept;

    void extendRelease (LaneRuntime& runtime, int channel, int note, double offPpq) noexcept;

    void fireStep (const Lane& lane, LaneRuntime& runtime, double stepStartPpq,
                   const BlockTiming& timing, MidiEventSink& sink) noexcept;

    void emitStep (const Lane& lane, LaneRuntime& runtime, double startPpq,
                   const BlockTiming& timing, MidiEventSink& sink) noexcept;

    void emitBendRamps (const BlockTiming& timing, MidiEventSink& sink) noexcept;

    double sampleRate = 48000.0;
    Pattern patternData {};
    const Pattern* adoptedPattern = nullptr;
    std::array<LaneRuntime, maxLanes> runtime {};    math::Rng rng;

    JUCE_DECLARE_NON_COPYABLE (Sequencer)
};

} // namespace kinetic
