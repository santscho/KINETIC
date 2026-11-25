#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace kinetic
{

constexpr int maxLanes = 8;
constexpr int maxSteps = 64;

enum class LaneType : juce::uint8 { note, drum, cc };
enum class PlayDirection : juce::uint8 { forward, reverse, pingPong, pendulum, randomWalk };
enum class VelocityRamp : juce::uint8 { flat, rampUp, rampDown };
enum class SlideMode : juce::uint8 { legatoOverlap, portamentoCC65, pitchBendRamp };
enum class ScaleType : juce::uint8 { chromatic, major, minor, dorian, phrygianDominant, pentatonic, custom };
enum class CycleCondition : juce::uint8 { always, first, notFirst, fill, oneOfTwo, twoOfTwo, oneOfFour, threeOfFour, fourOfFour };
enum class ClockModifier : juce::uint8 { straight, dotted, triplet };

/**
    Step duration, expressed against a quarter note.

    stepPpq = 4 * numerator / denominator, then scaled for dotted (x1.5) or triplet (x2/3).
    1/4 -> 1.0 PPQ (a quarter note), 1/16 -> 0.25, 1/64 -> 0.0625. Any numerator/denominator
    pair is legal, which covers "custom ratios" as well as the standard note values.
*/
struct ClockRatio
{
    int numerator = 1;
    int denominator = 4;
    ClockModifier modifier = ClockModifier::straight;

    double stepPpq() const noexcept
    {
        const auto base = 4.0 * static_cast<double> (numerator)
                              / static_cast<double> (juce::jmax (1, denominator));

        switch (modifier)
        {
            case ClockModifier::dotted:  return base * 1.5;
            case ClockModifier::triplet: return base * (2.0 / 3.0);
            case ClockModifier::straight: break;
        }

        return base;
    }
};

/**
    One stored step.

    A step carries every parameter, but the lane reads it through four independent loop
    lengths (trigger/gate, pitch, velocity, mod), so the same array yields polymetric
    combinations - index `n` of the gate sub-lane can line up with index `n % 3` of the
    pitch sub-lane, and so on.
*/
struct Step
{
    // Trigger / gate sub-lane
    bool trigger = false;
    juce::uint8 velocity = 100;        // 0-127
    juce::uint8 gatePercent = 50;      // 2-100 % of the step
    juce::int8  microTiming = 0;       // -50..+50 % of the step width
    juce::uint8 probability = 100;     // 0-100 %
    CycleCondition condition = CycleCondition::always;
    juce::uint8 ratchets = 1;          // 1-8 sub-triggers
    VelocityRamp ratchetRamp = VelocityRamp::flat;
    bool slide = false;
    bool accent = false;

    // Pitch / velocity / mod sub-lanes
    juce::int8  pitch = 0;             // semitone offset from the lane root
    juce::uint8 mod = 64;              // CC value when the lane is a CC lane
};

struct LaneSettings
{
    bool enabled = false;
    LaneType type = LaneType::note;
    int midiChannel = 1;               // 1-16
    int rootNote = 60;

    ClockRatio clock { 1, 16, ClockModifier::straight };

    // Decoupled sub-lane loop lengths (1-64)
    int gateLength = 16;
    int pitchLength = 16;
    int velocityLength = 16;
    int modLength = 16;

    int modController = 74;            // CC number for CC lanes

    PlayDirection direction = PlayDirection::forward;

    // Euclidean generator (replaces the painted trigger pattern while enabled)
    bool euclidEnabled = false;
    int euclidSteps = 16;
    int euclidPulses = 4;
    int euclidRotation = 0;

    // Scale quantiser
    bool useGlobalScale = true;
    ScaleType scale = ScaleType::chromatic;
    int scaleRoot = 0;

    // Acid translation
    SlideMode slideMode = SlideMode::legatoOverlap;
    double bendRangeSemitones = 2.0;
    double slideOverlapMs = 12.0;

    // Accent translation
    int accentCC = 74;
    int accentCCValue = 110;
    int accentVelocityBoost = 25;
};

struct Lane
{
    LaneSettings settings {};
    std::array<Step, maxSteps> steps {};
};

struct Pattern
{
    std::array<Lane, maxLanes> lanes {};
    ScaleType globalScale = ScaleType::chromatic;
    int globalRoot = 0;
    juce::uint16 customScaleMask = 0x0fff;
};

} // namespace kinetic
