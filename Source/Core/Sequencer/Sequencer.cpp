#include "Sequencer.h"

#include <cmath>

namespace kinetic
{

namespace
{
    int clampChannel (int channel) noexcept { return juce::jlimit (1, 16, channel); }

    int clampLength (int length) noexcept { return juce::jlimit (1, maxSteps, length); }

    /** Velocity multiplier for one sub-trigger of a ratchet group. */
    float ratchetGain (VelocityRamp ramp, int index, int count) noexcept
    {
        if (count <= 1)
            return 1.0f;

        const auto t = static_cast<float> (index + 1) / static_cast<float> (count);

        switch (ramp)
        {
            case VelocityRamp::rampUp:   return 0.45f + 0.55f * t;
            case VelocityRamp::rampDown: return 1.0f - 0.55f * t;
            case VelocityRamp::flat:     break;
        }

        return 1.0f;
    }

    /** Semitone offset -> 14-bit pitch bend, relative to the lane's bend range. */
    int bendValue (double semitones, double rangeSemitones) noexcept
    {
        if (rangeSemitones <= 0.0)
            return 8192;

        const auto value = 8192.0 + (semitones / rangeSemitones) * 8192.0;
        return static_cast<int> (std::lround (juce::jlimit (0.0, 16383.0, value)));
    }

    constexpr double noEventPpq = 1.0e18;
} // namespace

void Sequencer::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
    reset();
}

void Sequencer::reset() noexcept
{
    for (auto& lane : runtime)
        lane = {};

    rng.setSeed (0x9e3779b9u);
}

void Sequencer::resetLanePhases (const BlockTiming& timing) noexcept
{
    const auto& pattern = current();

    for (std::size_t i = 0; i < runtime.size(); ++i)
    {
        const auto& settings = pattern.lanes[i].settings;
        auto& rt = runtime[i];

        // Each sub-lane restarts at the head of each of its own loops.
        const auto startFor = [&settings] (int length)
        {
            const auto clamped = clampLength (length);
            return settings.direction == PlayDirection::reverse ? clamped - 1 : 0;
        };

        rt.gateWalk.reset (startFor (settings.gateLength));
        rt.pitchWalk.reset (startFor (settings.pitchLength));
        rt.velocityWalk.reset (startFor (settings.velocityLength));
        rt.modWalk.reset (startFor (settings.modLength));

        rt.tick = 0;
        rt.nextStepPpq = timing.blockStartPpq;
        rt.pending = {};
        rt.lastNote = -1;
        rt.baseNote = -1;
        rt.currentBend = 8192;
        rt.bendActive = false;
    }
}

void Sequencer::clearPending() noexcept
{
    for (auto& lane : runtime)
        lane.pending = {};
}

Sequencer::PendingNote* Sequencer::nextEvent (LaneRuntime& lane) noexcept
{
    PendingNote* earliest = nullptr;
    auto earliestKey = std::pair { noEventPpq, true };

    for (auto& pending : lane.pending)
    {
        if (pending.isFree())
            continue;

        // Sorting on (ppq, onSent) puts every release before the note-ons sharing its instant.
        const auto key = std::pair { pending.nextPpq(), pending.onSent };

        if (earliest == nullptr || key < earliestKey)
        {
            earliest = &pending;
            earliestKey = key;
        }
    }

    return earliest;
}

void Sequencer::emitNext (PendingNote& pending, const BlockTiming& timing,
                          MidiEventSink& sink, NoteTracker& notes) noexcept
{
    const auto offset = timing.sampleOffsetForPpq (juce::jmax (pending.nextPpq(),
                                                               timing.blockStartPpq));

    if (! pending.onSent)
    {
        notes.noteOn (sink, pending.channel, pending.note, pending.velocity, offset);
        pending.onSent = true;
        return;
    }

    notes.noteOff (sink, pending.channel, pending.note, offset);
    pending.channel = 0;
}

void Sequencer::queueNote (LaneRuntime& lane, int channel, int note, float velocity,
                           double onPpq, double offPpq) noexcept
{
    for (auto& pending : lane.pending)
    {
        if (pending.isFree())
        {
            pending = { channel, note, velocity, onPpq, offPpq, false };
            return;
        }
    }

    // Unreachable with 32 slots against one step's eight ratchets plus the gate still
    // sounding. Dropping the sub-trigger is the only safe failure: a note emitted without a
    // stored release would outlive its step.
}

void Sequencer::extendRelease (LaneRuntime& lane, int channel, int note, double offPpq) noexcept
{
    for (auto& pending : lane.pending)
        if (! pending.isFree() && pending.onSent
             && pending.channel == channel && pending.note == note)
            pending.offPpq = juce::jmax (pending.offPpq, offPpq);
}

