#pragma once

#include "SequencerTypes.h"

#include <cmath>

namespace kinetic::math
{

/** Small deterministic xorshift PRNG, so probability and random-walk are reproducible in tests. */
class Rng
{
public:
    explicit Rng (juce::uint32 seed = 0x9e3779b9u) noexcept { setSeed (seed); }

    void setSeed (juce::uint32 seed) noexcept { state = seed == 0 ? 1u : seed; }

    juce::uint32 nextUInt() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** Uniform in [0, 1). */
    float nextFloat() noexcept { return static_cast<float> (nextUInt() >> 8) * (1.0f / 16777216.0f); }

    bool nextBool() noexcept { return (nextUInt() & 1u) != 0u; }

private:
    juce::uint32 state = 1u;
};

//==============================================================================
/**
    Euclidean rhythm as an evenly spaced bit pattern.

    bit(i) is on when floor(i * pulses / steps) steps up at i, which distributes `pulses`
    over `steps` as evenly as Bjorklund's algorithm does, without the iterative state.
*/
inline bool euclidBit (int index, int steps, int pulses) noexcept
{
    steps = juce::jlimit (1, maxSteps, steps);
    pulses = juce::jlimit (0, steps, pulses);

    const auto i = ((index % steps) + steps) % steps;

    if (pulses <= 0)  return false;
    if (pulses >= steps) return true;

    return ((i * pulses) % steps) < pulses;
}

/** Same pattern, rotated by `rotation` steps (positive rotates later in time). */
inline bool euclidBitRotated (int index, int steps, int pulses, int rotation) noexcept
{
    steps = juce::jlimit (1, maxSteps, steps);
    const auto r = ((rotation % steps) + steps) % steps;
    return euclidBit ((index + steps - r) % steps, steps, pulses);
}

//==============================================================================
/**
    Cycle conditions.

    A cycle is one completed pass of the lane's trigger pattern. `1:2` plays on the first of
    every two passes, `Fill` on the last of every four.
*/
inline bool conditionPasses (CycleCondition condition, long long cycle) noexcept
{
    const auto c = cycle < 0 ? 0 : cycle;

    switch (condition)
    {
        case CycleCondition::always:    return true;
        case CycleCondition::first:     return c == 0;
        case CycleCondition::notFirst:  return c > 0;
        case CycleCondition::fill:      return (c % 4) == 3;
        case CycleCondition::oneOfTwo:  return (c % 2) == 0;
        case CycleCondition::twoOfTwo:  return (c % 2) == 1;
        case CycleCondition::oneOfFour: return (c % 4) == 0;
        case CycleCondition::threeOfFour: return (c % 4) == 2;
        case CycleCondition::fourOfFour:  return (c % 4) == 3;
    }

    return true;
}

//==============================================================================
/** 12-bit pitch-class mask for a scale, with C at bit 0. */
inline juce::uint16 scaleMask (ScaleType type, juce::uint16 customMask) noexcept
{
    switch (type)
    {
        case ScaleType::major:            return 0x0ab5;   // 0 2 4 5 7 9 11
        case ScaleType::minor:            return 0x05ad;   // 0 2 3 5 7 8 10
        case ScaleType::dorian:           return 0x06ad;   // 0 2 3 5 7 9 10
        case ScaleType::phrygianDominant: return 0x05b3;   // 0 1 4 5 7 8 10
        case ScaleType::pentatonic:       return 0x0295;   // 0 2 4 7 9
        case ScaleType::custom:           return static_cast<juce::uint16> (customMask & 0x0fffu);
        case ScaleType::chromatic:        break;
    }

    return 0x0fff;
}

/** Snaps a note to the nearest allowed pitch class, resolving ties upward. */
inline int quantise (int note, juce::uint16 mask, int root) noexcept
{
    if ((mask & 0x0fffu) == 0x0fffu)
        return note;

    for (int distance = 0; distance <= 6; ++distance)
    {
        const auto up = note + distance;

        if (((mask >> ((((up - root) % 12) + 12) % 12)) & 1) != 0)
            return up;

        if (distance > 0)
        {
            const auto down = note - distance;

            if (((mask >> ((((down - root) % 12) + 12) % 12)) & 1) != 0)
                return down;
        }
    }

    return note;
}

//==============================================================================
/** Play-direction walker: advances through a lane's step indices in the chosen order. */
class StepWalker
{
public:
    void reset (int startIndex = 0) noexcept
    {
        index = juce::jlimit (0, maxSteps - 1, startIndex);
        direction = 1;
    }

    int current() const noexcept { return index; }

    /** Where advance() would land, without mutating state. Random walk cannot peek. */
    int peekNext (int length, PlayDirection mode) const noexcept
    {
        length = juce::jlimit (1, maxSteps, length);

        switch (mode)
        {
            case PlayDirection::forward:  return (index + 1) % length;
            case PlayDirection::reverse:  return (index - 1 + length) % length;

            case PlayDirection::pingPong:
                if (length == 1) return 0;
                if (direction > 0) return index >= length - 1 ? length - 2 : index + 1;
                return index <= 0 ? 1 : index - 1;

            case PlayDirection::pendulum:
                if (length == 1) return 0;
                if (direction > 0) return index >= length - 1 ? length - 1 : index + 1;
                return index <= 0 ? 0 : index - 1;

            case PlayDirection::randomWalk:
                break;
        }

        return index;
    }

    int advance (int length, PlayDirection mode, Rng& rng) noexcept
    {
        length = juce::jlimit (1, maxSteps, length);

        switch (mode)
        {
            case PlayDirection::forward:
                index = (index + 1) % length;
                break;

            case PlayDirection::reverse:
                index = (index - 1 + length) % length;
                break;

            case PlayDirection::pingPong:
                if (length == 1) { index = 0; break; }
                if (direction > 0)
                {
                    if (index >= length - 1) { direction = -1; index = length - 2; }
                    else                     ++index;
                }
                else
                {
                    if (index <= 0) { direction = 1; index = 1; }
                    else            --index;
                }
                break;

            case PlayDirection::pendulum:
                if (length == 1) { index = 0; break; }
                if (direction > 0)
                {
                    if (index >= length - 1) direction = -1;   // repeat the end step
                    else                     ++index;
                }
                else
                {
                    if (index <= 0) direction = 1;             // repeat the start step
                    else            --index;
                }
                break;

            case PlayDirection::randomWalk:
                index = (index + (rng.nextBool() ? 1 : -1) + length) % length;
                break;
        }

        index = juce::jlimit (0, length - 1, index);
        return index;
    }

private:
    int index = 0;
    int direction = 1;
};

} // namespace kinetic::math
