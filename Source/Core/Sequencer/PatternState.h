#pragma once

#include "SequencerTypes.h"

#include <juce_core/juce_core.h>

namespace kinetic::patternstate
{

/**
    Save/restore of the whole pattern, plus which lane the editor was showing.

    Only step fields that differ from a default Step are written, so an untouched pattern
    serialises to eight nearly empty elements instead of 512 fully populated ones. Reading
    starts from a default Pattern, which means a restored session sounds exactly like the one
    that was saved and never inherits leftovers from whatever was loaded before.
*/
inline constexpr int formatVersion = 1;
inline constexpr const char* elementTag = "PATTERN";

inline void writeToXml (const Pattern& pattern, int selectedLane, juce::XmlElement& parent)
{
    auto* element = parent.createNewChildElement (elementTag);

    element->setAttribute ("format", formatVersion);
    element->setAttribute ("selectedLane", juce::jlimit (0, maxLanes - 1, selectedLane));
    element->setAttribute ("rootNote", pattern.rootNote);
    element->setAttribute ("globalScale", static_cast<int> (pattern.globalScale));
    element->setAttribute ("globalRoot", pattern.globalRoot);
    element->setAttribute ("customMask", static_cast<int> (pattern.customScaleMask));
    element->setAttribute ("scaleEnabled", pattern.scaleEnabled ? 1 : 0);

    const Step defaults;

    for (int laneIndex = 0; laneIndex < maxLanes; ++laneIndex)
    {
        const auto& lane = pattern.lanes[static_cast<std::size_t> (laneIndex)];
        const auto& settings = lane.settings;

        auto* laneElement = element->createNewChildElement ("lane");

        laneElement->setAttribute ("index", laneIndex);
        laneElement->setAttribute ("enabled", settings.enabled ? 1 : 0);
        laneElement->setAttribute ("type", static_cast<int> (settings.type));
        laneElement->setAttribute ("channel", settings.midiChannel);
        laneElement->setAttribute ("clockNum", settings.clock.numerator);
        laneElement->setAttribute ("clockDen", settings.clock.denominator);
        laneElement->setAttribute ("clockMod", static_cast<int> (settings.clock.modifier));
        laneElement->setAttribute ("length", settings.gateLength);
        laneElement->setAttribute ("pitchLength", settings.pitchLength);
        laneElement->setAttribute ("velocityLength", settings.velocityLength);
        laneElement->setAttribute ("modLength", settings.modLength);
        laneElement->setAttribute ("controller", settings.modController);
        laneElement->setAttribute ("direction", static_cast<int> (settings.direction));
        laneElement->setAttribute ("euclid", settings.euclidEnabled ? 1 : 0);
        laneElement->setAttribute ("euclidSteps", settings.euclidSteps);
        laneElement->setAttribute ("euclidPulses", settings.euclidPulses);
        laneElement->setAttribute ("euclidRotation", settings.euclidRotation);
        laneElement->setAttribute ("slideMode", static_cast<int> (settings.slideMode));
        laneElement->setAttribute ("bendRange", settings.bendRangeSemitones);
        laneElement->setAttribute ("overlapMs", settings.slideOverlapMs);
        laneElement->setAttribute ("accentCC", settings.accentCC);
        laneElement->setAttribute ("accentCCValue", settings.accentCCValue);
        laneElement->setAttribute ("accentBoost", settings.accentVelocityBoost);

        for (int stepIndex = 0; stepIndex < maxSteps; ++stepIndex)
        {
            const auto& step = lane.steps[static_cast<std::size_t> (stepIndex)];

            auto* stepElement = laneElement->createNewChildElement ("step");

            // The index is always explicit; everything else only when it is not the default.
            stepElement->setAttribute ("i", stepIndex);

            if (step.trigger)
                stepElement->setAttribute ("trig", 1);

            if (step.velocity != defaults.velocity)
                stepElement->setAttribute ("vel", static_cast<int> (step.velocity));

            if (step.gatePercent != defaults.gatePercent)
                stepElement->setAttribute ("gate", static_cast<int> (step.gatePercent));

            if (step.microTiming != defaults.microTiming)
                stepElement->setAttribute ("micro", static_cast<int> (step.microTiming));

            if (step.probability != defaults.probability)
                stepElement->setAttribute ("prob", static_cast<int> (step.probability));

            if (step.condition != defaults.condition)
                stepElement->setAttribute ("cond", static_cast<int> (step.condition));

            if (step.ratchets != defaults.ratchets)
                stepElement->setAttribute ("ratchets", static_cast<int> (step.ratchets));

            if (step.ratchetRamp != defaults.ratchetRamp)
                stepElement->setAttribute ("ramp", static_cast<int> (step.ratchetRamp));

            if (step.slide)
                stepElement->setAttribute ("slide", 1);

            if (step.accent)
                stepElement->setAttribute ("accent", 1);

            if (step.pitch != defaults.pitch)
                stepElement->setAttribute ("pitch", static_cast<int> (step.pitch));

            if (step.mod != defaults.mod)
                stepElement->setAttribute ("mod", static_cast<int> (step.mod));
        }
    }
}

/**
    Restores a pattern written by writeToXml(). False when `parent` holds no pattern, or holds one
    written in a format version this build does not understand; in both cases `pattern` and
    `selectedLane` are left untouched.
*/
inline bool readFromXml (const juce::XmlElement& parent, Pattern& pattern, int& selectedLane)
{
    const auto* element = parent.getChildByName (elementTag);

    if (element == nullptr)
        return false;

    // States written before the attribute existed are format 1. Anything else is refused rather
    // than parsed on a guess: a future format may move or reinterpret fields, and misreading them
    // would silently change how the session sounds.
    if (element->getIntAttribute ("format", formatVersion) != formatVersion)
        return false;

    // A restore replaces the pattern outright, so a lane the file does not mention stays silent
    // rather than keeping whatever this session happened to have.
    pattern = Pattern {};

    selectedLane = juce::jlimit (0, maxLanes - 1,
                                 element->getIntAttribute ("selectedLane", selectedLane));

    // The root note used to be stored per lane; an older session keeps the first lane's value so
    // loading one does not transpose the pattern behind the player's back.
    const auto* firstLaneElement = element->getChildByName ("lane");

    pattern.rootNote = element->getIntAttribute (
        "rootNote", firstLaneElement != nullptr ? firstLaneElement->getIntAttribute ("rootNote", 60) : 60);

    pattern.globalScale = static_cast<ScaleType> (
        element->getIntAttribute ("globalScale", static_cast<int> (pattern.globalScale)));
    pattern.globalRoot = element->getIntAttribute ("globalRoot", pattern.globalRoot);
    pattern.customScaleMask = static_cast<juce::uint16> (
        element->getIntAttribute ("customMask", static_cast<int> (pattern.customScaleMask)));
    pattern.scaleEnabled = element->getIntAttribute ("scaleEnabled", 0) != 0;

    for (const auto* laneElement : element->getChildIterator())
    {
        if (! laneElement->hasTagName ("lane"))
            continue;

        const auto laneIndex = laneElement->getIntAttribute ("index", -1);

        if (laneIndex < 0 || laneIndex >= maxLanes)
            continue;

        auto& lane = pattern.lanes[static_cast<std::size_t> (laneIndex)];
        auto& settings = lane.settings;

        settings.enabled = laneElement->getIntAttribute ("enabled", 0) != 0;
        settings.type = static_cast<LaneType> (laneElement->getIntAttribute ("type", 0));
        settings.midiChannel = laneElement->getIntAttribute ("channel", 1);
        settings.clock.numerator = laneElement->getIntAttribute ("clockNum", 1);
        settings.clock.denominator = laneElement->getIntAttribute ("clockDen", 16);
        settings.clock.modifier = static_cast<ClockModifier> (laneElement->getIntAttribute ("clockMod", 0));
        settings.gateLength = laneElement->getIntAttribute ("length", 16);
        settings.pitchLength = laneElement->getIntAttribute ("pitchLength", 16);
        settings.velocityLength = laneElement->getIntAttribute ("velocityLength", 16);
        settings.modLength = laneElement->getIntAttribute ("modLength", 16);
        settings.modController = laneElement->getIntAttribute ("controller", 74);
        settings.direction = static_cast<PlayDirection> (laneElement->getIntAttribute ("direction", 0));
        settings.euclidEnabled = laneElement->getIntAttribute ("euclid", 0) != 0;
        settings.euclidSteps = laneElement->getIntAttribute ("euclidSteps", 16);
        settings.euclidPulses = laneElement->getIntAttribute ("euclidPulses", 4);
        settings.euclidRotation = laneElement->getIntAttribute ("euclidRotation", 0);
        settings.slideMode = static_cast<SlideMode> (laneElement->getIntAttribute ("slideMode", 0));
        settings.bendRangeSemitones = laneElement->getDoubleAttribute ("bendRange", 2.0);
        settings.slideOverlapMs = laneElement->getDoubleAttribute ("overlapMs", 12.0);
        settings.accentCC = laneElement->getIntAttribute ("accentCC", 74);
        settings.accentCCValue = laneElement->getIntAttribute ("accentCCValue", 110);
        settings.accentVelocityBoost = laneElement->getIntAttribute ("accentBoost", 25);

        for (const auto* stepElement : laneElement->getChildIterator())
        {
            if (! stepElement->hasTagName ("step"))
                continue;

            const auto index = stepElement->getIntAttribute ("i", -1);

            if (index < 0 || index >= maxSteps)
                continue;

            auto& step = lane.steps[static_cast<std::size_t> (index)];

            step.trigger = stepElement->getIntAttribute ("trig", 0) != 0;
            step.velocity = static_cast<juce::uint8> (stepElement->getIntAttribute ("vel", 100));
            step.gatePercent = static_cast<juce::uint8> (stepElement->getIntAttribute ("gate", 50));
            step.microTiming = static_cast<juce::int8> (stepElement->getIntAttribute ("micro", 0));
            step.probability = static_cast<juce::uint8> (stepElement->getIntAttribute ("prob", 100));
            step.condition = static_cast<CycleCondition> (stepElement->getIntAttribute ("cond", 0));
            step.ratchets = static_cast<juce::uint8> (stepElement->getIntAttribute ("ratchets", 1));
            step.ratchetRamp = static_cast<VelocityRamp> (stepElement->getIntAttribute ("ramp", 0));
            step.slide = stepElement->getIntAttribute ("slide", 0) != 0;
            step.accent = stepElement->getIntAttribute ("accent", 0) != 0;
            step.pitch = static_cast<juce::int8> (stepElement->getIntAttribute ("pitch", 0));
            step.mod = static_cast<juce::uint8> (stepElement->getIntAttribute ("mod", 64));
        }
    }

    return true;
}

} // namespace kinetic::patternstate