void Sequencer::emitBendRamps (const BlockTiming& timing, MidiEventSink& sink) noexcept
{
    for (std::size_t i = 0; i < runtime.size(); ++i)
    {
        auto& rt = runtime[i];

        if (! rt.bendActive)
            continue;

        const auto span = rt.bendEndPpq - rt.bendStartPpq;

        if (span <= 0.0)
        {
            rt.bendActive = false;
            continue;
        }

        const auto channel = clampChannel (current().lanes[i].settings.midiChannel);

        const auto bendTo = [&] (double ppq, double t)
        {
            rt.currentBend = juce::jlimit (0, 16383, static_cast<int> (std::lround (
                static_cast<double> (rt.bendFrom)
                    + static_cast<double> (rt.bendTo - rt.bendFrom) * t)));

            sink.add (juce::MidiMessage::pitchWheel (channel, rt.currentBend),
                      timing.sampleOffsetForPpq (ppq));
        };

        const auto from = juce::jmax (rt.bendStartPpq, timing.blockStartPpq);
        const auto to = juce::jmin (rt.bendEndPpq, timing.blockEndPpq);

        // Roughly one message per millisecond, then land exactly on the target.
        const auto increment = timing.ppqPerSample * juce::jmax (1.0, sampleRate / 1000.0);

        for (auto ppq = from; ppq < to; ppq += increment)
            bendTo (ppq, (ppq - rt.bendStartPpq) / span);

        if (timing.blockEndPpq >= rt.bendEndPpq)
        {
            bendTo (rt.bendEndPpq, 1.0);
            rt.bendActive = false;
        }
    }
}

void Sequencer::processBlock (const BlockTiming& timing, MidiEventSink& sink,
                              NoteTracker& notes) noexcept
{
    if (! timing.isPlaying)
    {
        clearPending();
        return;
    }

    if (timing.didStart || timing.didJump)
        resetLanePhases (timing);

    for (std::size_t i = 0; i < runtime.size(); ++i)
    {
        auto& rt = runtime[i];
        const auto& lane = current().lanes[i];
        const bool stepsAllowed = lane.settings.enabled;

        // A lane switched on while the transport already runs joins at the current position.
        // Its step cursor sat frozen where it stopped, so replaying from there would fire every
        // missed step at sample offset 0 and arrive as one burst. A cursor that is still ahead
        // of the block is left alone: that step has not been missed yet.
        if (stepsAllowed && ! rt.enabled && ! timing.didStart && ! timing.didJump
             && rt.nextStepPpq < timing.blockStartPpq)
            rt.nextStepPpq = timing.blockStartPpq;

        // One timeline per lane: scheduled notes and step boundaries, whichever is next.
        for (int guard = 0; guard < 8192; ++guard)
        {
            auto* pending = nextEvent (rt);
            const auto notePpq = pending != nullptr ? pending->nextPpq() : noEventPpq;
            const bool stepDue = stepsAllowed && rt.nextStepPpq < timing.blockEndPpq;

            if (notePpq >= timing.blockEndPpq && ! stepDue)
                break;

            if (! stepDue || notePpq <= rt.nextStepPpq)
            {
                if (pending == nullptr)
                    break;

                emitNext (*pending, timing, sink, notes);
            }
            else
            {
                fireStep (lane, rt, rt.nextStepPpq, timing, sink);
            }
        }

        rt.enabled = stepsAllowed;
    }

    // Flushed last: a ramp that started part-way through this block still gets its messages,
    // and the sink sorts by sample offset so the host buffer is unaffected.
    emitBendRamps (timing, sink);
}

void Sequencer::fireStep (const Lane& lane, LaneRuntime& rt, double stepStartPpq,
                          const BlockTiming& timing, MidiEventSink& sink) noexcept
{
    const auto& settings = lane.settings;

    const auto gateLength = clampLength (settings.gateLength);
    const auto stepPpq = settings.clock.stepPpq();
    const auto index = rt.gateWalk.current();
    const auto& step = stepAt (lane, index);

    const bool triggered = settings.euclidEnabled
                             ? math::euclidBitRotated (index, settings.euclidSteps,
                                                       settings.euclidPulses, settings.euclidRotation)
                             : step.trigger;

    // A cycle is one pass of the trigger pattern, so it is derived from the tick count
    // rather than maintained beside it.
    const auto cycle = static_cast<long long> (rt.tick) / gateLength;

    if (triggered
         && math::conditionPasses (step.condition, cycle)
         && (step.probability >= 100
              || rng.nextFloat() * 100.0f < static_cast<float> (step.probability)))
    {
        const auto startPpq = stepStartPpq
                                + stepPpq * (static_cast<double> (step.microTiming) / 100.0);

        emitStep (lane, rt, startPpq, timing, sink);
    }

    ++rt.tick;

    rt.gateWalk.advance (gateLength, settings.direction, rng);
    rt.pitchWalk.advance (clampLength (settings.pitchLength), settings.direction, rng);
    rt.velocityWalk.advance (clampLength (settings.velocityLength), settings.direction, rng);
    rt.modWalk.advance (clampLength (settings.modLength), settings.direction, rng);

    rt.nextStepPpq = stepStartPpq + stepPpq;
}

