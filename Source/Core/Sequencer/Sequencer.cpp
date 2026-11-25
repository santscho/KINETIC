#include "Sequencer.h"

#include <cmath>

namespace kinetic
{

namespace
{
    int clampChannel (int channel) noexcept { return juce::jlimit (1, 16, channel); }

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
    for (int i = 0; i < maxLanes; ++i)
    {
        auto& rt = runtime[static_cast<std::size_t> (i)];
        const auto& settings = patternData.lanes[static_cast<std::size_t> (i)].settings;

        const auto gateLength = juce::jlimit (1, maxSteps, settings.gateLength);
        const auto pitchLength = juce::jlimit (1, maxSteps, settings.pitchLength);
        const auto velocityLength = juce::jlimit (1, maxSteps, settings.velocityLength);
        const auto modLength = juce::jlimit (1, maxSteps, settings.modLength);

        // A reversed lane starts at the end of each of its loops.
        const auto startFor = [&settings] (int length)
        {
            return settings.direction == PlayDirection::reverse ? length - 1 : 0;
        };

        rt.gateWalk.reset (startFor (gateLength));
        rt.pitchWalk.reset (startFor (pitchLength));
        rt.velocityWalk.reset (startFor (velocityLength));
        rt.modWalk.reset (startFor (modLength));

        rt.tick = 0;
        rt.cycle = 0;
        rt.nextStepPpq = timing.blockStartPpq;
        rt.offs = {};
        rt.ons = {};
        rt.lastNote = -1;
        rt.baseNote = -1;
        rt.currentBend = 8192;
        rt.bendActive = false;
    }
}

void Sequencer::clearPending() noexcept
{
    for (auto& lane : runtime)
    {
        lane.offs = {};
        lane.ons = {};
    }
}

double Sequencer::earliestOffPpq (const LaneRuntime& lane) const noexcept
{
    double earliest = noEventPpq;

    for (const auto& off : lane.offs)
        if (off.active && off.offPpq < earliest)
            earliest = off.offPpq;

    return earliest;
}

double Sequencer::earliestOnPpq (const LaneRuntime& lane) const noexcept
{
    double earliest = noEventPpq;

    for (const auto& on : lane.ons)
        if (on.active && on.onPpq < earliest)
            earliest = on.onPpq;

    return earliest;
}

Sequencer::PendingOff* Sequencer::findEarliestOff (LaneRuntime& lane) noexcept
{
    PendingOff* earliest = nullptr;

    for (auto& off : lane.offs)
        if (off.active && (earliest == nullptr || off.offPpq < earliest->offPpq))
            earliest = &off;

    return earliest;
}

Sequencer::PendingOn* Sequencer::findEarliestOn (LaneRuntime& lane) noexcept
{
    PendingOn* earliest = nullptr;

    for (auto& on : lane.ons)
        if (on.active && (earliest == nullptr || on.onPpq < earliest->onPpq))
            earliest = &on;

    return earliest;
}

void Sequencer::emitOff (PendingOff& off, const BlockTiming& timing,
                         MidiEventSink& sink, NoteTracker& notes) noexcept
{
    const auto ppq = juce::jmax (off.offPpq, timing.blockStartPpq);
    notes.noteOff (sink, off.channel, off.note, timing.sampleOffsetForPpq (ppq));
    off.active = false;
}

void Sequencer::emitOn (LaneRuntime& lane, PendingOn& on, const BlockTiming& timing,
                        MidiEventSink& sink, NoteTracker& notes) noexcept
{
    const auto ppq = juce::jmax (on.onPpq, timing.blockStartPpq);
    notes.noteOn (sink, on.channel, on.note, on.velocity, timing.sampleOffsetForPpq (ppq));

    // Hand the release over to the off timeline so the note is never left sounding.
    scheduleOff (lane, on.channel, on.note, on.offPpq);
    on.active = false;
}

void Sequencer::scheduleOff (LaneRuntime& lane, int channel, int note, double offPpq) noexcept
{
    for (auto& off : lane.offs)
    {
        if (! off.active)
        {
            off = { true, channel, note, offPpq };
            return;
        }
    }

    // Deliberately drop rather than mis-time: the note stays tracked, and the transport
    // engine's panic path (or the next stop) is the safety net.
}

void Sequencer::queueNoteOn (LaneRuntime& lane, int channel, int note, float velocity,
                             double onPpq, double offPpq, const BlockTiming& timing,
                             MidiEventSink& sink, NoteTracker& notes) noexcept
{
    for (auto& on : lane.ons)
    {
        if (! on.active)
        {
            on = { true, channel, note, velocity, onPpq, offPpq };
            return;
        }
    }

    // Queue full (should not happen with 8 ratchets and 64 slots): sound the note now at the
    // block boundary so it is never silently lost, then release it.
    notes.noteOn (sink, channel, note, velocity, 0);
    notes.noteOff (sink, channel, note, timing.sampleOffsetForPpq (juce::jmax (offPpq,
                                                                               timing.blockStartPpq)));
}

void Sequencer::extendPendingOff (LaneRuntime& lane, int channel, int note, double offPpq) noexcept
{
    for (auto& off : lane.offs)
        if (off.active && off.channel == channel && off.note == note)
            off.offPpq = juce::jmax (off.offPpq, offPpq);
}

void Sequencer::emitBendRamps (const BlockTiming& timing, MidiEventSink& sink) noexcept
{
    for (int i = 0; i < maxLanes; ++i)
    {
        auto& rt = runtime[static_cast<std::size_t> (i)];
        const auto& settings = patternData.lanes[static_cast<std::size_t> (i)].settings;

        if (! rt.bendActive)
            continue;

        const auto channel = clampChannel (settings.midiChannel);
        const auto duration = rt.bendEndPpq - rt.bendStartPpq;

        const auto emitAt = [&] (double ppq)
        {
            const auto t = duration > 0.0
                             ? juce::jlimit (0.0, 1.0, (ppq - rt.bendStartPpq) / duration)
                             : 1.0;

            const auto value = static_cast<int> (std::lround (
                static_cast<double> (rt.bendFrom)
                    + static_cast<double> (rt.bendTo - rt.bendFrom) * t));

            rt.currentBend = juce::jlimit (0, 16383, value);
            sink.add (juce::MidiMessage::pitchWheel (channel, rt.currentBend),
                      timing.sampleOffsetForPpq (ppq));
        };

        const auto from = juce::jmax (rt.bendStartPpq, timing.blockStartPpq);
        const auto to = juce::jmin (rt.bendEndPpq, timing.blockEndPpq);

        if (to <= from)
        {
            if (timing.blockStartPpq >= rt.bendEndPpq)
            {
                rt.currentBend = rt.bendTo;
                rt.bendActive = false;
            }

            continue;
        }

        // Roughly one message per millisecond, plus the exact end point.
        const auto increment = juce::jmax (timing.ppqPerSample * (sampleRate / 1000.0),
                                           timing.ppqPerSample);

        if (increment > 0.0)
            for (auto ppq = juce::jmax (from, rt.bendEmittedPpq); ppq < to; ppq += increment)
                emitAt (ppq);

        rt.bendEmittedPpq = to;

        if (to >= rt.bendEndPpq)
        {
            emitAt (rt.bendEndPpq);
            rt.bendActive = false;
            rt.currentBend = rt.bendTo;
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

    for (int laneIndex = 0; laneIndex < maxLanes; ++laneIndex)
    {
        const auto& lane = patternData.lanes[static_cast<std::size_t> (laneIndex)];
        auto& rt = runtime[static_cast<std::size_t> (laneIndex)];

        // Merge note-offs, queued note-ons and step boundaries by PPQ. Offs win ties so a
        // re-trigger always sees the previous note released.
        for (int guard = 0; guard < 8192; ++guard)
        {
            const auto offPpq = earliestOffPpq (rt);
            const auto onPpq = earliestOnPpq (rt);
            const auto stepPpq = rt.nextStepPpq;

            const bool canStep = lane.settings.enabled && stepPpq < timing.blockEndPpq;

            const auto nextOff = offPpq < timing.blockEndPpq;
            const auto nextOn = onPpq < timing.blockEndPpq;

            if (! canStep && ! nextOff && ! nextOn)
                break;

            if (nextOff && offPpq <= onPpq && (! canStep || offPpq <= stepPpq))
            {
                if (auto* off = findEarliestOff (rt))
                    emitOff (*off, timing, sink, notes);
                else
                    break;
            }
            else if (nextOn && (! canStep || onPpq <= stepPpq))
            {
                if (auto* on = findEarliestOn (rt))
                    emitOn (rt, *on, timing, sink, notes);
                else
                    break;
            }
            else
            {
                fireStep (laneIndex, stepPpq, timing, sink, notes);
            }
        }
    }

    // Flushed last: a slide that started part-way through this block still gets its ramp,
    // and the sink sorts by sample offset so ordering in the host buffer is unaffected.
    emitBendRamps (timing, sink);
}

void Sequencer::fireStep (int laneIndex, double stepStartPpq, const BlockTiming& timing,
                          MidiEventSink& sink, NoteTracker& notes) noexcept
{
    const auto& lane = patternData.lanes[static_cast<std::size_t> (laneIndex)];
    const auto& settings = lane.settings;
    auto& rt = runtime[static_cast<std::size_t> (laneIndex)];

    const auto gateLength = juce::jlimit (1, maxSteps, settings.gateLength);
    const auto pitchLength = juce::jlimit (1, maxSteps, settings.pitchLength);
    const auto velocityLength = juce::jlimit (1, maxSteps, settings.velocityLength);
    const auto modLength = juce::jlimit (1, maxSteps, settings.modLength);

    const auto laneStep = rt.gateWalk.current();

    const auto& triggerStep = lane.steps[static_cast<std::size_t> (laneStep)];
    const auto& pitchStep = lane.steps[static_cast<std::size_t> (rt.pitchWalk.current())];
    const auto& velocityStep = lane.steps[static_cast<std::size_t> (rt.velocityWalk.current())];
    const auto& modStep = lane.steps[static_cast<std::size_t> (rt.modWalk.current())];

    const auto channel = clampChannel (settings.midiChannel);
    const auto stepPpq = settings.clock.stepPpq();
    const auto startPpq = stepStartPpq
                            + stepPpq * (static_cast<double> (triggerStep.microTiming) / 100.0);
    const auto sampleOffset = timing.sampleOffsetForPpq (startPpq);

    const bool triggered = settings.euclidEnabled
                             ? math::euclidBitRotated (laneStep, settings.euclidSteps,
                                                       settings.euclidPulses,
                                                       settings.euclidRotation)
                             : triggerStep.trigger;

    if (triggered && math::conditionPasses (triggerStep.condition, rt.cycle))
    {
        const bool probabilityHit = triggerStep.probability >= 100
                                      || rng.nextFloat() * 100.0f
                                           < static_cast<float> (triggerStep.probability);

        if (probabilityHit)
            emitStep (laneIndex, channel, startPpq, sampleOffset, stepPpq,
                      triggerStep, pitchStep, velocityStep, modStep,
                      timing, sink, notes);
    }

    ++rt.tick;

    if ((rt.tick % static_cast<long long> (gateLength)) == 0)
        ++rt.cycle;

    rt.gateWalk.advance (gateLength, settings.direction, rng);
    rt.pitchWalk.advance (pitchLength, settings.direction, rng);
    rt.velocityWalk.advance (velocityLength, settings.direction, rng);
    rt.modWalk.advance (modLength, settings.direction, rng);

    rt.nextStepPpq = stepStartPpq + stepPpq;
}

void Sequencer::emitStep (int laneIndex, int channel, double startPpq, int sampleOffset,
                          double stepPpq, const Step& triggerStep, const Step& pitchStep,
                          const Step& velocityStep, const Step& modStep,
                          const BlockTiming& timing, MidiEventSink& sink,
                          NoteTracker& notes) noexcept
{
    const auto& settings = patternData.lanes[static_cast<std::size_t> (laneIndex)].settings;
    auto& rt = runtime[static_cast<std::size_t> (laneIndex)];

    if (settings.type == LaneType::cc)
    {
        sink.addController (channel,
                            juce::jlimit (0, 127, settings.modController),
                            juce::jlimit (0, 127, static_cast<int> (modStep.mod)),
                            sampleOffset);
        return;
    }

    const auto mask = settings.useGlobalScale
                        ? math::scaleMask (patternData.globalScale, patternData.customScaleMask)
                        : math::scaleMask (settings.scale, patternData.customScaleMask);
    const auto root = settings.useGlobalScale ? patternData.globalRoot : settings.scaleRoot;

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
                            sampleOffset);
    }

    const bool slideActive = triggerStep.slide && rt.lastNote >= 0;

    // Slide, mode 3: keep the held note and bend its pitch instead of retriggering.
    if (slideActive && settings.slideMode == SlideMode::pitchBendRamp)
    {
        rt.bendFrom = rt.currentBend;
        rt.bendTo = bendValue (static_cast<double> (note - rt.baseNote),
                               settings.bendRangeSemitones);
        rt.bendStartPpq = startPpq;
        rt.bendEndPpq = startPpq + stepPpq;
        rt.bendEmittedPpq = startPpq;
        rt.bendActive = true;
        rt.lastNote = note;

        sink.add (juce::MidiMessage::pitchWheel (channel, rt.currentBend), sampleOffset);
        return;
    }

    // Slide, mode 2: hand the glide to the synth's portamento.
    if (settings.slideMode == SlideMode::portamentoCC65)
        sink.addController (channel, 65, slideActive ? 127 : 0, sampleOffset);

    const auto overlapPpq = (settings.slideOverlapMs / 1000.0) * (timing.bpm / 60.0);

    // Slide, mode 1: let the previous note ring past this step's note-on.
    if (slideActive && settings.slideMode == SlideMode::legatoOverlap)
        extendPendingOff (rt, channel, rt.lastNote, startPpq + overlapPpq);

    // Overlap has to be decided when the off is *scheduled*: by the time the next step fires,
    // this note's off may already have been emitted. Look ahead instead.
    const auto gateLength = juce::jlimit (1, maxSteps, settings.gateLength);
    const auto nextStepIndex = rt.gateWalk.peekNext (gateLength, settings.direction);
    const auto nextSlides = settings.slideMode == SlideMode::legatoOverlap
                              && patternData.lanes[static_cast<std::size_t> (laneIndex)]
                                     .steps[static_cast<std::size_t> (nextStepIndex)].slide;

    const auto ratchets = juce::jlimit (1, 8, static_cast<int> (triggerStep.ratchets));
    const auto ratchetPpq = stepPpq / static_cast<double> (ratchets);
    const auto gateFraction = juce::jlimit (0.02, 1.0,
                                            static_cast<double> (triggerStep.gatePercent) / 100.0);

    if (rt.currentBend != 8192)
    {
        sink.add (juce::MidiMessage::pitchWheel (channel, 8192), sampleOffset);
        rt.currentBend = 8192;
    }

    for (int r = 0; r < ratchets; ++r)
    {
        const auto subStart = startPpq + ratchetPpq * static_cast<double> (r);
        const auto gain = ratchetGain (triggerStep.ratchetRamp, r, ratchets);

        auto offPpq = subStart + ratchetPpq * gateFraction;

        if (nextSlides && r == ratchets - 1)
            offPpq = startPpq + stepPpq + overlapPpq;

        // Queued rather than emitted: sub-triggers of a long step land in later blocks, and a
        // clamped offset would both mis-time them and make the tracker suppress the re-trigger.
        queueNoteOn (rt, channel, note, juce::jlimit (0.0f, 1.0f, velocity * gain),
                     subStart, offPpq, timing, sink, notes);
    }

    rt.lastNote = note;
    rt.baseNote = note;
}

} // namespace kinetic