void Sequencer::emitStep (const Lane& lane, LaneRuntime& rt, double startPpq,
                          const BlockTiming& timing, MidiEventSink& sink) noexcept
{
    const auto& pattern = current();
    const auto& settings = lane.settings;

    const auto channel = clampChannel (settings.midiChannel);
    const auto offset = timing.sampleOffsetForPpq (startPpq);
    const auto stepPpq = settings.clock.stepPpq();

    // The walkers still point at this step, so the four sub-lanes are read here.
    const auto& triggerStep = stepAt (lane, rt.gateWalk.current());
    const auto& pitchStep = stepAt (lane, rt.pitchWalk.current());
    const auto& velocityStep = stepAt (lane, rt.velocityWalk.current());
    const auto& modStep = stepAt (lane, rt.modWalk.current());

    if (settings.type == LaneType::cc)
    {
        sink.addController (channel,
                            juce::jlimit (0, 127, settings.modController),
                            juce::jlimit (0, 127, static_cast<int> (modStep.mod)),
                            offset);
        return;
    }

    // Scale aware is the master switch: switched off, every note lands exactly on the painted
    // pitch whatever the lane or global scale says, so the quantiser is a no-op.
    const auto mask = pattern.scaleEnabled
                          ? math::scaleMask (settings.useGlobalScale ? pattern.globalScale
                                                                     : settings.scale,
                                             pattern.customScaleMask)
                          : static_cast<juce::uint16> (0x0fff);
    const auto root = settings.useGlobalScale ? pattern.globalRoot : settings.scaleRoot;

    const auto note = juce::jlimit (0, 127,
                                    math::quantise (settings.rootNote + static_cast<int> (pitchStep.pitch),
                                                    mask, root));

    auto velocity = static_cast<float> (juce::jlimit (0, 127, static_cast<int> (velocityStep.velocity)))
                        / 127.0f;

    if (triggerStep.accent)
    {
        velocity = juce::jmin (1.0f,
                               velocity + static_cast<float> (settings.accentVelocityBoost) / 127.0f);

        sink.addController (channel,
                            juce::jlimit (0, 127, settings.accentCC),
                            juce::jlimit (0, 127, settings.accentCCValue),
                            offset);
    }

    const bool sliding = triggerStep.slide && rt.lastNote >= 0;

    // Slide, mode 3: keep the held note and bend its pitch instead of retriggering.
    if (sliding && settings.slideMode == SlideMode::pitchBendRamp)
    {
        rt.bendFrom = rt.currentBend;
        rt.bendTo = bendValue (static_cast<double> (note - rt.baseNote),
                               settings.bendRangeSemitones);
        rt.bendStartPpq = startPpq;
        rt.bendEndPpq = startPpq + stepPpq;
        rt.bendActive = true;
        rt.lastNote = note;

        sink.add (juce::MidiMessage::pitchWheel (channel, rt.currentBend), offset);
        return;
    }

    // Slide, mode 2: hand the glide to the synth's portamento.
    if (settings.slideMode == SlideMode::portamentoCC65)
        sink.addController (channel, 65, sliding ? 127 : 0, offset);

    const auto overlapPpq = (settings.slideOverlapMs / 1000.0) * (timing.bpm / 60.0);

    // Slide, mode 1: let the previous note ring past this step's note-on.
    if (sliding && settings.slideMode == SlideMode::legatoOverlap)
        extendRelease (rt, channel, rt.lastNote, startPpq + overlapPpq);

    // The overlap has to be applied when the release is *stored*: by the time the next step
    // fires, this note's release may already have been emitted. Look ahead instead.
    const auto nextSlides = settings.slideMode == SlideMode::legatoOverlap
                              && stepAt (lane, rt.gateWalk.peekNext (clampLength (settings.gateLength),
                                                                    settings.direction)).slide;

    const auto ratchets = juce::jlimit (1, 8, static_cast<int> (triggerStep.ratchets));
    const auto ratchetPpq = stepPpq / static_cast<double> (ratchets);
    const auto gateFraction = juce::jlimit (0.02, 1.0,
                                            static_cast<double> (triggerStep.gatePercent) / 100.0);

    if (rt.currentBend != 8192)
    {
        sink.add (juce::MidiMessage::pitchWheel (channel, 8192), offset);
        rt.currentBend = 8192;
    }

    for (int r = 0; r < ratchets; ++r)
    {
        const auto subStart = startPpq + ratchetPpq * static_cast<double> (r);
        const auto gain = ratchetGain (triggerStep.ratchetRamp, r, ratchets);

        auto offPpq = subStart + ratchetPpq * gateFraction;

        if (nextSlides && r == ratchets - 1)
            offPpq = startPpq + stepPpq + overlapPpq;

        // Scheduled, not emitted: sub-triggers of a long step belong to later blocks, and a
        // clamped offset would both mis-time them and make the tracker swallow the retrigger.
        queueNote (rt, channel, note, juce::jlimit (0.0f, 1.0f, velocity * gain), subStart, offPpq);
    }

    rt.lastNote = note;
    rt.baseNote = note;
}

} // namespace kinetic
